#include "PrintJobOrchestrator.hpp"

#include "PrintJobStepExecutors.hpp"

#include "../GUI_App.hpp"
#include "../AICoach/AIGuiOrchestrator.hpp"
#include "../ModelSearch/ModelSearchService.hpp"
#include "../OllamaAssistant/AiLocale.hpp"

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "libslic3r/BambuSmartPrint/BambuSmartPrintTypes.hpp" // ModelAnalysis
#include "libslic3r/BambuSmartPrint/PrintEstimateSummary.hpp" // PrintEstimateFormatter
#include "libslic3r/BambuSmartPrint/PrintIntentClarifier.hpp"
#include "libslic3r/BambuSmartPrint/PrintIntentSession.hpp"

#include <wx/string.h>
#include <wx/timer.h>

#include <boost/log/trivial.hpp>

#include <algorithm>
#include <functional>
#include <mutex>
#include <string>
#include <utility>

namespace Slic3r { namespace GUI { namespace AIPipeline {

namespace {

// --- Process-wide registries shared with the Plater import hook and the
//     AIGuiOrchestrator SliceDone publisher (all touched on the main thread,
//     but mutex-guarded for defensiveness). ---
std::mutex& import_mutex() { static std::mutex m; return m; }
PrintJobOrchestrator*& import_owner_ref() { static PrintJobOrchestrator* p = nullptr; return p; }

std::mutex& slice_mutex() { static std::mutex m; return m; }
std::string& slicing_job_id_ref() { static std::string s; return s; }

// wxTimer whose Notify() invokes a std::function (no wxEvtHandler owner needed).
// Used for the per-state watchdog; fires on the main thread.
class CallbackTimer : public wxTimer
{
public:
    explicit CallbackTimer(std::function<void()> fn) : m_fn(std::move(fn)) {}
    void Notify() override
    {
        if (m_fn)
            m_fn();
    }

private:
    std::function<void()> m_fn;
};

// Waiting-state deadlines: import waits on Plater's download/import hook,
// slicing waits on the SliceDone event — both can silently never arrive.
// Search waits on a detached worker (Ollama translation + MakerWorld HTTP) and
// can also hang without a deadline, leaving the UI stuck on "1/6 검색".
constexpr int kSearchTimeoutMs = 60 * 1000;
constexpr int kImportTimeoutMs = 120 * 1000;
constexpr int kSliceTimeoutMs  = 300 * 1000;

} // namespace

// ------------------------------------------------------------------ Flag / RAII

bool print_job_orchestrator_enabled()
{
    // Enabled by default. If app_config is unavailable we cannot read the flag,
    // so we return false to stay on the safe (legacy) path and avoid touching an
    // uninitialized config.
    if (!wxGetApp().app_config)
        return false;
    const std::string v = wxGetApp().app_config->get("ollama", "print_job_orchestrator");
    // Unset/empty => default ON. Only an explicit disable value turns it off,
    // preserving a power-user escape hatch.
    if (v.empty())
        return true;
    if (v == "0" || v == "false" || v == "no" || v == "off")
        return false;
    return true;
}

void ScopedEventSubscription::reset()
{
    if (m_id != 0) {
        OllamaAgentEventBus::instance().unsubscribe(m_id);
        m_id = 0;
    }
}

// ------------------------------------------------------------------ Lifecycle

PrintJobOrchestrator::PrintJobOrchestrator()
    : m_alive(std::make_shared<std::atomic<bool>>(true))
{
}

PrintJobOrchestrator::~PrintJobOrchestrator()
{
    if (m_alive)
        m_alive->store(false); // invalidate all outstanding async continuations
    disarm_watchdog();
    m_slice_sub.reset();
    clear_import_owner();
    clear_slicing_job();
}

// ------------------------------------------------------------------ Static hooks

bool PrintJobOrchestrator::has_active_import()
{
    std::lock_guard<std::mutex> lk(import_mutex());
    return import_owner_ref() != nullptr;
}

void PrintJobOrchestrator::dispatch_import_done(bool ok, const std::string& detail)
{
    PrintJobOrchestrator* owner = nullptr;
    {
        std::lock_guard<std::mutex> lk(import_mutex());
        owner = import_owner_ref();
    }
    if (owner)
        owner->on_plater_import_done(ok, detail);
}

std::string PrintJobOrchestrator::current_slicing_job_id()
{
    std::lock_guard<std::mutex> lk(slice_mutex());
    return slicing_job_id_ref();
}

void PrintJobOrchestrator::register_import_owner()
{
    std::lock_guard<std::mutex> lk(import_mutex());
    import_owner_ref() = this;
}

void PrintJobOrchestrator::clear_import_owner()
{
    std::lock_guard<std::mutex> lk(import_mutex());
    if (import_owner_ref() == this)
        import_owner_ref() = nullptr;
}

void PrintJobOrchestrator::set_slicing_job()
{
    std::lock_guard<std::mutex> lk(slice_mutex());
    slicing_job_id_ref() = m_job ? std::to_string(m_job->job_id) : std::string{};
}

void PrintJobOrchestrator::clear_slicing_job()
{
    std::lock_guard<std::mutex> lk(slice_mutex());
    if (m_job && slicing_job_id_ref() == std::to_string(m_job->job_id))
        slicing_job_id_ref().clear();
}

// ------------------------------------------------------------------ Queries

bool PrintJobOrchestrator::is_active() const
{
    return m_job && !is_terminal(m_job->state);
}

PrintJobState PrintJobOrchestrator::state() const
{
    return m_job ? m_job->state : m_last_terminal;
}

std::uint64_t PrintJobOrchestrator::active_job_id() const
{
    return m_job ? m_job->job_id : 0;
}

bool PrintJobOrchestrator::is_current(const std::shared_ptr<PrintJob>& job, std::uint64_t jid) const
{
    return job && m_job == job && job->job_id == jid && !job->is_cancelled();
}

// ------------------------------------------------------------------ Entry points

bool PrintJobOrchestrator::start(const std::string& user_utterance, wxWindow* parent, bool apply_mode,
                                 PrintJobUiCallbacks ui)
{
    if (is_active())
        return false;

    auto job                 = std::make_shared<PrintJob>();
    job->job_id              = m_next_job_id++;
    job->user_utterance      = user_utterance;
    job->apply_mode          = apply_mode;
    job->parent              = parent;
    job->ui                  = std::move(ui);
    job->auto_slice_and_send = true; // full "find and print" intent
    m_job                    = job;
    m_last_terminal          = PrintJobState::Idle;

    // Phase 2 intent accumulator is plate/chat-scoped: reset at job start, then
    // merge the opening turn and snapshot the intent by value. start() is called
    // straight from on_send on the main thread, so a throw here would unwind the
    // wx main loop and quit the app — degrade to an empty intent instead.
    try {
        BambuSmartPrint::PrintIntentSession::instance().clear();
        BambuSmartPrint::PrintIntentSession::instance().merge_turn(user_utterance);
        job->intent = BambuSmartPrint::PrintIntentSession::instance().intent();
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "PrintJobOrchestrator: intent merge failed: " << ex.what();
        job->intent = BambuSmartPrint::PrintIntent{};
    }

    if (!m_search_begun) {
        AIGuiOrchestrator::instance().on_makerworld_search_begin();
        m_search_begun = true;
    }

    transition_to(PrintJobState::IntentExtracted);
    return true;
}

void PrintJobOrchestrator::on_user_reply(const std::string& text)
{
    if (!m_job || m_job->state != PrintJobState::Clarifying)
        return;

    ++m_job->clarify_rounds;
    try {
        BambuSmartPrint::PrintIntentSession::instance().merge_turn(text);
        m_job->intent = BambuSmartPrint::PrintIntentSession::instance().intent();
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "PrintJobOrchestrator: intent merge failed: " << ex.what();
    }
    m_job->pending_question.reset();

    transition_to(PrintJobState::IntentExtracted);
}

void PrintJobOrchestrator::select_candidate(int index)
{
    if (!m_job || m_job->state != PrintJobState::CandidateSelect)
        return;
    if (index < 0 || static_cast<size_t>(index) >= m_job->candidates.size()) {
        finish_error("bad_selection",
                     AiLocale::text("Invalid model selection.", "잘못된 모델 선택입니다.").utf8_string());
        return;
    }
    m_job->selected_index     = index;
    m_job->selected_candidate = m_job->candidates[index];
    transition_to(PrintJobState::Importing);
}

void PrintJobOrchestrator::confirm_print()
{
    if (!m_job || m_job->state != PrintJobState::ReadyToPrint)
        return;
    transition_to(PrintJobState::Sending);
}

void PrintJobOrchestrator::cancel()
{
    if (!m_job || is_terminal(m_job->state))
        return;
    m_job->cancelled.store(true);
    ModelSearchService::instance().cancel_pending();
    chat(AiLocale::text("Cancelled.", "취소했습니다."));
    finish_cancelled();
}

void PrintJobOrchestrator::on_plater_import_done(bool ok, const std::string& detail)
{
    if (!m_job || m_job->state != PrintJobState::Importing)
        return;
    disarm_watchdog();
    clear_import_owner();

    if (!ok) {
        finish_error("import_failed",
                     detail.empty() ? AiLocale::text("Import failed.", "가져오기에 실패했습니다.").utf8_string()
                                    : detail);
        return;
    }
    chat(AiLocale::text("Model loaded.", "모델을 불러왔습니다."));
    transition_to(PrintJobState::MeshHealth);
}

// ------------------------------------------------------------------ State machine

void PrintJobOrchestrator::transition_to(PrintJobState next)
{
    if (!m_job)
        return;
    m_job->state = next;

    // Schedule enter_state on the next main-loop tick so busy/status/chat can
    // repaint between steps and every step re-validates the triple guard.
    std::weak_ptr<std::atomic<bool>> weak_alive = m_alive;
    std::weak_ptr<PrintJob>          weak_job   = m_job;
    const std::uint64_t              jid        = m_job->job_id;

    wxGetApp().CallAfter([this, weak_alive, weak_job, jid]() {
        auto alive = weak_alive.lock();
        if (!alive || !alive->load())
            return;
        auto job = weak_job.lock();
        if (!is_current(job, jid))
            return;
        enter_state();
    });
}

void PrintJobOrchestrator::enter_state()
{
    if (!m_job)
        return;
    notify_step();
    // Every step touches the plater / slicer / network / config, any of which may throw.
    // enter_state runs from a main-thread CallAfter, so an uncaught throw would unwind the
    // wx main loop (OnExceptionInMainLoop rethrows) and quit the app. Fail the job instead.
    try {
        switch (m_job->state) {
        case PrintJobState::IntentExtracted:  step_intent();          break;
        case PrintJobState::Clarifying:       step_clarify();         break;
        case PrintJobState::Searching:        step_search();          break;
        case PrintJobState::CandidateSelect:  step_candidate_select();break;
        case PrintJobState::Importing:        step_import();          break;
        case PrintJobState::MeshHealth:       step_mesh_health();     break;
        case PrintJobState::Repairing:        step_repair();          break;
        case PrintJobState::GeometryAnalysis: step_geometry();        break;
        case PrintJobState::AutoConfig:       step_autoconfig();      break;
        case PrintJobState::Slicing:          step_slice();           break;
        case PrintJobState::Estimating:       step_estimate();        break;
        case PrintJobState::ReadyToPrint:     step_ready();           break;
        case PrintJobState::Sending:          step_send();            break;
        case PrintJobState::Idle:
        case PrintJobState::Done:
        case PrintJobState::Failed:
        case PrintJobState::Cancelled:
            break;
        }
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "PrintJobOrchestrator step failed: " << ex.what();
        finish_error("internal_error",
                     AiLocale::text("Something went wrong while preparing the print.",
                                    "출력 준비 중 오류가 발생했습니다.").utf8_string());
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "PrintJobOrchestrator step failed: unknown error";
        finish_error("internal_error",
                     AiLocale::text("Something went wrong while preparing the print.",
                                    "출력 준비 중 오류가 발생했습니다.").utf8_string());
    }
}

void PrintJobOrchestrator::step_intent()
{
    PrintJob& job = *m_job;

    // Recompute missing/blocking slots against an empty mesh + live base config so
    // the clarifier can surface a single blocking question (Material) before search.
    DynamicPrintConfig base;
    if (wxGetApp().preset_bundle)
        base = wxGetApp().preset_bundle->full_config();
    const BambuSmartPrint::ModelAnalysis empty_mesh;
    BambuSmartPrint::PrintIntentClarifier::recompute_missing_slots(job.intent, empty_mesh, base);

    if (job.clarify_rounds < 1) {
        if (auto q = BambuSmartPrint::PrintIntentClarifier::next_question(job.intent, korean())) {
            if (q->blocks_config) {
                job.pending_question = q;
                chat_question(wxString::FromUTF8(korean() ? q->question_ko : q->question_en));
                transition_to(PrintJobState::Clarifying);
                return;
            }
        }
    }
    transition_to(PrintJobState::Searching);
}

void PrintJobOrchestrator::step_clarify()
{
    // Waiting state: the question was already appended in step_intent. Release the
    // busy state so the user can type a reply (routed via on_user_reply()).
    set_busy(false, AiLocale::text("Waiting for your answer…", "답변을 기다리는 중…"));
}

void PrintJobOrchestrator::step_search()
{
    PrintJob& job = *m_job;

    std::string query = job.intent.object_description;
    if (query.empty())
        query = job.user_utterance;
    job.search_query = query;

    set_busy(true, AiLocale::text("Searching for models…", "모델을 검색하는 중…"));

    arm_watchdog(PrintJobState::Searching, kSearchTimeoutMs, "search_timeout",
                 AiLocale::text("Model search timed out. Check your network or try again.",
                                "모델 검색 시간이 초과되었습니다. 네트워크를 확인하거나 다시 시도해 주세요."));

    std::weak_ptr<std::atomic<bool>> weak_alive = m_alive;
    std::weak_ptr<PrintJob>          weak_job   = m_job;
    const std::uint64_t              jid        = job.job_id;

    PrintJobStepExecutors::execute_search_async(query,
        [this, weak_alive, weak_job, jid](ModelSearchAggregateResult result) {
            auto alive = weak_alive.lock();
            if (!alive || !alive->load())
                return;
            auto j = weak_job.lock();
            if (!is_current(j, jid))
                return;

            disarm_watchdog();

            if (!result.ok || result.candidates.empty()) {
                finish_error("search_empty",
                             result.error.empty()
                                 ? AiLocale::text("No models found. Try different keywords.",
                                                  "모델을 찾지 못했습니다. 다른 검색어로 시도해 보세요.").utf8_string()
                                 : result.error);
                return;
            }
            j->candidates = std::move(result.candidates);
            chat(wxString::Format(AiLocale::text("Found %d models.", "모델 %d개를 찾았습니다."),
                                  static_cast<int>(j->candidates.size())));
            transition_to(PrintJobState::CandidateSelect);
        });
}

void PrintJobOrchestrator::step_candidate_select()
{
    PrintJob& job = *m_job;
    if (job.candidates.empty()) {
        finish_error("no_candidates",
                     AiLocale::text("No models to choose from.", "선택할 모델이 없습니다.").utf8_string());
        return;
    }

    // Modal offer dialog runs its own nested loop on the main thread.
    set_busy(false, AiLocale::text("Choose a model…", "모델을 선택하세요…"));
    const int idx = PrintJobStepExecutors::show_print_offer_dialog(job.parent, job.candidates);
    if (idx < 0) {
        chat(AiLocale::text("Print cancelled.", "출력을 취소했습니다."));
        finish_cancelled();
        return;
    }
    select_candidate(idx);
}

void PrintJobOrchestrator::step_import()
{
    PrintJob& job = *m_job;

    if (!job.apply_mode) {
        chat(AiLocale::text("Switch to Apply mode to import and print.",
               "가져와서 출력하려면 적용 모드로 전환하세요."));
        finish_cancelled();
        return;
    }

    job.import_tried.clear();
    resolve_import_for_candidate(job.selected_index);
}

int PrintJobOrchestrator::next_import_fallback_index() const
{
    if (!m_job)
        return -1;
    const PrintJob& job = *m_job;
    for (int i = 0; i < static_cast<int>(job.candidates.size()); ++i) {
        if (std::find(job.import_tried.begin(), job.import_tried.end(), i) == job.import_tried.end())
            return i;
    }
    return -1;
}

void PrintJobOrchestrator::resolve_import_for_candidate(int candidate_index)
{
    PrintJob& job = *m_job;
    if (candidate_index < 0 || static_cast<size_t>(candidate_index) >= job.candidates.size()) {
        finish_error("import_failed",
                     AiLocale::text("Invalid model selection.", "잘못된 모델 선택입니다.").utf8_string());
        return;
    }

    job.import_tried.push_back(candidate_index);
    job.selected_index     = candidate_index;
    job.selected_candidate = job.candidates[candidate_index];

    set_busy(true, AiLocale::text("Preparing download…", "다운로드를 준비하는 중…"));

    // The watchdog stays armed across the async resolve AND the subsequent
    // Plater download, so a stalled network call can never hang the job.
    arm_watchdog(PrintJobState::Importing, kImportTimeoutMs, "import_timeout",
                 AiLocale::text("The model download timed out. Please try again.",
                   "모델 다운로드가 시간 내에 완료되지 않았습니다. 다시 시도해 주세요."));

    std::weak_ptr<std::atomic<bool>> weak_alive = m_alive;
    std::weak_ptr<PrintJob>          weak_job   = m_job;
    const std::uint64_t              jid        = job.job_id;

    PrintJobStepExecutors::resolve_import_async(job.selected_candidate,
        [this, weak_alive, weak_job, jid](PrintJobStepExecutors::ImportResult res) {
            auto alive = weak_alive.lock();
            if (!alive || !alive->load())
                return;
            auto j = weak_job.lock();
            if (!is_current(j, jid) || j->state != PrintJobState::Importing)
                return;
            on_import_resolved(res);
        });
}

void PrintJobOrchestrator::on_import_resolved(const PrintJobStepExecutors::ImportResult& res)
{
    PrintJob& job = *m_job;

    if (!res.ok) {
        BOOST_LOG_TRIVIAL(warning) << "PrintJobOrchestrator: import resolve failed for candidate "
                                   << job.selected_index << " (" << job.selected_candidate.id
                                   << "): " << res.error;
        // Auth problems block every candidate equally — fail fast with guidance.
        if (res.needs_login) {
            finish_error("import_needs_login",
                         res.error.empty()
                             ? AiLocale::text("Sign in to Bambu Cloud to download this model.",
                                              "이 모델을 내려받으려면 Bambu Cloud에 로그인하세요.").utf8_string()
                             : res.error);
            return;
        }

        // This candidate has no usable download link — fall back to the next one.
        const int next = next_import_fallback_index();
        if (next >= 0) {
            chat(wxString::Format(
                AiLocale::text("Model %d can't be downloaded — trying model %d instead.",
                               "%d번 모델은 받을 수 없어 %d번 모델로 진행합니다."),
                job.selected_index + 1, next + 1));
            resolve_import_for_candidate(next);
            return;
        }

        finish_error("import_failed",
                     res.error.empty()
                         ? AiLocale::text("Import failed.", "가져오기에 실패했습니다.").utf8_string()
                         : res.error);
        return;
    }

    // Resolved: hand the payload to Plater and wait for the import-done hook.
    register_import_owner(); // must be set before request_model_download so the done hook routes here
    set_busy(true, AiLocale::text("Downloading model…", "모델을 내려받는 중…"));
    PrintJobStepExecutors::begin_download(res.download_info);
    chat(wxString::Format(AiLocale::text("Downloading \"%s\"…", "\"%s\" 내려받는 중…"),
                          wxString::FromUTF8(job.selected_candidate.title.empty()
                                                 ? job.selected_candidate.id
                                                 : job.selected_candidate.title)));
}

void PrintJobOrchestrator::step_mesh_health()
{
    PrintJob& job = *m_job;
    set_busy(true, AiLocale::text("Checking mesh health…", "메시 상태를 확인하는 중…"));

    const PrintJobStepExecutors::MeshHealthResult res = PrintJobStepExecutors::assess_mesh_health();
    if (res.valid) {
        job.mesh_health       = res.json;
        job.mesh_needs_repair = res.needs_repair;
    }

    if (res.valid && res.needs_repair && job.repair_attempts < 1)
        transition_to(PrintJobState::Repairing);
    else
        transition_to(PrintJobState::GeometryAnalysis);
}

void PrintJobOrchestrator::step_repair()
{
    PrintJob& job = *m_job;
    set_busy(true, AiLocale::text("Repairing mesh…", "메시를 복구하는 중…"));
    ++job.repair_attempts;

    const int repaired = PrintJobStepExecutors::attempt_repair(job.mesh_health);
    if (repaired > 0)
        chat(wxString::Format(AiLocale::text("Repaired %d mesh volume(s).", "메시 볼륨 %d개를 복구했습니다."), repaired));
    else
        chat(AiLocale::text("No automatic mesh repair was applied.", "자동 메시 복구를 적용하지 않았습니다."));

    // Re-assess once so downstream steps see the post-repair state.
    const PrintJobStepExecutors::MeshHealthResult res = PrintJobStepExecutors::assess_mesh_health();
    if (res.valid) {
        job.mesh_health       = res.json;
        job.mesh_needs_repair = res.needs_repair;
    }
    transition_to(PrintJobState::GeometryAnalysis);
}

void PrintJobOrchestrator::step_geometry()
{
    PrintJob& job = *m_job;
    set_busy(true, AiLocale::text("Analyzing geometry…", "형상을 분석하는 중…"));

    const PrintJobStepExecutors::GeometryResult res = PrintJobStepExecutors::build_geometry_context();
    if (!res.ok) {
        finish_error("geometry_failed",
                     AiLocale::text("Could not analyze the model geometry.",
                                    "모델 형상을 분석하지 못했습니다.").utf8_string());
        return;
    }
    job.plate_context = res.ctx;
    job.assessment    = res.assessment;
    transition_to(PrintJobState::AutoConfig);
}

void PrintJobOrchestrator::step_autoconfig()
{
    PrintJob& job = *m_job;
    set_busy(true, AiLocale::text("Preparing print settings…", "출력 설정을 준비하는 중…"));

    job.proposal = PrintJobStepExecutors::build_and_cache_proposal(job.plate_context, job.intent, korean());
    const int changed = PrintJobStepExecutors::apply_config_proposal(job.proposal);
    if (changed > 0)
        chat(wxString::Format(AiLocale::text("Applied %d setting change(s).", "설정 %d개를 적용했습니다."), changed));

    transition_to(PrintJobState::Slicing);
}

void PrintJobOrchestrator::step_slice()
{
    set_busy(true, AiLocale::text("Slicing…", "슬라이싱 중…"));
    set_slicing_job();

    // Job-scoped one-shot SliceDone subscription. The permanent
    // OllamaAgentController subscriber is untouched.
    std::weak_ptr<std::atomic<bool>> weak_alive = m_alive;
    std::weak_ptr<PrintJob>          weak_job   = m_job;
    const std::uint64_t              jid        = m_job->job_id;

    const OllamaSubscriptionId id = OllamaAgentEventBus::instance().subscribe(
        [this, weak_alive, weak_job, jid](const OllamaAgentEvent& evt) {
            if (evt.kind != OllamaAgentEventKind::SliceDone)
                return;
            // Strict job-id match. AIGuiOrchestrator stamps every SliceDone with
            // current_slicing_job_id(), which we registered (set_slicing_job)
            // before triggering the slice — so a SliceDone for OUR slice always
            // carries our id. An empty/mismatching id means an unrelated or
            // legacy slice and must never complete this job.
            const std::string payload_id = evt.payload.value("job_id", std::string{});
            if (payload_id != std::to_string(jid))
                return;
            auto alive = weak_alive.lock();
            if (!alive || !alive->load())
                return;
            // Marshal onto the main thread and re-validate before mutating state.
            wxGetApp().CallAfter([this, weak_alive, weak_job, jid, evt]() {
                auto a = weak_alive.lock();
                if (!a || !a->load())
                    return;
                auto j = weak_job.lock();
                if (!is_current(j, jid))
                    return;
                on_agent_event(evt);
            });
        });
    m_slice_sub = ScopedEventSubscription(id);

    if (!PrintJobStepExecutors::trigger_slice()) {
        finish_error("slice_failed",
                     AiLocale::text("The slicer is not ready to slice.",
                                    "슬라이서가 아직 준비되지 않았습니다.").utf8_string());
        return;
    }
    // Wait for SliceDone -> on_agent_event(), with a deadline in case the
    // background slice never reports back.
    arm_watchdog(PrintJobState::Slicing, kSliceTimeoutMs, "slice_timeout",
                 AiLocale::text("Slicing did not finish in time. Please try again.",
                   "슬라이싱이 시간 내에 완료되지 않았습니다. 다시 시도해 주세요."));
}

void PrintJobOrchestrator::on_agent_event(const OllamaAgentEvent& evt)
{
    if (!m_job || m_job->state != PrintJobState::Slicing)
        return;
    if (evt.kind != OllamaAgentEventKind::SliceDone)
        return;

    disarm_watchdog();
    m_slice_sub.reset(); // one-shot
    clear_slicing_job();

    const bool success = evt.payload.value("success", false);
    m_job->slice_succeeded = success;
    if (!success) {
        finish_error("slice_failed",
                     AiLocale::text("Slicing failed. Adjust the model or settings and try again.",
                                    "슬라이싱에 실패했습니다. 모델이나 설정을 조정한 뒤 다시 시도해 주세요.").utf8_string());
        return;
    }
    transition_to(PrintJobState::Estimating);
}

void PrintJobOrchestrator::step_estimate()
{
    PrintJob& job = *m_job;
    set_busy(true, AiLocale::text("Estimating print…", "출력 예측 중…"));

    job.estimate = PrintJobStepExecutors::collect_estimate(job.assessment);
    if (job.estimate.valid)
        chat(wxString::FromUTF8(BambuSmartPrint::PrintEstimateFormatter::format_chat_block(job.estimate, korean())));

    transition_to(PrintJobState::ReadyToPrint);
}

void PrintJobOrchestrator::step_ready()
{
    PrintJob& job = *m_job;
    if (job.auto_slice_and_send) {
        transition_to(PrintJobState::Sending);
        return;
    }
    // Manual confirmation path: wait for confirm_print().
    chat(AiLocale::text("Ready to print. Confirm to send to the printer.",
           "출력 준비가 되었습니다. 확인하면 프린터로 전송합니다."));
    set_busy(false);
}

void PrintJobOrchestrator::step_send()
{
    set_busy(true, AiLocale::text("Sending to printer…", "프린터로 전송하는 중…"));

    const PrintJobStepExecutors::SendResult res = PrintJobStepExecutors::trigger_send();
    if (res.setup_blocked) {
        chat(res.blocked_message.empty()
                 ? AiLocale::text("Complete printer setup before sending.", "전송 전에 프린터 설정을 완료하세요.")
                 : wxString::FromUTF8(res.blocked_message));
    } else {
        chat(AiLocale::text("Sending the model to your printer.", "모델을 프린터로 전송합니다."));
    }
    finish_success();
}

// ------------------------------------------------------------------ Finalizers

void PrintJobOrchestrator::finish_success()
{
    if (!m_job)
        return;
    m_job->state = PrintJobState::Done;
    finish_common();
}

void PrintJobOrchestrator::finish_error(const std::string& code, const std::string& message)
{
    if (!m_job)
        return;
    PrintJobError err;
    err.failed_state = m_job->state;
    err.code         = code;
    err.message      = message;
    m_job->error     = err;

    chat_error(wxString::FromUTF8(message));
    m_job->state = PrintJobState::Failed;
    finish_common();
}

void PrintJobOrchestrator::finish_cancelled()
{
    if (!m_job)
        return;
    m_job->state = PrintJobState::Cancelled;
    finish_common();
}

void PrintJobOrchestrator::finish_common()
{
    // The ONLY place the pipeline unwinds: unsubscribe, release shared registries,
    // clear busy and fire on_finished exactly once.
    disarm_watchdog();
    m_slice_sub.reset();
    clear_slicing_job();
    clear_import_owner();

    if (m_search_begun) {
        AIGuiOrchestrator::instance().on_makerworld_search_end();
        m_search_begun = false;
    }

    set_busy(false);
    m_last_terminal = m_job ? m_job->state : PrintJobState::Idle;

    notify_step(); // terminal state → progress UI hides itself
    if (m_job && m_job->ui.on_finished)
        m_job->ui.on_finished();
    // m_job is retained (terminal) so state()/active_job_id() stay queryable; the
    // next start() replaces it. Stale async continuations fail the id guard.
}

// ------------------------------------------------------------------ Watchdog

void PrintJobOrchestrator::arm_watchdog(PrintJobState guarded_state, int timeout_ms, const char* code,
                                        const wxString& message)
{
    disarm_watchdog();
    if (!m_job)
        return;

    std::weak_ptr<std::atomic<bool>> weak_alive = m_alive;
    std::weak_ptr<PrintJob>          weak_job   = m_job;
    const std::uint64_t              jid        = m_job->job_id;
    const std::string                code_str   = code;
    const std::string                msg_utf8   = message.utf8_string();

    // Raw `this` is safe: the timer is owned by this orchestrator (destroyed
    // with it) and Notify() fires on the main thread; the triple guard below
    // additionally rejects stale jobs and state changes.
    m_watchdog = std::make_unique<CallbackTimer>([this, weak_alive, weak_job, jid, guarded_state, code_str, msg_utf8]() {
        auto alive = weak_alive.lock();
        if (!alive || !alive->load())
            return;
        auto job = weak_job.lock();
        if (!is_current(job, jid) || job->state != guarded_state)
            return;
        BOOST_LOG_TRIVIAL(error) << "PrintJobOrchestrator watchdog: job " << jid << " timed out ("
                                 << code_str << ")";
        finish_error(code_str, msg_utf8);
    });
    m_watchdog->StartOnce(timeout_ms);
}

void PrintJobOrchestrator::disarm_watchdog()
{
    if (m_watchdog) {
        m_watchdog->Stop();
        m_watchdog.reset();
    }
}

// ------------------------------------------------------------------ UI helpers

void PrintJobOrchestrator::set_busy(bool busy, const wxString& status)
{
    if (m_job && m_job->ui.set_busy)
        m_job->ui.set_busy(busy, status);
}

void PrintJobOrchestrator::notify_step()
{
    if (m_job && m_job->ui.on_step)
        m_job->ui.on_step(m_job->state, wxString{});
}

void PrintJobOrchestrator::chat(const wxString& message)
{
    if (m_job && m_job->ui.append_chat)
        m_job->ui.append_chat(message);
}

void PrintJobOrchestrator::chat_question(const wxString& question)
{
    if (!m_job)
        return;
    if (m_job->ui.append_question)
        m_job->ui.append_question(question);
    else if (m_job->ui.append_chat)
        m_job->ui.append_chat(question);
}

void PrintJobOrchestrator::chat_error(const wxString& message)
{
    if (!m_job)
        return;
    if (m_job->ui.append_error)
        m_job->ui.append_error(message);
    else if (m_job->ui.append_chat)
        m_job->ui.append_chat(message);
}

bool PrintJobOrchestrator::korean() const
{
    return AiLocale::korean();
}

}}} // namespace Slic3r::GUI::AIPipeline

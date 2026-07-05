#include "OllamaAgentController.hpp"

#include "OllamaActionExecutor.hpp"
#include "OllamaActionPipeline.hpp"
#include "OllamaActionWorkflow.hpp"
#include "OllamaAgentGoalPlanner.hpp"
#include "OllamaAgentStateService.hpp"
#include "OllamaAgentEventBus.hpp"
#include "OllamaAssistContextBuilder.hpp"
#include "OllamaConfig.hpp"
#include "OllamaExecutionPolicy.hpp"
#include "OllamaToolRegistry.hpp"
#include "OllamaToolResult.hpp"
#include "OllamaConfigProposalBuilder.hpp"

#include "../AICoach/AIGuiOrchestrator.hpp"
#include "../BambuSmartPrint/PrintPlannerGui.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"

#include "libslic3r/BambuSmartPrint/PrintIntentSession.hpp"

#include <boost/log/trivial.hpp>

#include <atomic>
#include <thread>

namespace Slic3r { namespace GUI {

namespace {

std::string build_assist_loop_system_prompt(bool korean)
{
    std::string prompt = OllamaActionExecutor::build_system_prompt(true);
    prompt += korean
        ? "\n\n## 에이전트 루프 (Cursor 방식)\n"
          "- 가능하면 한 응답에 관련 actions를 묶어 실행하세요 (예: rotate + scale 동시).\n"
          "- slicer_context가 이미 제공되면 get_state를 반복하지 마세요. 확인이 꼭 필요할 때만 호출.\n"
          "- JSON에 \"done\":false 를 유지하고, 목표 달성 시에만 done:true 와 요약 message.\n"
          "- **형상 요청**(크기·회전·구멍): scale/rotate/mesh_boolean 등만 — set_config·slice 금지(슬라이스 요청 시 제외).\n"
          "- **인쇄 증상/품질**(현재 플레이트에 있는 출력물의 문제): set_config만 — 형상 도구 금지(이동·편집 요청 없으면).\n"
          "- **새 물건 출력 요청**(\"용 피규어 출력해줘\"처럼 플레이트에 없는 물건 이름): makerworld_find_and_print (영어 query) — set_config·slice 금지.\n"
          "- 50% 축소 → scale factor 0.5. 구멍 → mesh_boolean subtract_cylinder.\n"
          "- 증상/품질 요청: slicer_context·wiki_evidence·settings_analysis·candidate_keys를 읽고 원인→최소 변경.\n"
          "- 메쉬 요청: mesh_health 확인 후 repair_mesh/mesh_boolean/mirror_mesh/scale/split_mesh.\n"
          "- 기능 설계(튼튼/가볍): mesh 작업 + set_config를 함께 계획할 수 있습니다.\n"
        : "\n\n## Agent loop (Cursor-style)\n"
          "- Batch related actions in one reply when safe (e.g. rotate + scale together).\n"
          "- Do not repeat get_state when slicer_context is already provided — only when verification is needed.\n"
          "- Keep \"done\":false until the goal is fully met; then done:true with summary message.\n"
          "- **Model shape** (resize, rotate, hole): scale/rotate/mesh_boolean only — no set_config or slice unless user asked to slice.\n"
          "- **Print symptoms/quality** (issues with the model already on the CURRENT plate): set_config only — no geometry tools unless user asked to move or edit the model.\n"
          "- **Print a new named object** (e.g. \"print me a dragon figure\" and it is not on the plate): makerworld_find_and_print with an English query — never set_config/slice for this.\n"
          "- 50% smaller → scale factor 0.5. Drill hole → mesh_boolean subtract_cylinder.\n"
          "- For symptoms/quality: read slicer_context, wiki_evidence, settings_analysis, candidate_keys; infer cause → minimal fix.\n"
          "- For mesh: check mesh_health, then repair_mesh / mesh_boolean / mirror_mesh / scale / split_mesh.\n"
          "- Functional design (stronger/lighter): plan mesh ops and set_config together when needed.\n";
    prompt += OllamaToolRegistry::agent_tools_schema_block(korean);
    return prompt;
}

void trim_assist_messages(std::vector<OllamaMessage>& messages)
{
    constexpr size_t kKeepTail = 20;
    if (messages.size() <= 1 + kKeepTail)
        return;
    std::vector<OllamaMessage> trimmed;
    trimmed.reserve(1 + kKeepTail);
    trimmed.push_back(messages.front());
    trimmed.insert(trimmed.end(), messages.end() - static_cast<ptrdiff_t>(kKeepTail), messages.end());
    messages.swap(trimmed);
}

void reorder_actions_readonly_first(nlohmann::json& root)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return;
    nlohmann::json readonly = nlohmann::json::array();
    nlohmann::json mutating = nlohmann::json::array();
    for (const auto& a : root["actions"]) {
        if (!a.is_object()) {
            mutating.push_back(a);
            continue;
        }
        const std::string type = a.value("type", "");
        if (ollama_action_is_readonly(type))
            readonly.push_back(a);
        else
            mutating.push_back(a);
    }
    root["actions"] = nlohmann::json::array();
    for (const auto& a : readonly)
        root["actions"].push_back(a);
    for (const auto& a : mutating)
        root["actions"].push_back(a);
}

bool root_is_done(const nlohmann::json& root)
{
    return root.value("done", false) || root.value("status", "") == "complete";
}

OllamaAgentController* g_active_agent = nullptr;
bool                   g_agent_event_hook_installed = false;

bool step_results_had_effective_change(const std::vector<nlohmann::json>& step_tool_results)
{
    for (const auto& step : step_tool_results) {
        if (!step.is_array())
            continue;
        for (const auto& r : step) {
            if (r.is_object() && r.value("ok", false) && r.value("changed", false))
                return true;
        }
    }
    return false;
}

std::string pick_user_facing_message(const std::vector<nlohmann::json>& step_tool_results,
                                     const std::string& assistant_msg, bool korean)
{
    if (step_results_had_effective_change(step_tool_results) || !step_tool_results.empty())
        return ollama_user_facing_summary(step_tool_results, assistant_msg, korean);
    return assistant_msg;
}

void on_global_agent_event(const OllamaAgentEvent& evt)
{
    if (evt.kind != OllamaAgentEventKind::SliceDone)
        return;
    OllamaAgentController* agent = g_active_agent;
    if (!agent || !agent->is_running())
        return;
    const bool success = evt.payload.value("success", false);
    wxGetApp().CallAfter([agent, success]() {
        if (!wxGetApp().initialized() || wxGetApp().is_closing())
            return;
        if (!g_active_agent || g_active_agent != agent || !agent->is_running())
            return;
        agent->handle_agent_event({OllamaAgentEventKind::SliceDone, {{"success", success}}});
    });
}

void ensure_agent_event_hook()
{
    if (g_agent_event_hook_installed)
        return;
    g_agent_event_hook_installed = true;
    OllamaAgentEventBus::instance().subscribe(on_global_agent_event);
}

} // namespace

OllamaAgentController::OllamaAgentController(OllamaClient& client, std::string model)
    : m_client(client), m_model(std::move(model)), m_alive(std::make_shared<std::atomic<bool>>(true))
{
}

OllamaAgentController::~OllamaAgentController()
{
    // Invalidate the liveness token BEFORE the object is gone. All async
    // continuations (client callbacks, scheduled verifications, the global
    // SliceDone hook) run on the main thread via CallAfter and check
    // m_alive / g_active_agent first; this destructor also runs on the main
    // thread, so flipping the token here makes any captured raw `this`
    // unreachable after the check.
    m_alive->store(false);
    m_running        = false;
    m_awaiting_slice = false;
    if (g_active_agent == this)
        g_active_agent = nullptr;
    OllamaClient::cancel_active_requests(OllamaCancelDomain::Chat);
}

void OllamaAgentController::set_model(std::string model)
{
    m_model = std::move(model);
}

void OllamaAgentController::cancel()
{
    m_cancelled      = true;
    m_awaiting_slice = false;
    if (g_active_agent == this)
        g_active_agent = nullptr;
    OllamaClient::cancel_active_requests(OllamaCancelDomain::Chat);
}

void OllamaAgentController::run_goal(const std::string& user_goal, OllamaExecutionPolicy policy, wxWindow* parent,
                                     OllamaAgentCallbacks callbacks, int max_steps)
{
    if (m_running)
        return;
    ensure_agent_event_hook();
    m_running        = true;
    m_cancelled      = false;
    m_awaiting_slice = false;
    m_step           = 0;
    m_max_steps      = max_steps > 0 ? max_steps : ollama_assist_max_steps();
    m_policy         = policy;
    m_parent         = parent;
    m_callbacks      = std::move(callbacks);
    m_user_goal      = user_goal;
    m_step_tool_results.clear();
    m_messages.clear();
    m_pending_assistant_msg.clear();
    m_pending_raw_text.clear();
    m_pending_executed_root = nlohmann::json::object();
    m_mutations_applied     = false;

    const bool ko = wxGetApp().current_language_code().StartsWith("ko");
    refresh_print_intent_and_proposal(ko);
    // Local-only prefetch (settings analysis, mesh health) — no network.
    m_prefetch    = OllamaAssistContextBuilder::prefetch_for_goal(user_goal, ko);
    m_messages.push_back({"system", build_assist_loop_system_prompt(ko)});

    const nlohmann::json plan_hint = OllamaAgentGoalPlanner::build_plan_hint(user_goal, ko);

    // Wiki evidence uses sync HTTP with 20s+ timeouts, so it must never run on
    // the main thread. Approach: fetch it on a detached worker BEFORE the first
    // LLM call and delay that call until the fetch completes — the UI stays
    // responsive and the first LLM turn still sees the evidence. begin_step()
    // re-checks cancellation, so a cancel during the fetch finishes cleanly.
    if (OllamaAssistContextBuilder::wants_wiki_prefetch(user_goal)) {
        if (m_callbacks.on_thinking)
            m_callbacks.on_thinking(ko ? wxString::FromUTF8("관련 도움말을 찾는 중…")
                                       : wxString("Looking up guidance…"));
        const auto alive = m_alive;
        std::thread([this, alive, user_goal, plan_hint, ko]() {
            nlohmann::json wiki = nlohmann::json::array();
            try {
                wiki = OllamaAssistContextBuilder::fetch_wiki_evidence(user_goal, ko);
            } catch (const std::exception& ex) {
                BOOST_LOG_TRIVIAL(warning) << "Ollama agent wiki prefetch failed: " << ex.what();
            } catch (...) {
                BOOST_LOG_TRIVIAL(warning) << "Ollama agent wiki prefetch failed: unknown error";
            }
            if (wxGetApp().is_closing())
                return; // shutting down: never post to a dying main loop
            wxGetApp().CallAfter([this, alive, wiki, user_goal, plan_hint, ko]() {
                if (!alive->load() || wxGetApp().is_closing())
                    return;
                m_prefetch.wiki = wiki;
                m_messages.push_back({"user", OllamaAssistContextBuilder::build_initial_user_block(
                                                  user_goal, plan_hint, m_prefetch, ko)});
                begin_step();
            });
        }).detach();
        return;
    }

    m_messages.push_back(
        {"user", OllamaAssistContextBuilder::build_initial_user_block(user_goal, plan_hint, m_prefetch, ko)});

    begin_step();
}

void OllamaAgentController::refresh_print_intent_and_proposal(bool korean)
{
    // Best-effort deterministic proposal. Never let an exception escape here: this runs on
    // the main thread inside the agent loop, and an uncaught throw would unwind the wx main
    // loop (OnExceptionInMainLoop rethrows) and quit the whole app.
    try {
        auto& session = BambuSmartPrint::PrintIntentSession::instance();
        if (Plater* plater = wxGetApp().plater()) {
            const BambuSmartPrint::PlateContext ctx = PrintPlannerGui::build_plate_context(plater);
            session.merge_turn(m_user_goal, ctx.mesh, ctx.base_config);
            OllamaConfigProposalBuilder::build_from_context(plater, ctx, session.intent(), korean);
        } else {
            session.merge_turn(m_user_goal);
        }
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "Ollama agent proposal build failed: " << ex.what();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "Ollama agent proposal build failed: unknown error";
    }
}

void OllamaAgentController::begin_step()
{
    if (m_cancelled || !m_alive->load()) {
        finish({.cancelled = true});
        return;
    }
    if (m_step >= m_max_steps) {
        OllamaAgentRunResult r;
        r.completed     = false;
        r.blocked       = true;
        r.steps_taken   = m_step;
        r.step_tool_results = m_step_tool_results;
        const bool ko   = wxGetApp().current_language_code().StartsWith("ko");
        r.final_message = ko ? "최대 단계 수에 도달했습니다." : "Reached maximum agent steps.";
        finish(std::move(r));
        return;
    }

    ++m_step;
    // Thinking lines are shown when actions are planned / applied — not generic "Working… (N)".

    trim_assist_messages(m_messages);

    const auto alive = m_alive;
    const std::string model = m_model;
    m_client.chat(model, m_messages,
                  [this, alive](const std::string& text, const std::string& error) {
                      wxGetApp().CallAfter([this, alive, text, error]() {
                          if (!alive->load() || wxGetApp().is_closing())
                              return;
                          on_llm_response(text, error);
                      });
                  });
}

void OllamaAgentController::note_workflow_mutations(const OllamaWorkflowRun& workflow)
{
    for (const auto& r : workflow.results) {
        if (r.success && r.effective_change) {
            m_mutations_applied = true;
            return;
        }
    }
}

OllamaPipelineOptions OllamaAgentController::pipeline_options() const
{
    OllamaPipelineOptions opt;
    opt.apply_mode                = true;
    opt.include_makerworld        = true;
    opt.user_request              = m_user_goal;
    opt.mutations_already_applied = m_mutations_applied;
    return opt;
}

void OllamaAgentController::on_llm_response(const std::string& text, const std::string& error)
{
    if (m_cancelled) {
        finish({.cancelled = true, .steps_taken = m_step, .step_tool_results = m_step_tool_results});
        return;
    }
    if (!error.empty()) {
        OllamaAgentRunResult r;
        r.blocked           = true;
        r.steps_taken       = m_step;
        r.step_tool_results = m_step_tool_results;
        r.final_message     = error;
        finish(std::move(r));
        return;
    }

    m_messages.push_back({"assistant", text});

    nlohmann::json root;
    try {
        root = OllamaActionExecutor::extract_action_json(text);
    } catch (...) {
        root = nlohmann::json::object({{"message", text}});
    }

    reorder_actions_readonly_first(root);

    OllamaPipelineOptions opt = pipeline_options();
    OllamaActionPipeline::process_actions(root, opt);

    const std::string assistant_msg = root.value("message", "");
    if (root_is_done(root) && !OllamaActionWorkflow::has_executable_actions(root)) {
        OllamaAgentRunResult r;
        r.completed         = true;
        r.steps_taken       = m_step;
        r.step_tool_results = m_step_tool_results;
        r.final_message     = assistant_msg.empty() ? text : assistant_msg;
        finish(std::move(r));
        return;
    }

    if (!OllamaActionWorkflow::has_executable_actions(root)) {
        if (m_step < m_max_steps) {
            const bool ko = wxGetApp().current_language_code().StartsWith("ko");
            std::string nudge = ko
                ? "실행 가능한 actions JSON이 필요합니다. get_state로 상태를 확인한 뒤 set_config/geometry/mesh 도구를 호출하세요. "
                  "설명만 하지 말고 JSON actions를 반환하세요.\n\nGoal:\n"
                : "Reply with executable actions JSON. Call get_state first, then set_config/geometry/mesh tools. "
                  "Do not only explain — return JSON actions.\n\nGoal:\n";
            nudge += m_user_goal;
            m_messages.push_back({"user", std::move(nudge)});
            begin_step();
            return;
        }
        OllamaAgentRunResult r;
        r.completed         = true;
        r.steps_taken       = m_step;
        r.step_tool_results = m_step_tool_results;
        r.final_message     = assistant_msg.empty() ? text : assistant_msg;
        finish(std::move(r));
        return;
    }

    AIGuiOrchestrator::instance().on_chat_apply_begin();
    if (m_callbacks.on_thinking) {
        const bool ko = wxGetApp().current_language_code().StartsWith("ko");
        const std::string plan = ollama_thinking_planned_actions(root, ko);
        if (!plan.empty())
            m_callbacks.on_thinking(wxString::FromUTF8(plan));
    }
    const OllamaWorkflowRun workflow = OllamaActionWorkflow::execute_with_policy(root, m_parent, m_policy);
    note_workflow_mutations(workflow);
    const nlohmann::json    tool_results = ollama_tool_results_from_workflow(root, workflow);
    m_step_tool_results.push_back(tool_results);

    bool applied = false;
    for (const auto& r : workflow.results) {
        if (r.effective_change)
            applied = true;
    }
    AIGuiOrchestrator::instance().on_chat_apply_end(applied, root, "ollama_assist_loop");

    schedule_post_apply_verification(root, assistant_msg, text, applied, std::move(workflow));
}

void OllamaAgentController::schedule_post_apply_verification(const nlohmann::json& root,
                                                             const std::string& assistant_msg,
                                                             const std::string& raw_text, bool applied,
                                                             OllamaWorkflowRun workflow)
{
    const auto alive = m_alive;
    wxGetApp().CallAfter([this, alive, root, assistant_msg, raw_text, applied, workflow = std::move(workflow)]() mutable {
        if (!alive->load() || wxGetApp().is_closing() || !m_running || m_cancelled)
            return;
        handle_post_apply_verification(root, assistant_msg, raw_text, applied, workflow);
    });
}

void OllamaAgentController::handle_post_apply_verification(const nlohmann::json& root,
                                                           const std::string& assistant_msg,
                                                           const std::string& raw_text, bool applied,
                                                           const OllamaWorkflowRun& workflow)
{
    OllamaConfigVerifyReport report = OllamaAgentStateService::verify_set_config_actions(root, m_user_goal);
    if (!m_step_tool_results.empty() && report.tool_results.is_array() && !report.tool_results.empty())
        m_step_tool_results.back().insert(m_step_tool_results.back().end(), report.tool_results.begin(),
                                          report.tool_results.end());

    if (handle_verify_report(report, assistant_msg, root, raw_text, applied, workflow))
        return;

    if (workflow.cancelled || workflow.preview_only) {
        OllamaAgentRunResult r;
        r.blocked           = true;
        r.steps_taken       = m_step;
        r.step_tool_results = m_step_tool_results;
        r.final_message     = assistant_msg.empty() ? "Workflow cancelled or preview only." : assistant_msg;
        finish(std::move(r));
        return;
    }

    const nlohmann::json& tool_results =
        m_step_tool_results.empty() ? nlohmann::json::array() : m_step_tool_results.back();
    if (m_callbacks.on_thinking) {
        const bool ko = wxGetApp().current_language_code().StartsWith("ko");
        const std::string summary = ollama_thinking_after_tools(tool_results, ko);
        if (!summary.empty())
            m_callbacks.on_thinking(wxString::FromUTF8(summary));
    }

    if (OllamaAgentGoalPlanner::should_auto_finish_after_apply(m_user_goal, applied, m_step_tool_results)) {
        OllamaAgentRunResult r;
        r.completed         = true;
        r.steps_taken       = m_step;
        r.step_tool_results = m_step_tool_results;
        const bool ko       = wxGetApp().current_language_code().StartsWith("ko");
        r.final_message     = pick_user_facing_message(m_step_tool_results, assistant_msg, ko);
        if (r.final_message.empty())
            r.final_message = applied
                ? (ko ? "요청한 변경을 적용했습니다." : "Applied the requested changes.")
                : (ko ? "적용된 변경이 없습니다." : "Nothing was applied.");
        finish(std::move(r));
        return;
    }

    m_pending_assistant_msg = assistant_msg;
    m_pending_raw_text      = raw_text;
    m_pending_executed_root = root;
    if (update_awaiting_slice_from_results()) {
        g_active_agent = this;
        return;
    }

    proceed_after_tool_execution(root, assistant_msg, raw_text);
}

bool OllamaAgentController::handle_verify_report(const OllamaConfigVerifyReport& report,
                                               const std::string& assistant_msg, const nlohmann::json& root,
                                               const std::string& raw_text, bool /*applied*/,
                                               const OllamaWorkflowRun& /*workflow*/)
{
    if (report.all_ok)
        return false;

    if (m_step < m_max_steps) {
        const bool ko = wxGetApp().current_language_code().StartsWith("ko");
        std::string fb = OllamaAgentStateService::build_verify_retry_nudge(report, ko);
        fb += m_user_goal;
        m_messages.push_back({"user", std::move(fb)});
        begin_step();
        return true;
    }

    OllamaAgentRunResult r;
    r.blocked           = true;
    r.steps_taken       = m_step;
    r.step_tool_results = m_step_tool_results;
    if (!assistant_msg.empty())
        r.final_message = assistant_msg;
    else if (!report.mismatches.empty())
        r.final_message = "Config verification failed: " + report.mismatches.front();
    else
        r.final_message = "Config verification failed.";
    finish(std::move(r));
    (void) root;
    (void) raw_text;
    return true;
}

bool OllamaAgentController::update_awaiting_slice_from_results()
{
    const nlohmann::json state = OllamaAgentStateService::snapshot();
    const bool           plate_sliced =
        state.contains("slice_state") && state["slice_state"].value("current_plate_sliced", false);
    if (plate_sliced) {
        m_awaiting_slice = false;
        return false;
    }

    bool slice_started = false;
    for (const auto& step : m_step_tool_results) {
        if (!step.is_array())
            continue;
        for (const auto& r : step) {
            if (r.is_object() && r.value("tool", "") == "slice" && r.value("ok", false))
                slice_started = true;
        }
    }
    m_awaiting_slice = slice_started;
    return m_awaiting_slice;
}

void OllamaAgentController::handle_agent_event(const OllamaAgentEvent& evt)
{
    if (evt.kind != OllamaAgentEventKind::SliceDone || !m_awaiting_slice)
        return;
    continue_after_slice(evt.payload.value("success", false));
}

void OllamaAgentController::continue_after_slice(bool slice_ok)
{
    if (!m_running || m_cancelled)
        return;

    m_awaiting_slice = false;
    if (g_active_agent == this)
        g_active_agent = nullptr;

    if (!slice_ok) {
        const bool ko = wxGetApp().current_language_code().StartsWith("ko");
        OllamaAgentRunResult r;
        r.blocked           = true;
        r.steps_taken       = m_step;
        r.step_tool_results = m_step_tool_results;
        r.final_message     = ko ? "슬라이싱에 실패했습니다." : "Slicing failed.";
        finish(std::move(r));
        return;
    }

    const bool ko = wxGetApp().current_language_code().StartsWith("ko");
    m_messages.push_back({"user", ko ? std::string("슬라이싱이 완료되었습니다. get_state로 slice_state를 확인한 뒤 다음 단계를 진행하세요.\n\nGoal:\n") + m_user_goal
                                   : std::string("Slicing finished. Call get_state to verify slice_state, then continue.\n\nGoal:\n") + m_user_goal});
    begin_step();
}

void OllamaAgentController::proceed_after_tool_execution(const nlohmann::json& executed_root,
                                                         const std::string& assistant_msg, const std::string& raw_text)
{
    if (root_is_done(executed_root)) {
        OllamaAgentRunResult r;
        r.completed         = true;
        r.steps_taken       = m_step;
        r.step_tool_results = m_step_tool_results;
        const bool ko       = wxGetApp().current_language_code().StartsWith("ko");
        r.final_message     = pick_user_facing_message(m_step_tool_results, assistant_msg, ko);
        if (r.final_message.empty()) {
            if (!m_step_tool_results.empty())
                r.final_message = ko ? "적용된 변경이 없습니다." : "Nothing was applied.";
            else
                r.final_message = assistant_msg.empty() ? raw_text : assistant_msg;
        }
        finish(std::move(r));
        return;
    }

    const GeometryGoalNeeds geo_needs = OllamaAgentGoalPlanner::parse_geometry_needs(m_user_goal);
    if (geo_needs.any() && OllamaAgentGoalPlanner::geometry_needs_met(geo_needs, m_step_tool_results)) {
        const bool ko = wxGetApp().current_language_code().StartsWith("ko");
        OllamaAgentRunResult r;
        r.completed         = true;
        r.steps_taken       = m_step;
        r.step_tool_results = m_step_tool_results;
        r.final_message     = pick_user_facing_message(m_step_tool_results, assistant_msg, ko);
        if (r.final_message.empty())
            r.final_message = ko ? "요청한 모델 변경을 적용했습니다." : "Applied the requested model changes.";
        finish(std::move(r));
        return;
    }

    const nlohmann::json& tool_results =
        m_step_tool_results.empty() ? nlohmann::json::array() : m_step_tool_results.back();
    std::string           feedback     = std::string("Tool results (JSON):\n") + tool_results.dump(2);
    for (const auto& r : tool_results) {
        if (!r.is_object() || r.value("ok", true))
            continue;
        if (r.contains("blocker") && r["blocker"].is_string())
            feedback += std::string("\nBlocker: ") + r["blocker"].get<std::string>();
        if (r.value("tool", "") == "verify_config" && r.contains("data") && r["data"].is_object()
            && r["data"].contains("config_digest") && r["data"]["config_digest"].is_object()
            && !r["data"]["config_digest"].empty()) {
            feedback += std::string("\n\nConfig digest (JSON):\n") + r["data"]["config_digest"].dump(2);
        }
    }
    if (!assistant_msg.empty())
        feedback += std::string("\n\nAssistant note:\n") + assistant_msg;
    feedback += std::string("\n\nContinue toward goal (done:false until complete):\n") + m_user_goal;
    m_messages.push_back({"user", std::move(feedback)});

    // Applied actions may have changed the baseline config; re-derive the proposal so the
    // next step's context reflects the updated geometry/config state.
    const bool ko = wxGetApp().current_language_code().StartsWith("ko");
    refresh_print_intent_and_proposal(ko);

    begin_step();
}

void OllamaAgentController::finish(OllamaAgentRunResult result)
{
    m_running        = false;
    m_awaiting_slice = false;
    if (g_active_agent == this)
        g_active_agent = nullptr;
    if (m_callbacks.on_finished)
        m_callbacks.on_finished(result);
}

}} // namespace

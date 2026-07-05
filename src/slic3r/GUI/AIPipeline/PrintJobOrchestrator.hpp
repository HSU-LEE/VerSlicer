#ifndef slic3r_AIPipeline_PrintJobOrchestrator_hpp_
#define slic3r_AIPipeline_PrintJobOrchestrator_hpp_

#include "PrintJob.hpp"
#include "PrintJobState.hpp"
#include "PrintJobStepExecutors.hpp" // ImportResult

#include "../OllamaAssistant/OllamaAgentEventBus.hpp" // OllamaSubscriptionId, OllamaAgentEvent

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

class wxTimer;
class wxWindow;

namespace Slic3r { namespace GUI { namespace AIPipeline {

/** Feature flag: ollama/print_job_orchestrator (default ON; set to 0/false/no/off to disable). */
bool print_job_orchestrator_enabled();

/**
 * RAII holder for a single event-bus subscription. Unsubscribes on destruction
 * or reset so a job-scoped SliceDone handler can never outlive its job.
 */
class ScopedEventSubscription
{
public:
    ScopedEventSubscription() = default;
    explicit ScopedEventSubscription(OllamaSubscriptionId id) : m_id(id) {}
    ~ScopedEventSubscription() { reset(); }

    ScopedEventSubscription(ScopedEventSubscription&& other) noexcept : m_id(other.m_id) { other.m_id = 0; }
    ScopedEventSubscription& operator=(ScopedEventSubscription&& other) noexcept
    {
        if (this != &other) {
            reset();
            m_id       = other.m_id;
            other.m_id = 0;
        }
        return *this;
    }
    ScopedEventSubscription(const ScopedEventSubscription&)            = delete;
    ScopedEventSubscription& operator=(const ScopedEventSubscription&) = delete;

    void reset();
    bool active() const { return m_id != 0; }

private:
    OllamaSubscriptionId m_id{ 0 };
};

/**
 * Phase 3 GUI state machine tying the whole AI print pipeline into one flow:
 *   intent -> [clarify] -> search -> select -> import -> mesh health ->
 *   [repair] -> geometry -> auto-config -> slice -> estimate -> [send].
 *
 * Threading / lifetime contract:
 *  - Every method must be called on the wx main thread.
 *  - The orchestrator is owned by OllamaChatPanel via std::unique_ptr. Because it
 *    is not shared_ptr-managed, weak_from_this() is unavailable; instead every
 *    async continuation captures a weak handle to an internal liveness token
 *    (m_alive) plus the job's shared_ptr + job_id and re-validates all three
 *    before touching state. This is the functional equivalent of the requested
 *    weak_from_this() + job_id triple guard.
 *  - Only finish_success/finish_error/finish_cancelled clear the busy state,
 *    unsubscribe the event bus, and fire on_finished — never mid-pipeline.
 */
class PrintJobOrchestrator
{
public:
    PrintJobOrchestrator();
    ~PrintJobOrchestrator();

    PrintJobOrchestrator(const PrintJobOrchestrator&)            = delete;
    PrintJobOrchestrator& operator=(const PrintJobOrchestrator&) = delete;

    // --- Public API (main thread only) ---
    /** Begin a new end-to-end job. Returns false when one is already active. */
    bool start(const std::string& user_utterance, wxWindow* parent, bool apply_mode, PrintJobUiCallbacks ui);

    /** Feed a chat reply while Clarifying (merges intent, re-evaluates). */
    void on_user_reply(const std::string& text);

    /** Select a search candidate by index while in CandidateSelect. */
    void select_candidate(int index);

    /** Confirm sending to the printer when ReadyToPrint (manual mode). */
    void confirm_print();

    /** Cancel the active job (idempotent). */
    void cancel();

    /** Called from Plater's import-done hook when this job owns the import. */
    void on_plater_import_done(bool ok, const std::string& detail);

    bool          is_active() const;
    PrintJobState state() const;
    std::uint64_t active_job_id() const;

    // --- Static integration hooks for shared call sites ---
    /** True while some orchestrator instance has an import in flight. */
    static bool has_active_import();
    /** Route a Plater import-done to the owning orchestrator (main thread). */
    static void dispatch_import_done(bool ok, const std::string& detail);
    /** job_id (as string) of the job currently slicing, or "" (for SliceDone payload). */
    static std::string current_slicing_job_id();

private:
    // --- State machine core ---
    void transition_to(PrintJobState next);
    void enter_state();

    void step_intent();
    void step_clarify();
    void step_search();
    void step_candidate_select();
    void step_import();
    // Import fallback chain: resolve one candidate on a worker thread; when it
    // has no usable download link, advance to the next untried candidate.
    void resolve_import_for_candidate(int candidate_index);
    void on_import_resolved(const PrintJobStepExecutors::ImportResult& res);
    int  next_import_fallback_index() const;
    void step_mesh_health();
    void step_repair();
    void step_geometry();
    void step_autoconfig();
    void step_slice();
    void step_estimate();
    void step_ready();
    void step_send();

    void on_agent_event(const OllamaAgentEvent& evt);

    void finish_success();
    void finish_error(const std::string& code, const std::string& message);
    void finish_cancelled();
    void finish_common();

    // --- Helpers ---
    void     set_busy(bool busy, const wxString& status = {});
    void     notify_step(); // fire ui.on_step for the current state (when set)
    void     chat(const wxString& message);
    void     chat_question(const wxString& question); // emphasized bubble when supported
    void     chat_error(const wxString& message);     // warning-tinted bubble when supported
    bool     korean() const;
    // Validate that `job` is still the current, non-cancelled job with matching id.
    bool     is_current(const std::shared_ptr<PrintJob>& job, std::uint64_t jid) const;

    void register_import_owner();
    void clear_import_owner();
    void set_slicing_job();
    void clear_slicing_job();

    // Per-state timeout watchdog for externally-completed waiting states
    // (Importing / Slicing). Fails the job with `code`/`message` if it is still
    // in `guarded_state` when the deadline elapses.
    void arm_watchdog(PrintJobState guarded_state, int timeout_ms, const char* code, const wxString& message);
    void disarm_watchdog();

    // --- Members ---
    std::shared_ptr<PrintJob>                 m_job;
    std::shared_ptr<std::atomic<bool>>        m_alive;      // liveness token (see class doc)
    ScopedEventSubscription                   m_slice_sub;  // job-scoped SliceDone subscription
    std::unique_ptr<wxTimer>                  m_watchdog;   // per-state timeout (import/slice)
    PrintJobState                             m_last_terminal{ PrintJobState::Idle };
    bool                                      m_search_begun{ false }; // matched on_makerworld_search_begin/end
    std::uint64_t                             m_next_job_id{ 1 };
};

}}} // namespace Slic3r::GUI::AIPipeline

#endif

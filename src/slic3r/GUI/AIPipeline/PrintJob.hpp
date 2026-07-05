#ifndef slic3r_AIPipeline_PrintJob_hpp_
#define slic3r_AIPipeline_PrintJob_hpp_

#include "PrintJobState.hpp"

#include "../ModelSearch/ModelSearchTypes.hpp"

#include "libslic3r/BambuSmartPrint/PrintIntent.hpp"
#include "libslic3r/BambuSmartPrint/PrintIntentClarifier.hpp"
#include "libslic3r/BambuSmartPrint/AutoConfigEngine.hpp"          // GeometryAssessment, ConfigProposal
#include "libslic3r/BambuSmartPrint/PrintPlannerTypes.hpp"         // PlateContext
#include "libslic3r/BambuSmartPrint/PrintEstimateSummary.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class wxWindow;
class wxString;

namespace Slic3r { namespace GUI { namespace AIPipeline {

/**
 * UI seam for the orchestrator. All callbacks are invoked on the wx main thread.
 * Mirrors MakerWorldFlowUiCallbacks (see PrintJobUiAdapter) so the legacy chat
 * plumbing can be reused verbatim.
 */
struct PrintJobUiCallbacks
{
    // Append an assistant-authored message to the chat log.
    std::function<void(const wxString& assistant_message)> append_chat;
    // Optional: a clarifying question that needs a user reply (rendered with
    // emphasis). Falls back to append_chat when unset.
    std::function<void(const wxString& question)>           append_question;
    // Optional: a terminal error message (rendered with a warning tint).
    // Falls back to append_chat when unset.
    std::function<void(const wxString& error_message)>      append_error;
    // Toggle the busy/working state and optionally set a status string.
    std::function<void(bool busy, const wxString& status)>  set_busy;
    // Optional: fired on every state transition (including terminal states) so
    // the UI can render a compact stepper/progress strip. Legacy callers that
    // leave it unset keep the old chat-only behavior.
    std::function<void(PrintJobState state, const wxString& detail)> on_step;
    // Fired exactly once when the job leaves the machine (Done/Failed/Cancelled).
    std::function<void()>                                   on_finished;
};

/** Structured error captured on the Failed path (kept for UI + telemetry). */
struct PrintJobError
{
    PrintJobState failed_state{ PrintJobState::Idle }; // state that produced the error
    std::string   code;                                // short machine tag
    std::string   message;                             // human-readable (English)
};

/**
 * Cross-step data bag for one end-to-end AI print job. Owned by the
 * PrintJobOrchestrator via std::shared_ptr; async callbacks hold weak refs and
 * re-validate identity via job_id before touching it.
 *
 * All fields use the REAL foundation types so no data is lost across steps.
 */
struct PrintJob
{
    // --- Identity / lifecycle ---
    std::uint64_t     job_id{ 0 };
    std::atomic<bool> cancelled{ false };
    PrintJobState     state{ PrintJobState::Idle };

    // --- Inputs ---
    std::string       user_utterance;      // original request that started the job
    bool              apply_mode{ true };  // false = question-only (no plate mutation)
    wxWindow*         parent{ nullptr };    // dialog parent (main-thread only)

    // --- Intent / clarification (Phase 2) ---
    BambuSmartPrint::PrintIntent                          intent;
    std::optional<BambuSmartPrint::ClarifyingQuestion>    pending_question;
    int                                                   clarify_rounds{ 0 };

    // --- Search / selection (Foundation A) ---
    std::string                          search_query;
    std::vector<ModelCandidate>          candidates;
    int                                  selected_index{ -1 };
    ModelCandidate                       selected_candidate;
    // Import fallback chain: candidate indices already attempted (the selected
    // one first, then remaining candidates in rank order).
    std::vector<int>                     import_tried;

    // --- Import / slice-send behavior ---
    bool              auto_slice_and_send{ false }; // find_and_print path
    int               plate_index{ 0 };

    // --- Mesh health / repair (Foundation B) ---
    // Copy-init: brace-init json{ json::object() } would create [{}] (an array).
    nlohmann::json    mesh_health = nlohmann::json::object();
    bool              mesh_needs_repair{ false };
    int               repair_attempts{ 0 };

    // --- Geometry / config (Foundation B + Phase 2) ---
    BambuSmartPrint::PlateContext        plate_context;
    BambuSmartPrint::GeometryAssessment  assessment;
    BambuSmartPrint::ConfigProposal      proposal;

    // --- Estimate / result (Foundation B) ---
    BambuSmartPrint::PrintEstimateSummary estimate;
    bool              slice_succeeded{ false };

    // --- UI + terminal error ---
    PrintJobUiCallbacks           ui;
    std::optional<PrintJobError>  error;

    PrintJob() = default;
    PrintJob(const PrintJob&)            = delete; // atomic member; never copy
    PrintJob& operator=(const PrintJob&) = delete;

    bool is_cancelled() const { return cancelled.load(); }
};

}}} // namespace Slic3r::GUI::AIPipeline

#endif

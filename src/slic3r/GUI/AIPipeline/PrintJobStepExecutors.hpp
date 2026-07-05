#ifndef slic3r_AIPipeline_PrintJobStepExecutors_hpp_
#define slic3r_AIPipeline_PrintJobStepExecutors_hpp_

#include "../ModelSearch/ModelSearchTypes.hpp"

#include "libslic3r/BambuSmartPrint/AutoConfigEngine.hpp"      // GeometryAssessment, ConfigProposal
#include "libslic3r/BambuSmartPrint/PrintIntent.hpp"
#include "libslic3r/BambuSmartPrint/PrintPlannerTypes.hpp"     // PlateContext
#include "libslic3r/BambuSmartPrint/PrintEstimateSummary.hpp"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

class wxWindow;

namespace Slic3r { namespace GUI { namespace AIPipeline {

/**
 * Thin, side-effecting wrappers around the two foundations and the Phase 1/2
 * GUI bridges. Each function does exactly one pipeline step and performs no
 * state-machine logic; the orchestrator owns sequencing, guarding and threading.
 *
 * Unless documented as async, every function must be called on the wx main
 * thread (they touch Plater / preset bundle / wx dialogs).
 */
class PrintJobStepExecutors
{
public:
    // --- Search (Foundation A) ---
    // ASYNC: builds context on the calling (main) thread, fans out on a worker,
    // and delivers `callback` on the main thread (see ModelSearchService).
    static void execute_search_async(const std::string& user_text, ModelSearchResultCallback callback);

    // Modal top-N offer dialog. Returns the selected index into `candidates`
    // (>= 0) or -1 when the user cancelled / no selection.
    static int show_print_offer_dialog(wxWindow* parent, const std::vector<ModelCandidate>& candidates);

    struct ImportResult {
        bool        ok{ false };
        bool        needs_login{ false };
        // No download link could be resolved for THIS candidate; the caller may
        // fall back to the next search candidate.
        bool        no_download_link{ false };
        std::string error;
        std::string detail_page_url;
        std::string download_info; // resolved Plater download payload (on ok)
    };

    // Resolve the download payload for `candidate`. Blocking (sync HTTP);
    // call on a worker thread.
    static ImportResult resolve_import_blocking(const ModelCandidate& candidate);

    // ASYNC: resolves the download payload on a detached worker thread and
    // delivers the result on the wx main thread. The callback is dropped
    // (never invoked) when the app is shutting down.
    static void resolve_import_async(const ModelCandidate& candidate, std::function<void(ImportResult)> callback);

    // Kick off the Plater download for a resolved payload. Main thread only;
    // the caller must wait for Plater's import-done callback afterwards.
    static void begin_download(const std::string& download_info);

    // --- Mesh health / repair (Foundation B) ---
    struct MeshHealthResult {
        bool           valid{ false };
        bool           needs_repair{ false };
        // Copy-init: brace-init json{ json::object() } would create [{}] (an array).
        nlohmann::json json = nlohmann::json::object();
    };
    static MeshHealthResult assess_mesh_health();

    // Repair every volume flagged in `mesh_health`. Returns repaired volume count.
    static int attempt_repair(const nlohmann::json& mesh_health);

    // --- Geometry / config (Foundation B + Phase 2) ---
    struct GeometryResult {
        bool                                 ok{ false };
        BambuSmartPrint::PlateContext        ctx;
        BambuSmartPrint::GeometryAssessment  assessment;
    };
    static GeometryResult build_geometry_context();

    // Build + cache the deterministic proposal (OllamaConfigProposalBuilder).
    static BambuSmartPrint::ConfigProposal build_and_cache_proposal(const BambuSmartPrint::PlateContext& ctx,
                                                                     const BambuSmartPrint::PrintIntent& intent,
                                                                     bool korean);

    // Apply the proposal's set_config actions to the live preset. Returns the
    // number of set_config actions that were dispatched.
    static int apply_config_proposal(const BambuSmartPrint::ConfigProposal& proposal);

    // --- Slice / estimate / send (Phase 1) ---
    // Posts EVT_GLTOOLBAR_SLICE_PLATE. Returns false when no plater is available.
    static bool trigger_slice();

    // Collect the estimate from the current plate's slice statistics.
    static BambuSmartPrint::PrintEstimateSummary collect_estimate(
        const BambuSmartPrint::GeometryAssessment& assessment);

    struct SendResult {
        bool        handled{ false };
        bool        setup_blocked{ false };
        std::string blocked_message;
    };
    // Dispatch the "send_print" coach action (OllamaUserFlow).
    static SendResult trigger_send();
};

}}} // namespace Slic3r::GUI::AIPipeline

#endif

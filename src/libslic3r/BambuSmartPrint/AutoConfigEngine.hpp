#ifndef slic3r_AutoConfigEngine_hpp_
#define slic3r_AutoConfigEngine_hpp_

#include "PrintPlannerTypes.hpp"
#include "PrintIntent.hpp"
#include "PrintIntentClarifier.hpp"
#include "StabilityAnalyzer.hpp"
#include "OrientationOptimizer.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace Slic3r {
namespace BambuSmartPrint {

/** Bundled geometry-derived analyses used to drive configuration derivation. */
struct GeometryAssessment {
    ModelAnalysis     mesh;
    SliceAnalysis     slice;
    ReadinessReport   readiness;
    SuccessPrediction prediction;
    bool              has_slice{ false };
    std::string       orientation_hint; // compatibility string (mirrors orientation.summary_en)

    // Phase 5 additive members (other phases depend on GeometryAssessment: additive only).
    StabilityMetrics    stability;   // stability.computed == false when not evaluated
    OrientationResult   orientation; // orientation.computed == false when not evaluated
};

/** A concrete, reviewable configuration proposal derived from intent + geometry. */
struct ConfigProposal {
    DynamicPrintConfig         base_config;
    DynamicPrintConfig         proposed_config;
    DynamicPrintConfig         delta;
    std::vector<SettingChange> changes;
    std::vector<SettingChange> blocked_changes;
    PrintExplanation           explanation;
    std::vector<PrintRisk>     risks;
    // Copy-init: brace-init json{ json::array() } would create [[]] (a nested array).
    nlohmann::json             set_config_actions = nlohmann::json::array();
    nlohmann::json             geometry_actions   = nlohmann::json::array();
    float                      success_estimate{ 0.f };
    bool                       has_blocking_missing_slots{ false };
    std::optional<ClarifyingQuestion> clarifying_question;
    std::string                proposal_id;
};

/**
 * Headless auto-configuration engine. Derives a ConfigProposal deterministically from a
 * PrintIntent and the plate's geometry/analysis context, reusing PrintPlanner helpers.
 */
class AutoConfigEngine
{
public:
    static ConfigProposal propose(const PrintIntent& intent, const PlateContext& ctx);

    /** Compact JSON for LLM context injection (REFINE_ONLY policy). */
    static nlohmann::json proposal_to_context_json(const ConfigProposal& proposal, bool korean);

    /** Merge LLM assistant actions/message into an existing proposal. */
    static ConfigProposal merge_llm_actions(const ConfigProposal& base, const nlohmann::json& llm_root);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

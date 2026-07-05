#ifndef slic3r_PrintPlanner_hpp_
#define slic3r_PrintPlanner_hpp_

#include "PrintPlannerTypes.hpp"

namespace Slic3r {
namespace BambuSmartPrint {

class PrintGoalSession;

class PrintPlanner
{
public:
    static PrintGoal parse_goal(const std::string& user_text);

    /** Rule-based plan (no LLM). Uses AutoSettingsEngine output in context + goal trade-offs. */
    static PrintPlan plan_without_llm(const PlateContext& ctx, const PrintGoal& goal);

    /** Merge LLM assistant JSON into a plan; falls back to rule plan on empty actions. */
    static PrintPlan plan_from_assistant(const PlateContext& ctx, const PrintGoal& goal,
                                         const nlohmann::json& assistant_root);

    /** Incremental replan when session goal unchanged and mesh stable. */
    static PrintPlan replan(const PlateContext& ctx, PrintGoalSession& session);

    /** Apply goal-driven config patches in place (shared with AutoConfigEngine). */
    static void apply_goal_patches(DynamicPrintConfig& cfg, const PrintGoal& goal, const ModelAnalysis& mesh);

    static void dedupe_actions(nlohmann::json& root);
    static std::vector<PrintRisk> build_risks(const PlateContext& ctx);
    static PrintExplanation build_explanation(const PrintPlan& plan);
    static std::string build_tradeoff_note(const PrintGoal& goal);

    static nlohmann::json config_delta_to_actions(const DynamicPrintConfig& base,
                                                  const DynamicPrintConfig& proposed,
                                                  const std::vector<SettingChange>& changes);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

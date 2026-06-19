#ifndef slic3r_PrintPlannerTypes_hpp_
#define slic3r_PrintPlannerTypes_hpp_

#include "BambuSmartPrintTypes.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r {
namespace BambuSmartPrint {

enum class PrintGoalIntent : int {
    Unknown  = 0,
    Cosmetic,
    Strong,
    Fast,
    Outdoor,
    Adhesion,
    Overhang,
};

enum class PrintRiskKind : int {
    Unknown = 0,
    Overhang,
    Adhesion,
    TallNarrow,
    SmallFootprint,
    FilamentMismatch,
    UnsupportedSlice,
    General,
};

enum class RecommendedAction : int {
    None = 0,
    EnableSupport,
    AddBrim,
    Rotate,
    Arrange,
    IncreaseInfill,
    ThickenWalls,
    SlowFirstLayer,
};

enum class ApplyPolicy : int {
    SilentSafe = 0,
    Notify,
    ReviewDialog,
};

struct PrintGoal {
    std::vector<PrintGoalIntent> intents;
    std::string                  user_text;
    float                        weight_cosmetic{ 0.f };
    float                        weight_strong{ 0.f };
    float                        weight_fast{ 0.f };
    float                        weight_outdoor{ 0.f };

    bool has_intent(PrintGoalIntent i) const;
    bool empty() const;
};

struct PrintRisk {
    PrintRiskKind       kind{ PrintRiskKind::Unknown };
    std::string         label;
    std::string         detail;
    RiskSeverity        severity{ RiskSeverity::Info };
    RecommendedAction   recommended{ RecommendedAction::None };
    std::string         recommended_action_text;
    nlohmann::json      action_json;
};

struct PrintExplanation {
    std::string              summary;
    std::vector<std::string> change_reasons;
    std::vector<std::string> expected_effects;
    std::string              tradeoff_note;
};

struct PlateContext {
    ModelAnalysis       mesh;
    ReadinessReport     readiness;
    SuccessPrediction   prediction;
    DynamicPrintConfig  base_config;
    DynamicPrintConfig  proposed_config;
    AutoSettingsResult  auto_result;
    SliceAnalysis       slice;
    bool                has_slice{ false };
    bool                has_model{ false };
    std::string         printer_id;
    std::string         filament_name;
    size_t              change_count{ 0 };
};

struct PrintPlan {
    PrintGoal                  goal;
    ReadinessReport            readiness;
    SuccessPrediction          prediction;
    ModelAnalysis              mesh;
    nlohmann::json             root;
    PrintExplanation           explanation;
    std::vector<PrintRisk>     risks;
    float                      success_estimate{ 0.f };
    ApplyPolicy                apply_policy{ ApplyPolicy::Notify };
    AutoSettingsResult         auto_result;
    DynamicPrintConfig         proposed_config;
    DynamicPrintConfig         base_config;
    size_t                     change_count{ 0 };
    bool                       from_llm{ false };

    bool has_actions() const;
    std::string message() const;
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

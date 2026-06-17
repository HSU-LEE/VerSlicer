#ifndef slic3r_AICoachTypes_hpp_
#define slic3r_AICoachTypes_hpp_

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

enum class AICoachTriggerId {
    None = 0,
    ModelTallBrim,
    SliceDoneSend,
    OverhangSupport,
    BedArrange,
    AdhesionRisk,
    PrintFailure,
    SendGateBlocked,
    PrintSuccessFinishing,
    PrintMonitor,
    FailureDoctor,
    PersonalTrainer,
    AppliedUndo,
};

enum class AICoachImportance {
    Low = 0,
    Normal,
    Critical,
};

enum class AICoachButtonRole {
    Primary,
    Secondary,
};

enum class AICoachCardKind {
    Standard = 0,
    ExplainableRecommendation,
    PrintFinishing,
    AppliedUndo,
};

struct AICoachButton {
    std::string           label;
    AICoachButtonRole     role{ AICoachButtonRole::Primary };
    /** "apply_actions", "dismiss", "undo_apply", "feedback_good", ... */
    std::string           action_id;
    nlohmann::json        action_payload;
    bool                  enabled{ true };
};

/** Single setting line for explainable AI (before → after). */
struct AICoachSettingLine {
    std::string label;
    std::string before_value;
    std::string after_value;
};

struct AICoachTrustBrief {
    std::vector<AICoachSettingLine> changes;
    std::string                     reason;
    std::string                     success_effect;
    std::string                     time_effect;
    std::string                     filament_effect;
    int                             confidence_percent{ 0 };
    bool                            experimental{ false };
};

struct AICoachFinishingStep {
    std::string title;
    std::string duration_hint;
    std::string difficulty;
};

struct AICoachFinishingBrief {
    std::vector<std::string>      summary_lines;
    std::vector<std::string>      checklist;
    std::vector<AICoachFinishingStep> steps;
    std::string                   history_model;
    std::string                   history_time;
    std::string                   history_filament;
};

struct AICoachCard {
    AICoachTriggerId              trigger{ AICoachTriggerId::None };
    AICoachImportance             importance{ AICoachImportance::Normal };
    AICoachCardKind               kind{ AICoachCardKind::Standard };
    std::string                   title;
    std::string                   body;
    std::vector<std::string>      bullets;
    std::vector<std::string>      sections; // titled blocks (trust / finishing subsections)
    AICoachTrustBrief             trust;
    AICoachFinishingBrief         finishing;
    std::vector<AICoachButton>    buttons;
    nlohmann::json                apply_root; // Ollama-style { message, actions }
    int                           auto_dismiss_ms{ 10000 };
};

}} // namespace

#endif

#ifndef slic3r_PrintIntent_hpp_
#define slic3r_PrintIntent_hpp_

#include "PrintPlannerTypes.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r {
namespace BambuSmartPrint {

enum class PrintIntentPriority : int {
    Unknown = 0,
    Strength,
    Speed,
    Quality,
    Balanced,
};

enum class PrintIntentSlot : int {
    ObjectDescription = 0,
    TargetSize,
    Material,
    Priority,
    Printer,
    SymptomIntents,
};

enum class PrintIntentSource : int {
    Unknown = 0,
    DeterministicParser,
    Session,
    LlmExtract,
    GeometryInference,
    ActivePreset,
};

struct PrintIntentTargetSize {
    bool        has_value{ false };
    double      scale_factor{ 1.0 };
    double      dim_x_mm{ 0.0 };
    double      dim_y_mm{ 0.0 };
    double      dim_z_mm{ 0.0 };
    std::string raw_text;
};

/**
 * Slot model unifying deterministic (PrintGoalParser) and LLM-extracted print intent.
 * Headless: no GUI/wx dependencies.
 */
struct PrintIntent {
    std::string           object_description;
    PrintIntentTargetSize target_size;
    std::string           material;
    PrintIntentPriority   priority{ PrintIntentPriority::Unknown };
    std::string           printer_model;
    std::string           printer_id;
    PrintGoal             symptom_goal;
    float                 confidence{ 0.f };
    std::vector<PrintIntentSlot> missing_slots;
    std::vector<PrintIntentSlot> blocking_slots;
    // NOTE: copy-init, not brace-init: json j{ json::object() } would create [{}] (an array).
    nlohmann::json        slot_sources = nlohmann::json::object();

    bool has_symptom(PrintGoalIntent i) const;
    bool is_print_quality_request() const;

    bool has_missing_slot(PrintIntentSlot slot) const;
    bool has_blocking_slot(PrintIntentSlot slot) const;

    nlohmann::json     to_json() const;
    static PrintIntent from_json(const nlohmann::json& j);
};

/** Enum <-> string helpers, exposed for reuse by extractor/clarifier/context builders. */
const char*         to_string(PrintIntentPriority p);
PrintIntentPriority print_intent_priority_from_string(const std::string& s);
const char*         to_string(PrintIntentSlot s);
const char*         to_string(PrintIntentSource s);

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

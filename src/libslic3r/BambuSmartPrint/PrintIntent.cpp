#include "PrintIntent.hpp"

#include <algorithm>

namespace Slic3r {
namespace BambuSmartPrint {

namespace {

const char* print_goal_intent_to_string(PrintGoalIntent i)
{
    switch (i) {
    case PrintGoalIntent::Cosmetic: return "cosmetic";
    case PrintGoalIntent::Strong:   return "strong";
    case PrintGoalIntent::Fast:     return "fast";
    case PrintGoalIntent::Outdoor:  return "outdoor";
    case PrintGoalIntent::Adhesion: return "adhesion";
    case PrintGoalIntent::Overhang: return "overhang";
    case PrintGoalIntent::Unknown:
    default:                        return "unknown";
    }
}

PrintGoalIntent print_goal_intent_from_string(const std::string& s)
{
    if (s == "cosmetic") return PrintGoalIntent::Cosmetic;
    if (s == "strong")   return PrintGoalIntent::Strong;
    if (s == "fast")     return PrintGoalIntent::Fast;
    if (s == "outdoor")  return PrintGoalIntent::Outdoor;
    if (s == "adhesion") return PrintGoalIntent::Adhesion;
    if (s == "overhang") return PrintGoalIntent::Overhang;
    return PrintGoalIntent::Unknown;
}

nlohmann::json print_goal_to_json(const PrintGoal& goal)
{
    nlohmann::json intents = nlohmann::json::array();
    for (PrintGoalIntent i : goal.intents)
        intents.push_back(print_goal_intent_to_string(i));
    return nlohmann::json{
        {"intents", std::move(intents)},
        {"user_text", goal.user_text},
        {"weight_cosmetic", goal.weight_cosmetic},
        {"weight_strong", goal.weight_strong},
        {"weight_fast", goal.weight_fast},
        {"weight_outdoor", goal.weight_outdoor},
    };
}

PrintGoal print_goal_from_json(const nlohmann::json& j)
{
    PrintGoal goal;
    if (!j.is_object())
        return goal;
    if (j.contains("intents") && j["intents"].is_array()) {
        for (const auto& e : j["intents"]) {
            if (!e.is_string())
                continue;
            const PrintGoalIntent i = print_goal_intent_from_string(e.get<std::string>());
            if (std::find(goal.intents.begin(), goal.intents.end(), i) == goal.intents.end())
                goal.intents.push_back(i);
        }
    }
    goal.user_text       = j.value("user_text", std::string{});
    goal.weight_cosmetic = j.value("weight_cosmetic", 0.f);
    goal.weight_strong   = j.value("weight_strong", 0.f);
    goal.weight_fast     = j.value("weight_fast", 0.f);
    goal.weight_outdoor  = j.value("weight_outdoor", 0.f);
    return goal;
}

nlohmann::json target_size_to_json(const PrintIntentTargetSize& ts)
{
    return nlohmann::json{
        {"has_value", ts.has_value},
        {"scale_factor", ts.scale_factor},
        {"dim_x_mm", ts.dim_x_mm},
        {"dim_y_mm", ts.dim_y_mm},
        {"dim_z_mm", ts.dim_z_mm},
        {"raw_text", ts.raw_text},
    };
}

PrintIntentTargetSize target_size_from_json(const nlohmann::json& j)
{
    PrintIntentTargetSize ts;
    if (!j.is_object())
        return ts;
    ts.has_value    = j.value("has_value", false);
    ts.scale_factor = j.value("scale_factor", 1.0);
    ts.dim_x_mm     = j.value("dim_x_mm", 0.0);
    ts.dim_y_mm     = j.value("dim_y_mm", 0.0);
    ts.dim_z_mm     = j.value("dim_z_mm", 0.0);
    ts.raw_text     = j.value("raw_text", std::string{});
    return ts;
}

nlohmann::json slots_to_json(const std::vector<PrintIntentSlot>& slots)
{
    nlohmann::json arr = nlohmann::json::array();
    for (PrintIntentSlot s : slots)
        arr.push_back(to_string(s));
    return arr;
}

PrintIntentSlot slot_from_string(const std::string& s)
{
    if (s == "object_description") return PrintIntentSlot::ObjectDescription;
    if (s == "target_size")        return PrintIntentSlot::TargetSize;
    if (s == "material")           return PrintIntentSlot::Material;
    if (s == "priority")           return PrintIntentSlot::Priority;
    if (s == "printer")            return PrintIntentSlot::Printer;
    if (s == "symptom_intents")    return PrintIntentSlot::SymptomIntents;
    return PrintIntentSlot::ObjectDescription;
}

std::vector<PrintIntentSlot> slots_from_json(const nlohmann::json& j)
{
    std::vector<PrintIntentSlot> out;
    if (!j.is_array())
        return out;
    for (const auto& e : j) {
        if (e.is_string())
            out.push_back(slot_from_string(e.get<std::string>()));
    }
    return out;
}

} // namespace

const char* to_string(PrintIntentPriority p)
{
    switch (p) {
    case PrintIntentPriority::Strength: return "strength";
    case PrintIntentPriority::Speed:    return "speed";
    case PrintIntentPriority::Quality:  return "quality";
    case PrintIntentPriority::Balanced: return "balanced";
    case PrintIntentPriority::Unknown:
    default:                            return "unknown";
    }
}

PrintIntentPriority print_intent_priority_from_string(const std::string& s)
{
    if (s == "strength") return PrintIntentPriority::Strength;
    if (s == "speed")    return PrintIntentPriority::Speed;
    if (s == "quality")  return PrintIntentPriority::Quality;
    if (s == "balanced") return PrintIntentPriority::Balanced;
    return PrintIntentPriority::Unknown;
}

const char* to_string(PrintIntentSlot s)
{
    switch (s) {
    case PrintIntentSlot::ObjectDescription: return "object_description";
    case PrintIntentSlot::TargetSize:        return "target_size";
    case PrintIntentSlot::Material:          return "material";
    case PrintIntentSlot::Priority:          return "priority";
    case PrintIntentSlot::Printer:           return "printer";
    case PrintIntentSlot::SymptomIntents:    return "symptom_intents";
    default:                                 return "object_description";
    }
}

const char* to_string(PrintIntentSource s)
{
    switch (s) {
    case PrintIntentSource::DeterministicParser: return "deterministic_parser";
    case PrintIntentSource::Session:             return "session";
    case PrintIntentSource::LlmExtract:          return "llm_extract";
    case PrintIntentSource::GeometryInference:   return "geometry_inference";
    case PrintIntentSource::ActivePreset:        return "active_preset";
    case PrintIntentSource::Unknown:
    default:                                     return "unknown";
    }
}

bool PrintIntent::has_symptom(PrintGoalIntent i) const
{
    return symptom_goal.has_intent(i);
}

bool PrintIntent::is_print_quality_request() const
{
    return priority == PrintIntentPriority::Quality
        || symptom_goal.has_intent(PrintGoalIntent::Cosmetic);
}

bool PrintIntent::has_missing_slot(PrintIntentSlot slot) const
{
    return std::find(missing_slots.begin(), missing_slots.end(), slot) != missing_slots.end();
}

bool PrintIntent::has_blocking_slot(PrintIntentSlot slot) const
{
    return std::find(blocking_slots.begin(), blocking_slots.end(), slot) != blocking_slots.end();
}

nlohmann::json PrintIntent::to_json() const
{
    return nlohmann::json{
        {"object_description", object_description},
        {"target_size", target_size_to_json(target_size)},
        {"material", material},
        {"priority", to_string(priority)},
        {"printer_model", printer_model},
        {"printer_id", printer_id},
        {"symptom_goal", print_goal_to_json(symptom_goal)},
        {"confidence", confidence},
        {"missing_slots", slots_to_json(missing_slots)},
        {"blocking_slots", slots_to_json(blocking_slots)},
        {"slot_sources", slot_sources.is_null() ? nlohmann::json::object() : slot_sources},
    };
}

PrintIntent PrintIntent::from_json(const nlohmann::json& j)
{
    PrintIntent intent;
    if (!j.is_object())
        return intent;
    intent.object_description = j.value("object_description", std::string{});
    if (j.contains("target_size"))
        intent.target_size = target_size_from_json(j["target_size"]);
    intent.material      = j.value("material", std::string{});
    intent.priority      = print_intent_priority_from_string(j.value("priority", std::string{"unknown"}));
    intent.printer_model = j.value("printer_model", std::string{});
    intent.printer_id    = j.value("printer_id", std::string{});
    if (j.contains("symptom_goal"))
        intent.symptom_goal = print_goal_from_json(j["symptom_goal"]);
    intent.confidence     = j.value("confidence", 0.f);
    if (j.contains("missing_slots"))
        intent.missing_slots = slots_from_json(j["missing_slots"]);
    if (j.contains("blocking_slots"))
        intent.blocking_slots = slots_from_json(j["blocking_slots"]);
    if (j.contains("slot_sources") && j["slot_sources"].is_object())
        intent.slot_sources = j["slot_sources"];
    else
        intent.slot_sources = nlohmann::json::object();
    return intent;
}

} // namespace BambuSmartPrint
} // namespace Slic3r

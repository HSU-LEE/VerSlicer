#include "PrintIntentExtractor.hpp"
#include "PrintGoalParser.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {
namespace BambuSmartPrint {

namespace {

// LLM slots only overwrite scalar intent fields at or above this confidence.
constexpr double kLlmOverwriteConfidence = 0.7;

std::string read_config_string(const DynamicPrintConfig& cfg, const std::string& key)
{
    if (!cfg.has(key))
        return {};
    const ConfigOption* opt = cfg.option(key);
    if (!opt)
        return {};
    if (auto* s = dynamic_cast<const ConfigOptionString*>(opt))
        return s->value;
    if (auto* ss = dynamic_cast<const ConfigOptionStrings*>(opt)) {
        if (!ss->values.empty())
            return ss->values.front();
        return {};
    }
    return opt->serialize();
}

} // namespace

PrintIntentPriority PrintIntentExtractor::derive_priority(const PrintGoal& goal)
{
    const float c = goal.weight_cosmetic;
    const float s = goal.weight_strong;
    const float f = goal.weight_fast;

    const bool has_fast     = goal.has_intent(PrintGoalIntent::Fast);
    const bool has_strong   = goal.has_intent(PrintGoalIntent::Strong)
                              || goal.has_intent(PrintGoalIntent::Outdoor);
    const bool has_cosmetic = goal.has_intent(PrintGoalIntent::Cosmetic);

    // fast weight >= 0.85 and dominant -> Speed
    if (f >= 0.85f && f > s && f > c)
        return PrintIntentPriority::Speed;
    // strong >= 0.85 and >= fast -> Strength
    if (s >= 0.85f && s >= f)
        return PrintIntentPriority::Strength;
    // cosmetic >= 0.85 and > fast -> Quality
    if (c >= 0.85f && c > f)
        return PrintIntentPriority::Quality;
    // strong & fast both present and close -> Balanced
    if (has_strong && has_fast && std::fabs(s - f) <= 0.15f)
        return PrintIntentPriority::Balanced;
    // intent-majority (Strong > Fast > Cosmetic)
    if (has_strong)
        return PrintIntentPriority::Strength;
    if (has_fast)
        return PrintIntentPriority::Speed;
    if (has_cosmetic)
        return PrintIntentPriority::Quality;
    return PrintIntentPriority::Unknown;
}

PrintIntent PrintIntentExtractor::extract_deterministic(const std::string& user_text)
{
    PrintIntent intent;
    intent.symptom_goal = PrintGoalParser::parse(user_text);
    intent.priority     = derive_priority(intent.symptom_goal);
    intent.confidence   = std::max({ intent.symptom_goal.weight_cosmetic,
                                     intent.symptom_goal.weight_strong,
                                     intent.symptom_goal.weight_fast,
                                     intent.symptom_goal.weight_outdoor });

    const bool recognized = std::any_of(
        intent.symptom_goal.intents.begin(), intent.symptom_goal.intents.end(),
        [](PrintGoalIntent i) { return i != PrintGoalIntent::Unknown; });
    if (recognized)
        intent.slot_sources[to_string(PrintIntentSlot::SymptomIntents)] =
            to_string(PrintIntentSource::DeterministicParser);
    if (intent.priority != PrintIntentPriority::Unknown)
        intent.slot_sources[to_string(PrintIntentSlot::Priority)] =
            to_string(PrintIntentSource::DeterministicParser);
    return intent;
}

void PrintIntentExtractor::enrich_from_geometry(PrintIntent&              intent,
                                                const ModelAnalysis&      mesh,
                                                const DynamicPrintConfig& base_config,
                                                const std::string&        printer_id)
{
    if (intent.material.empty() && !mesh.suggested_material.empty()) {
        intent.material = mesh.suggested_material;
        intent.slot_sources[to_string(PrintIntentSlot::Material)] =
            to_string(PrintIntentSource::GeometryInference);
    }

    if (intent.printer_model.empty()) {
        const std::string model = read_config_string(base_config, "printer_model");
        if (!model.empty()) {
            intent.printer_model = model;
            intent.slot_sources[to_string(PrintIntentSlot::Printer)] =
                to_string(PrintIntentSource::ActivePreset);
        }
    }

    if (intent.printer_id.empty() && !printer_id.empty())
        intent.printer_id = printer_id;
}

void PrintIntentExtractor::merge_llm_slots(PrintIntent& intent, const nlohmann::json& slots)
{
    if (!slots.is_object())
        return;

    const double conf = slots.value("confidence", 0.0);
    if (conf > static_cast<double>(intent.confidence))
        intent.confidence = static_cast<float>(conf);

    // Scalar slots overwritten only at/above the confidence threshold.
    if (conf >= kLlmOverwriteConfidence) {
        if (slots.contains("material") && slots["material"].is_string()) {
            const std::string mat = slots["material"].get<std::string>();
            if (!mat.empty()) {
                intent.material = mat;
                intent.slot_sources[to_string(PrintIntentSlot::Material)] =
                    to_string(PrintIntentSource::LlmExtract);
            }
        }
        if (slots.contains("priority") && slots["priority"].is_string()) {
            const PrintIntentPriority p =
                print_intent_priority_from_string(slots["priority"].get<std::string>());
            if (p != PrintIntentPriority::Unknown) {
                intent.priority = p;
                intent.slot_sources[to_string(PrintIntentSlot::Priority)] =
                    to_string(PrintIntentSource::LlmExtract);
            }
        }
        if (slots.contains("object_description") && slots["object_description"].is_string()) {
            const std::string desc = slots["object_description"].get<std::string>();
            if (!desc.empty()) {
                intent.object_description = desc;
                intent.slot_sources[to_string(PrintIntentSlot::ObjectDescription)] =
                    to_string(PrintIntentSource::LlmExtract);
            }
        }
    }

    // target_size is LLM-only (deterministic parser never fills it).
    if (slots.contains("target_size") && slots["target_size"].is_object()) {
        const nlohmann::json& ts = slots["target_size"];
        PrintIntentTargetSize size;
        size.scale_factor = ts.value("scale_factor", 1.0);
        size.dim_x_mm     = ts.value("dim_x_mm", 0.0);
        size.dim_y_mm     = ts.value("dim_y_mm", 0.0);
        size.dim_z_mm     = ts.value("dim_z_mm", 0.0);
        size.raw_text     = ts.value("raw_text", std::string{});
        size.has_value    = size.scale_factor != 1.0 || size.dim_x_mm > 0.0 || size.dim_y_mm > 0.0
                            || size.dim_z_mm > 0.0 || !size.raw_text.empty();
        if (size.has_value) {
            intent.target_size = size;
            intent.slot_sources[to_string(PrintIntentSlot::TargetSize)] =
                to_string(PrintIntentSource::LlmExtract);
        }
    }
}

} // namespace BambuSmartPrint
} // namespace Slic3r

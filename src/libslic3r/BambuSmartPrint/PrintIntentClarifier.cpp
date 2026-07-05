#include "PrintIntentClarifier.hpp"

#include <algorithm>
#include <cctype>

namespace Slic3r {
namespace BambuSmartPrint {

namespace {

std::string to_upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string active_filament_family(const DynamicPrintConfig& cfg)
{
    if (!cfg.has("filament_type"))
        return {};
    const ConfigOption* opt = cfg.option("filament_type");
    if (!opt)
        return {};
    if (auto* ss = dynamic_cast<const ConfigOptionStrings*>(opt)) {
        if (!ss->values.empty())
            return to_upper(ss->values.front());
        return {};
    }
    if (auto* s = dynamic_cast<const ConfigOptionString*>(opt))
        return to_upper(s->value);
    return {};
}

bool is_flexible_family(const std::string& upper)
{
    return upper.find("TPU") != std::string::npos
        || upper.find("TPE") != std::string::npos
        || upper.find("FLEX") != std::string::npos;
}

void add_unique(std::vector<PrintIntentSlot>& v, PrintIntentSlot s)
{
    if (std::find(v.begin(), v.end(), s) == v.end())
        v.push_back(s);
}

} // namespace

void PrintIntentClarifier::recompute_missing_slots(PrintIntent&              intent,
                                                   const ModelAnalysis&      mesh,
                                                   const DynamicPrintConfig& base)
{
    (void) mesh;
    intent.missing_slots.clear();
    intent.blocking_slots.clear();

    const std::string active_family = active_filament_family(base);
    const bool active_is_pla        = active_family.find("PLA") != std::string::npos;
    const bool active_is_flexible   = is_flexible_family(active_family);

    // --- Material -----------------------------------------------------------
    const bool material_empty     = intent.material.empty();
    const bool wants_tough        = intent.has_symptom(PrintGoalIntent::Outdoor)
                                    || intent.has_symptom(PrintGoalIntent::Strong);
    const bool requested_flexible = is_flexible_family(to_upper(intent.material));

    if (material_empty)
        add_unique(intent.missing_slots, PrintIntentSlot::Material);

    // Blocking: an outdoor/ABS-type need against loaded PLA, or a TPU-implying
    // request that conflicts with a non-flexible loaded filament.
    const bool material_blocks =
        (material_empty && wants_tough && active_is_pla)
        || (requested_flexible && !active_family.empty() && !active_is_flexible);
    if (material_blocks) {
        add_unique(intent.missing_slots, PrintIntentSlot::Material);
        add_unique(intent.blocking_slots, PrintIntentSlot::Material);
    }

    // --- Priority -----------------------------------------------------------
    int conflicting = 0;
    if (intent.has_symptom(PrintGoalIntent::Strong))   ++conflicting;
    if (intent.has_symptom(PrintGoalIntent::Fast))     ++conflicting;
    if (intent.has_symptom(PrintGoalIntent::Cosmetic)) ++conflicting;
    if (intent.priority == PrintIntentPriority::Unknown && conflicting >= 2) {
        add_unique(intent.missing_slots, PrintIntentSlot::Priority);
        add_unique(intent.blocking_slots, PrintIntentSlot::Priority);
    }

    // --- Non-blocking slots -------------------------------------------------
    if (!intent.target_size.has_value)
        add_unique(intent.missing_slots, PrintIntentSlot::TargetSize);
    if (intent.object_description.empty())
        add_unique(intent.missing_slots, PrintIntentSlot::ObjectDescription);
    if (intent.printer_model.empty())
        add_unique(intent.missing_slots, PrintIntentSlot::Printer);

    const bool has_recognized_symptom = std::any_of(
        intent.symptom_goal.intents.begin(), intent.symptom_goal.intents.end(),
        [](PrintGoalIntent i) { return i != PrintGoalIntent::Unknown; });
    if (!has_recognized_symptom)
        add_unique(intent.missing_slots, PrintIntentSlot::SymptomIntents);
}

std::optional<ClarifyingQuestion> PrintIntentClarifier::next_question(const PrintIntent& intent,
                                                                      bool               korean)
{
    (void) korean;
    if (intent.has_blocking_slot(PrintIntentSlot::Material)) {
        ClarifyingQuestion q;
        q.slot          = PrintIntentSlot::Material;
        q.question_ko   = "이 모델을 어떤 필라멘트로 출력할까요? (예: PLA, PETG, ABS, TPU)";
        q.question_en   = "Which filament should I print this with? (e.g. PLA, PETG, ABS, TPU)";
        q.blocks_config = true;
        return q;
    }
    if (intent.has_blocking_slot(PrintIntentSlot::Priority)) {
        ClarifyingQuestion q;
        q.slot          = PrintIntentSlot::Priority;
        q.question_ko   = "무엇을 가장 우선할까요? 강도, 속도, 표면 품질 중에서 골라 주세요.";
        q.question_en   = "What matters most for this print — strength, speed, or surface quality?";
        q.blocks_config = true;
        return q;
    }
    return std::nullopt;
}

} // namespace BambuSmartPrint
} // namespace Slic3r

#include "AICoachTrustBuilder.hpp"

#include "../BambuSmartPrint/BambuSmartPrintService.hpp"
#include "../OllamaAssistant/OllamaActionPipeline.hpp"
#include "../OllamaAssistant/OllamaActionWorkflow.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../Plater.hpp"
#include "../format.hpp"

#include "libslic3r/BambuSmartPrint/BambuSmartPrintTypes.hpp"
#include "libslic3r/BambuSmartPrint/ConfigSnapshot.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/PresetBundle.hpp"

#include <unordered_map>

namespace Slic3r { namespace GUI {

namespace {

std::string display_value(const std::string& key, const std::string& raw)
{
    if (key == "enable_support" || key == "enable_brim")
        return (raw == "1" || raw == "true" || raw == "on") ? _u8L("On") : _u8L("Off");
    if (key == "brim_type") {
        if (raw == "no_brim" || raw == "0")
            return _u8L("Off");
        return raw;
    }
    return raw;
}

std::string setting_label(const std::string& key)
{
    static const std::unordered_map<std::string, const char*> labels = {
        {"bed_temperature", "Bed temperature"},
        {"first_layer_bed_temperature", "First-layer bed temperature"},
        {"nozzle_temperature", "Nozzle temperature"},
        {"first_layer_temperature", "First-layer nozzle temperature"},
        {"brim_type", "Brim"},
        {"brim_width", "Brim width"},
        {"enable_support", "Support"},
        {"enable_brim", "Brim"},
        {"first_layer_speed", "First-layer speed"},
        {"initial_layer_speed", "First-layer speed"},
        {"initial_layer_print_speed", "First-layer speed"},
    };
    const auto it = labels.find(key);
    return it != labels.end() ? std::string(_u8L(it->second)) : key;
}

void append_section_lines(AICoachCard& card, const AICoachTrustBrief& trust)
{
    if (!trust.changes.empty()) {
        card.sections.push_back(std::string(_u8L("What will change")));
        for (const AICoachSettingLine& ch : trust.changes) {
            card.sections.push_back(ch.label + ": " + ch.before_value + " -> " + ch.after_value);
        }
    }
    if (!trust.reason.empty()) {
        card.sections.push_back(std::string(_u8L("Reason")));
        card.sections.push_back(trust.reason);
    }
    if (!trust.success_effect.empty() || !trust.time_effect.empty() || !trust.filament_effect.empty()) {
        card.sections.push_back(std::string(_u8L("What to expect")));
        if (!trust.success_effect.empty())
            card.sections.push_back(trust.success_effect);
        if (!trust.time_effect.empty())
            card.sections.push_back(trust.time_effect);
        if (!trust.filament_effect.empty())
            card.sections.push_back(trust.filament_effect);
    }
    {
        std::string conf = std::string(_u8L("Confidence: "))
            + std::to_string(trust.confidence_percent) + "%";
        if (trust.experimental)
            conf += std::string(" (") + _u8L("experimental") + ")";
        card.sections.push_back(std::move(conf));
    }
}

} // namespace

bool AICoachTrustBuilder::enrich_recommendation_card(AICoachCard& card, Plater* plater)
{
    if (card.apply_root.empty() || !plater || !wxGetApp().preset_bundle)
        return false;
    if (!card.apply_root.contains("actions") || !card.apply_root["actions"].is_array()
        || card.apply_root["actions"].empty())
        return false;

    BambuSmartPrintService::instance().update_plate_assessment_data(plater);
    const DynamicPrintConfig before = wxGetApp().preset_bundle->full_config(false);

    if (!OllamaActionPipeline::prepare_apply_root(card.apply_root, card.body, /*include_makerworld*/ false)) {
        card.apply_root.erase("actions");
        return false;
    }

    const DynamicPrintConfig after = OllamaActionWorkflow::simulate_proposed_config(before, card.apply_root);

    const std::vector<BambuSmartPrint::SettingChange> diff =
        BambuSmartPrint::ConfigSnapshot::diff(before, after);
    if (diff.empty())
        return false;

    AICoachTrustBrief trust;
    trust.changes.reserve(diff.size());
    for (const BambuSmartPrint::SettingChange& ch : diff) {
        AICoachSettingLine line;
        line.label         = setting_label(ch.key);
        line.before_value  = display_value(ch.key, ch.old_value);
        line.after_value   = display_value(ch.key, ch.new_value);
        trust.changes.push_back(std::move(line));
    }

    if (card.apply_root.contains("message") && card.apply_root["message"].is_string())
        trust.reason = card.apply_root["message"].get<std::string>();
    else if (!diff.empty() && !diff.front().reason.empty())
        trust.reason = diff.front().reason;
    else
        trust.reason = _u8L("This should make the print more reliable for your model.");

    const auto& readiness = BambuSmartPrintService::instance().last_readiness_report();
    const auto& prediction = BambuSmartPrintService::instance().last_prediction();
    float score = readiness.score > 0.f ? readiness.score : prediction.success_rate;
    if (score <= 0.f)
        score = 75.f;
    trust.confidence_percent = static_cast<int>(std::round(std::min(99.f, std::max(40.f, score))));
    trust.experimental       = trust.confidence_percent < 70;

    bool adds_time = false;
    bool adds_material = false;
    for (const auto& ch : diff) {
        if (ch.key.find("brim") != std::string::npos || ch.key == "enable_support")
            adds_material = adds_time = true;
        if (ch.key.find("temperature") != std::string::npos)
            trust.success_effect = _u8L("First layer should stick to the bed more reliably");
    }
    if (trust.success_effect.empty())
        trust.success_effect = _u8L("Higher chance of a successful print");
    if (adds_time)
        trust.time_effect = _u8L("Print time: about 3 minutes longer");
    if (adds_material)
        trust.filament_effect = _u8L("Filament: about 2 g more");

    card.kind  = AICoachCardKind::ExplainableRecommendation;
    card.trust = std::move(trust);
    card.sections.clear();
    append_section_lines(card, card.trust);

    card.buttons.clear();
    card.buttons = {
        {_u8L("Apply"), AICoachButtonRole::Primary, "apply_actions", card.apply_root},
        {_u8L("Not now"), AICoachButtonRole::Secondary, "dismiss", {}},
    };
    card.auto_dismiss_ms = 0;

    return true;
}

}} // namespace

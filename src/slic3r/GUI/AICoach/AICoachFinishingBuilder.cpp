#include "AICoachFinishingBuilder.hpp"

#include "../BambuSmartPrint/BambuSmartPrintService.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../Plater.hpp"
#include "../format.hpp"

#include "libslic3r/Config.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r { namespace GUI {

namespace {

bool config_bool(const DynamicPrintConfig& cfg, const char* key)
{
    return cfg.has(key) && cfg.opt_bool(key);
}

double config_float(const DynamicPrintConfig& cfg, const char* key)
{
    return cfg.has(key) ? cfg.opt_float(key) : 0.0;
}

std::string primary_model_name(Plater* plater)
{
    if (!plater || plater->model().objects.empty())
        return {};
    if (const ModelObject* obj = plater->model().objects.front())
        return obj->name;
    return {};
}

} // namespace

AICoachCard AICoachFinishingBuilder::build_print_success_card(Plater* plater, const std::string& job_name)
{
    AICoachCard card;
    card.trigger         = AICoachTriggerId::PrintSuccessFinishing;
    card.importance      = AICoachImportance::Normal;
    card.kind            = AICoachCardKind::PrintFinishing;
    card.title           = _u8L("Print complete");
    card.body            = _u8L("Here is what to do next, based on your slice settings.");
    card.auto_dismiss_ms = 0;

    DynamicPrintConfig cfg;
    if (wxGetApp().preset_bundle)
        cfg = wxGetApp().preset_bundle->full_config(false);

    bool has_brim = config_float(cfg, "brim_width") > 0.01;
    if (cfg.has("brim_type")) {
        const BrimType bt = cfg.opt_enum<BrimType>("brim_type");
        if (bt != btNoBrim)
            has_brim = true;
    }
    const bool support = config_bool(cfg, "enable_support");

    if (has_brim)
        card.finishing.summary_lines.push_back(_u8L("Brim used"));
    if (support)
        card.finishing.summary_lines.push_back(_u8L("Support used"));

    std::string time_str;
    double      filament_g = 0.0;
    if (plater) {
        const Print& print = plater->fff_print();
        {
            const auto& stats = print.print_statistics();
            if (!stats.estimated_normal_print_time.empty())
                time_str = stats.estimated_normal_print_time;
            if (stats.total_weight > 0.05)
                filament_g = stats.total_weight;
        }
    }
    if (time_str.empty())
        time_str = BambuSmartPrintService::instance().last_estimated_print_time();
    if (filament_g < 0.1)
        filament_g = BambuSmartPrintService::instance().last_estimated_filament_g();

    if (!time_str.empty())
        card.finishing.summary_lines.push_back(std::string(_u8L("Print time ")) + time_str);
    if (filament_g > 0.1)
        card.finishing.summary_lines.push_back(
            std::string(_u8L("Filament ")) + std::to_string(static_cast<int>(std::round(filament_g))) + " g");

    card.sections.push_back(_u8L("Post-processing checklist"));
    card.sections.push_back(_u8L("We suggest these steps:"));
    if (has_brim)
        card.finishing.checklist.push_back(_u8L("Remove brim"));
    if (support)
        card.finishing.checklist.push_back(_u8L("Remove support"));
    card.finishing.checklist.push_back(_u8L("Clean the surface (sand or deburr if needed)"));
    for (const std::string& item : card.finishing.checklist)
        card.sections.push_back(std::string("☐ ") + item);

    card.sections.push_back(_u8L("Step-by-step guide"));
    int step = 1;
    auto add_step = [&](const char* title, const char* duration, const char* difficulty) {
        AICoachFinishingStep s;
        s.title          = _u8L(title);
        s.duration_hint  = duration;
        s.difficulty     = _u8L(difficulty);
        card.finishing.steps.push_back(s);
        card.sections.push_back(std::string(_u8L("Step ")) + std::to_string(step) + " — " + s.title);
        card.sections.push_back(
            std::string(_u8L("Time: ")) + s.duration_hint + " · " + _u8L("Difficulty: ") + s.difficulty);
        ++step;
    };
    if (support)
        add_step("Remove support", "2 min", "Medium");
    if (has_brim)
        add_step("Remove brim", "30 sec", "Easy");
    add_step("Surface cleanup", "3 min", "Easy");

    card.sections.push_back(_u8L("Print record"));
    card.finishing.history_model = !job_name.empty() ? job_name : primary_model_name(plater);
    card.finishing.history_time  = time_str;
    if (filament_g > 0.1)
        card.finishing.history_filament = std::to_string(static_cast<int>(std::round(filament_g))) + " g";
    card.sections.push_back(_u8L("Print succeeded"));
    if (!card.finishing.history_model.empty())
        card.sections.push_back(std::string(_u8L("Model: ")) + card.finishing.history_model);
    if (!card.finishing.history_time.empty())
        card.sections.push_back(std::string(_u8L("Time: ")) + card.finishing.history_time);
    if (!card.finishing.history_filament.empty())
        card.sections.push_back(std::string(_u8L("Filament: ")) + card.finishing.history_filament);
    card.sections.push_back(_u8L("Saved to Smart Print history automatically."));

    card.sections.push_back(_u8L("How did this print go?"));
    card.buttons = {
        {_u8L("Great"), AICoachButtonRole::Secondary, "feedback_good", {}},
        {_u8L("OK"), AICoachButtonRole::Secondary, "feedback_ok", {}},
        {_u8L("Failed"), AICoachButtonRole::Secondary, "feedback_bad", {}},
        {_u8L("Close"), AICoachButtonRole::Primary, "dismiss", {}},
    };

    return card;
}

}} // namespace

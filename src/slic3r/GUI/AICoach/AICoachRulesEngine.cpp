#include "AICoachRulesEngine.hpp"
#include "AICoachTrustBuilder.hpp"
#include "AICoachTriggerPolicy.hpp"

#include "../BambuSmartPrint/BambuSmartPrintService.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../Plater.hpp"
#include "libslic3r/BambuSmartPrint/BambuSmartPrintTypes.hpp"
#include "libslic3r/SlicePilot/SlicePilotRestrictions.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Slic3r { namespace GUI {

namespace {

constexpr double kTallHeightMm     = 150.0;
constexpr double kNarrowFootprint  = 400.0; // mm^2 min_xy footprint
constexpr double kOverhangRatio    = 0.15;
constexpr double kLowBedUtilRatio  = 0.25;
constexpr float  kAdhesionRiskScore = 55.f;

nlohmann::json set_config_action(const std::string& key, const nlohmann::json& value)
{
    return nlohmann::json{
        {"type", "set_config"},
        {"preset", "print"},
        {"options", nlohmann::json::object({{key, value}})},
    };
}

AICoachCard make_card(AICoachTriggerId id, AICoachImportance imp, const std::string& body,
                      std::vector<AICoachButton> buttons, nlohmann::json apply_root = {},
                      std::vector<std::string> bullets = {})
{
    AICoachCard c;
    c.trigger           = id;
    c.importance        = imp;
    c.title             = "AI Coach";
    c.body              = body;
    c.bullets           = std::move(bullets);
    c.buttons           = std::move(buttons);
    c.apply_root        = std::move(apply_root);
    c.auto_dismiss_ms   = imp == AICoachImportance::Critical ? 0 : 10000;
    AICoachTriggerPolicy::apply_defaults(c);
    return c;
}

double model_max_height(Plater* plater)
{
    double max_z = 0.0;
    if (!plater)
        return max_z;
    for (const ModelObject* obj : plater->model().objects) {
        if (!obj)
            continue;
        max_z = std::max(max_z, obj->bounding_box_exact().size().z());
    }
    return max_z;
}

double model_min_footprint(Plater* plater)
{
    double min_area = std::numeric_limits<double>::max();
    if (!plater)
        return min_area;
    for (const ModelObject* obj : plater->model().objects) {
        if (!obj)
            continue;
        const Vec3d sz = obj->bounding_box_exact().size();
        min_area = std::min(min_area, sz.x() * sz.y());
    }
    return min_area == std::numeric_limits<double>::max() ? 0.0 : min_area;
}

int object_count(Plater* plater)
{
    return plater ? static_cast<int>(plater->model().objects.size()) : 0;
}

float bed_utilization_ratio(Plater* plater)
{
    if (!plater)
        return 1.f;
    const BoundingBoxf3 bed = plater->build_volume().bounding_volume();
    const double bed_area   = std::max(1.0, bed.size().x() * bed.size().y());

    BoundingBoxf3 used;
    for (const ModelObject* obj : plater->model().objects) {
        if (!obj)
            continue;
        used.merge(obj->bounding_box_exact());
    }
    if (!used.defined)
        return 0.f;
    const double used_area = used.size().x() * used.size().y();
    return static_cast<float>(std::min(1.0, used_area / bed_area));
}

double filament_weight_g(const PrintStatistics& stats)
{
    if (stats.total_weight > 0.05)
        return stats.total_weight;
    return 0.0;
}

std::string format_filament_bullet(double grams)
{
    if (grams < 0.05)
        return {};
    const int g = static_cast<int>(std::round(grams));
    if (g < 1)
        return _u8L("Filament: less than 1 g");
    if (g >= 1000)
        return std::string(_u8L("Filament: about ")) + std::to_string((g + 500) / 1000) + _u8L(" kg");
    return std::string(_u8L("Filament: about ")) + std::to_string(g) + " g";
}

void enrich_apply_cards(std::vector<AICoachCard>& cards, Plater* plater)
{
    for (AICoachCard& c : cards) {
        if (!c.apply_root.empty())
            AICoachTrustBuilder::enrich_recommendation_card(c, plater);
    }
}

} // namespace

std::vector<AICoachCard> AICoachRulesEngine::evaluate_after_model_load(Plater* plater)
{
    std::vector<AICoachCard> out;
    if (!plater || plater->model().objects.empty())
        return out;

    BambuSmartPrintService::instance().update_plate_assessment_data(plater);
    const auto& mesh = BambuSmartPrintService::instance().last_mesh_analysis();

    const double height = mesh.height_mm > 0 ? mesh.height_mm : model_max_height(plater);
    const double foot   = mesh.min_xy_footprint_mm2 > 0 ? mesh.min_xy_footprint_mm2 : model_min_footprint(plater);

    if (height >= kTallHeightMm || mesh.tall_narrow || foot < kNarrowFootprint) {
        nlohmann::json root;
        root["message"] = _u8L("A wider brim helps tall prints stay stable on the bed.");
        root["actions"] = nlohmann::json::array({
            set_config_action("brim_type", "outer_only"),
            set_config_action("brim_width", 5),
        });
        out.push_back(make_card(
            AICoachTriggerId::ModelTallBrim, AICoachImportance::Normal,
            _u8L("This model is tall. A brim can help keep it stable on the bed."),
            {
                {_u8L("Apply"), AICoachButtonRole::Primary, "apply_actions", root},
                {_u8L("Not now"), AICoachButtonRole::Secondary, "dismiss", {}},
            },
            root));
    }

    const double oh = mesh.overhang_face_ratio > 0 ? mesh.overhang_face_ratio : mesh.overhang_ratio;
    if (oh >= kOverhangRatio) {
        nlohmann::json root;
        root["message"] = _u8L("Supports hold up steep overhangs so they do not sag while printing.");
        root["actions"] = nlohmann::json::array({ set_config_action("enable_support", true) });
        out.push_back(make_card(
            AICoachTriggerId::OverhangSupport, AICoachImportance::Normal,
            _u8L("Steep overhangs were found on this model. Turning on supports can improve print success."),
            {
                {_u8L("Turn on supports"), AICoachButtonRole::Primary, "apply_actions", root},
                {_u8L("Not now"), AICoachButtonRole::Secondary, "dismiss", {}},
            },
            root));
    }

    if (object_count(plater) == 1 && bed_utilization_ratio(plater) < kLowBedUtilRatio) {
        out.push_back(make_card(
            AICoachTriggerId::BedArrange, AICoachImportance::Low,
            _u8L("There is plenty of empty space on the bed. Auto-arrange can center the model."),
            {
                {_u8L("Auto-arrange"), AICoachButtonRole::Primary, "arrange", {}},
                {_u8L("Not now"), AICoachButtonRole::Secondary, "dismiss", {}},
            }));
    }

    const auto& readiness = BambuSmartPrintService::instance().last_readiness_report();
    if (readiness.score > 0.f && readiness.score < kAdhesionRiskScore) {
        std::vector<std::string> bullets;
        for (const auto& item : readiness.action_items) {
            if (bullets.size() >= 3)
                break;
            bullets.push_back(item);
        }
        if (bullets.empty() && !readiness.headline.empty())
            bullets.push_back(readiness.headline);

        nlohmann::json actions = nlohmann::json::array();
        const auto& auto_res = BambuSmartPrintService::instance().last_auto_result();
        for (const auto& ch : auto_res.changes) {
            if (actions.size() >= 4)
                break;
            try {
                if (ch.key == "brim_width" || ch.key == "brim_type" || ch.key == "enable_support"
                    || ch.key == "bed_temperature" || ch.key == "first_layer_bed_temperature") {
                    actions.push_back(set_config_action(ch.key, nlohmann::json(ch.new_value)));
                }
            } catch (...) {
            }
        }
        if (actions.empty()) {
            actions.push_back(set_config_action("brim_type", "outer_only"));
            actions.push_back(set_config_action("brim_width", 5));
        }
        nlohmann::json root;
        root["message"] = _u8L("Safer first-layer settings can reduce the risk of the print coming loose.");
        root["actions"] = actions;

        out.push_back(make_card(
            AICoachTriggerId::AdhesionRisk, AICoachImportance::Critical,
            _u8L("The first layer may not stick well with the current settings. I can suggest safer values."),
            {
                {_u8L("Apply suggestions"), AICoachButtonRole::Primary, "apply_actions", root},
                {_u8L("Adjust manually"), AICoachButtonRole::Secondary, "open_settings", {}},
            },
            root,
            bullets));
    }

    enrich_apply_cards(out, plater);
    return out;
}

std::vector<AICoachCard> AICoachRulesEngine::evaluate_after_slice(Plater* plater, const Print* print, bool slice_ok)
{
    std::vector<AICoachCard> out;
    if (!slice_ok || !plater || !print)
        return out;

    BambuSmartPrintService::instance().refresh_post_slice_assessment(plater);

    std::string time_str;
    double      filament_g = 0.0;
    const auto& stats      = print->print_statistics();
    if (!stats.estimated_normal_print_time.empty())
        time_str = stats.estimated_normal_print_time;
    else if (!BambuSmartPrintService::instance().last_estimated_print_time().empty())
        time_str = BambuSmartPrintService::instance().last_estimated_print_time();
    filament_g = filament_weight_g(stats);
    if (filament_g < 0.05)
        filament_g = BambuSmartPrintService::instance().last_estimated_filament_g();

    std::vector<std::string> summary;
    if (!time_str.empty())
        summary.push_back(std::string(_u8L("Print time: ")) + time_str);
    if (const std::string fil = format_filament_bullet(filament_g); !fil.empty())
        summary.push_back(fil);

    const bool bbl = wxGetApp().preset_bundle
        && Slic3r::SlicePilot::is_active_printer_bbl(*wxGetApp().preset_bundle);
    if (bbl) {
        out.push_back(make_card(
            AICoachTriggerId::SliceDoneSend, AICoachImportance::Normal,
            _u8L("Slicing finished. Review the estimates below, then send the job to your printer."),
            {
                {_u8L("Send to printer"), AICoachButtonRole::Primary, "send_print", {}},
                {_u8L("Preview"), AICoachButtonRole::Secondary, "preview_tab", {}},
            },
            {},
            summary));
    } else {
        out.push_back(make_card(
            AICoachTriggerId::SliceDoneSend, AICoachImportance::Normal,
            _u8L("Slicing finished. Export G-code to print on your machine, or open Preview to check layers."),
            {
                {_u8L("Export G-code"), AICoachButtonRole::Primary, "export_gcode", {}},
                {_u8L("Preview"), AICoachButtonRole::Secondary, "preview_tab", {}},
            },
            {},
            summary));
    }

    const auto& slice_a = BambuSmartPrintService::instance().last_slice_analysis();
    if (slice_a.valid && slice_a.overhang_area_ratio >= kOverhangRatio) {
        nlohmann::json root;
        root["message"] = _u8L("Supports hold up steep overhangs so they do not sag while printing.");
        root["actions"] = nlohmann::json::array({ set_config_action("enable_support", true) });
        out.push_back(make_card(
            AICoachTriggerId::OverhangSupport, AICoachImportance::Normal,
            _u8L("After slicing, steep overhangs remain. Supports can help this print succeed."),
            {
                {_u8L("Turn on supports"), AICoachButtonRole::Primary, "apply_actions", root},
                {_u8L("Not now"), AICoachButtonRole::Secondary, "dismiss", {}},
            },
            root));
    }

    enrich_apply_cards(out, plater);
    return out;
}

std::vector<AICoachCard> AICoachRulesEngine::evaluate_periodic(Plater* plater)
{
    return evaluate_after_model_load(plater);
}

std::vector<AICoachCard> AICoachRulesEngine::evaluate_during_print(Plater* plater, int print_percent)
{
    std::vector<AICoachCard> out;
    if (!plater || print_percent < 0)
        return out;

    const std::string body = std::string(_u8L("Your print is in progress ("))
        + std::to_string(print_percent)
        + _u8L("%). Avoid opening the chamber unless you need to check something.");

    out.push_back(make_card(
        AICoachTriggerId::PrintMonitor, AICoachImportance::Low, body,
        {
            {_u8L("OK"), AICoachButtonRole::Primary, "dismiss", {}},
        }));
    return out;
}

std::vector<AICoachCard> AICoachRulesEngine::evaluate_personal_trainer(Plater* plater)
{
    std::vector<AICoachCard> out;
    if (!plater)
        return out;

    nlohmann::json root;
    root["message"] = _u8L("A slower first layer can improve bed adhesion on repeated failures.");
    root["actions"] = nlohmann::json::array({
        set_config_action("initial_layer_speed", 30),
        set_config_action("first_layer_speed", 30),
    });

    out.push_back(make_card(
        AICoachTriggerId::PersonalTrainer, AICoachImportance::Low,
        _u8L("Recent prints did not go well. Try a slower first layer for better adhesion."),
        {
            {_u8L("Apply"), AICoachButtonRole::Primary, "apply_actions", root},
            {_u8L("Not now"), AICoachButtonRole::Secondary, "dismiss", {}},
        },
        root));

    enrich_apply_cards(out, plater);
    return out;
}

}} // namespace

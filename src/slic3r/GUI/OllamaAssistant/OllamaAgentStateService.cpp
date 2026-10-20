#include "OllamaAgentStateService.hpp"
#include "AiCatalog.hpp"
#include "OllamaMeshOps.hpp"

#include "OllamaActionExecutor.hpp"
#include "OllamaUserFlow.hpp"
#include "../BambuSmartPrint/BambuSmartPrintService.hpp"
#include "../GUI_App.hpp"
#include "../MainFrame.hpp"
#include "../Plater.hpp"
#include "../Selection.hpp"
#include "../GLCanvas3D.hpp"

#include "libslic3r/PresetBundle.hpp"

#include <cmath>
#include <sstream>
#include <unordered_set>

namespace Slic3r { namespace GUI {

namespace {

nlohmann::json selection_snapshot(Plater* plater)
{
    nlohmann::json sel = nlohmann::json::object();
    if (!plater) {
        sel["count"] = 0;
        sel["empty"] = true;
        return sel;
    }
    GLCanvas3D* view3d = plater->get_view3D_canvas3D();
    const Selection& selection = view3d ? view3d->get_selection() : plater->get_selection();
    nlohmann::json   ids       = nlohmann::json::array();
    std::unordered_set<int> seen;
    if (!selection.is_empty()) {
        const int single = selection.get_object_idx();
        if (single >= 0)
            seen.insert(single);
        for (const auto& kv : selection.get_content()) {
            if (kv.first >= 0)
                seen.insert(static_cast<int>(kv.first));
        }
    }
    for (int id : seen)
        ids.push_back(id);
    sel["object_ids"] = std::move(ids);
    sel["count"]      = seen.size();
    sel["empty"]      = selection.is_empty();
    return sel;
}

nlohmann::json objects_snapshot(Plater* plater)
{
    nlohmann::json arr = nlohmann::json::array();
    if (!plater)
        return arr;
    const Model& model = plater->model();
    PartPlateList& plates = plater->get_partplate_list();
    auto round1 = [](double v) { return std::round(v * 10.0) / 10.0; };
    for (size_t i = 0; i < model.objects.size(); ++i) {
        ModelObject* mo = model.objects[i];
        if (!mo)
            continue;
        nlohmann::json obj = nlohmann::json::object({
            {"id", i},
            {"name", mo->name},
        });
        if (!mo->instances.empty()) {
            const int inst = 0;
            ModelInstance* mi = mo->instances[inst];
            const Vec3d off = mi->get_offset();
            const BoundingBoxf3 bb = mo->instance_bounding_box(static_cast<size_t>(inst));
            const Vec3d size = bb.size();
            const double z_deg = mi->get_rotation().z() * 180.0 / 3.14159265358979323846;
            obj["position_mm"] = nlohmann::json::array({round1(off.x()), round1(off.y()), round1(off.z())});
            obj["size_mm"] = nlohmann::json::array({round1(size.x()), round1(size.y()), round1(size.z())});
            obj["rotation_z_deg"] = round1(z_deg);
            const int plate = plates.find_instance_belongs(static_cast<int>(i), inst);
            if (plate >= 0)
                obj["plate"] = plate;
        }
        arr.push_back(std::move(obj));
    }
    return arr;
}

nlohmann::json plates_snapshot(Plater* plater)
{
    nlohmann::json p = nlohmann::json::object();
    if (!plater) {
        p["count"] = 0;
        return p;
    }
    const auto& list = plater->get_partplate_list();
    p["current"]     = list.get_curr_plate_index();
    p["count"]       = list.get_plate_count();
    p["can_add"]     = plater->can_add_plate();
    p["can_delete"]  = plater->can_delete_plate();
    return p;
}

std::string active_tab_name()
{
    MainFrame* mf = wxGetApp().mainframe;
    if (!mf)
        return "unknown";
    if (mf->is_tab_selected(MainFrame::tp3DEditor))
        return "prepare";
    if (mf->is_tab_selected(MainFrame::tpPreview))
        return "preview";
    if (mf->is_tab_selected(MainFrame::tpMonitor))
        return "monitor";
    if (mf->is_tab_selected(MainFrame::tpSmartPrint))
        return "smart_print";
    if (mf->is_tab_selected(MainFrame::tpCalibration))
        return "calibration";
    if (mf->is_tab_selected(MainFrame::tpHome))
        return "home";
    return "other";
}

} // namespace

nlohmann::json OllamaAgentStateService::snapshot()
{
    Plater* plater = wxGetApp().plater();

    nlohmann::json state = nlohmann::json::object();
    state["selection"]   = selection_snapshot(plater);
    state["objects"]     = objects_snapshot(plater);
    state["plates"]      = plates_snapshot(plater);

    bool sliced = false;
    if (plater) {
        const int plate_idx = plater->get_partplate_list().get_curr_plate_index();
        sliced              = plater->get_partplate_list().get_plate(plate_idx)->is_slice_result_valid();
    }
    state["slice_state"] = nlohmann::json::object({
        {"current_plate_sliced", sliced},
        {"export_scheduled", plater ? plater->is_export_gcode_scheduled() : false},
    });

    state["ui"] = nlohmann::json::object({
        {"active_tab", active_tab_name()},
    });

    try {
        state["flow"] = OllamaUserFlow::build_flow_context_json();
    } catch (...) {
        state["flow"] = nlohmann::json::object();
    }

    if (wxGetApp().preset_bundle) {
        auto& bundle = *wxGetApp().preset_bundle;
        state["presets"] = nlohmann::json::object({
            {"print", bundle.prints.get_edited_preset().name},
            {"filament", bundle.filaments.get_edited_preset().name},
            {"printer", bundle.printers.get_edited_preset().name},
        });
    }

    auto& svc = BambuSmartPrintService::instance();
    const auto& readiness = svc.last_readiness_report();
    if (!readiness.headline.empty() || readiness.score > 0.f) {
        state["readiness"] = nlohmann::json::object({
            {"headline", readiness.headline},
            {"score", readiness.score},
            {"success_rate", readiness.success_rate},
        });
    }

    state["mesh_health"] = OllamaMeshOps::mesh_health_for_plate(plater);

    return state;
}

std::string OllamaAgentStateService::snapshot_json()
{
    return snapshot().dump(2);
}

nlohmann::json OllamaAgentStateService::objects_json()
{
    return objects_snapshot(wxGetApp().plater());
}

nlohmann::json OllamaAgentStateService::config_digest(const std::string& user_goal_hint)
{
    nlohmann::json out = nlohmann::json::object();
    if (!wxGetApp().preset_bundle)
        return out;

    (void) user_goal_hint;
    std::vector<std::string> keys;
    {
        static const char* kDefaultKeys[] = {
            "layer_height", "sparse_infill_density", "enable_support", "brim_width",
            "retraction_length", "nozzle_temperature", "bed_temperature", "outer_wall_speed",
        };
        for (const char* k : kDefaultKeys)
            keys.push_back(k);
    }

    const DynamicPrintConfig cfg = wxGetApp().preset_bundle->full_config(false);
    for (const std::string& key : keys) {
        const std::string norm = OllamaActionExecutor::normalize_config_key(key);
        if (!cfg.has(norm))
            continue;
        const ConfigOption* opt = cfg.option(norm);
        if (opt)
            out[norm] = opt->serialize();
    }
    return out;
}

std::vector<std::string> OllamaAgentStateService::verify_config_applied(const nlohmann::json& expected_options,
                                                                        const std::string& preset)
{
    nlohmann::json action = nlohmann::json::object({
        {"type", "set_config"},
        {"preset", preset},
        {"options", expected_options},
    });
    return OllamaActionExecutor::verify_set_config_applied(action);
}

OllamaConfigVerifyReport OllamaAgentStateService::verify_set_config_actions(const nlohmann::json& root,
                                                                            const std::string& user_goal_hint)
{
    OllamaConfigVerifyReport report;
    report.config_digest = OllamaActionExecutor::config_digest_for_set_config_actions(root, user_goal_hint);

    if (!root.contains("actions") || !root["actions"].is_array())
        return report;

    bool had_set_config = false;
    for (const auto& action : root["actions"]) {
        if (!action.is_object() || action.value("type", "") != "set_config")
            continue;
        nlohmann::json work = action;
        OllamaActionExecutor::coalesce_set_config_action_options(work);
        if (!work.contains("options") || !work["options"].is_object())
            continue;

        had_set_config = true;
        const auto mismatches = OllamaActionExecutor::verify_set_config_applied(work);
        if (mismatches.empty()) {
            report.tool_results.push_back(nlohmann::json::object({
                {"tool", "verify_config"},
                {"ok", true},
                {"changed", false},
                {"message", "Config verified"},
                {"data", nlohmann::json::object({
                             {"preset", work.value("preset", "print")},
                             {"options", OllamaActionExecutor::normalized_set_config_options_from_action(work)},
                             {"config_digest", report.config_digest},
                         })},
            }));
            continue;
        }

        report.all_ok = false;
        report.mismatches.insert(report.mismatches.end(), mismatches.begin(), mismatches.end());
        nlohmann::json mismatch_arr = nlohmann::json::array();
        for (const std::string& m : mismatches)
            mismatch_arr.push_back(m);
        report.tool_results.push_back(nlohmann::json::object({
            {"tool", "verify_config"},
            {"ok", false},
            {"changed", false},
            {"message", "Config verification failed"},
            {"blocker", mismatches.front()},
            {"data", nlohmann::json::object({
                         {"preset", work.value("preset", "print")},
                         {"mismatches", std::move(mismatch_arr)},
                         {"config_digest", report.config_digest},
                     })},
        }));
    }

    if (!had_set_config)
        report.config_digest = config_digest(user_goal_hint);

    return report;
}

std::string OllamaAgentStateService::build_verify_retry_nudge(const OllamaConfigVerifyReport& report, bool korean)
{
    std::string fb = ai_catalog_text("verify_config failed. Review config_digest and mismatches, then call set_config again.\n");

    if (!report.mismatches.empty()) {
        fb += ai_catalog_text("\nMismatches:\n");
        for (const std::string& m : report.mismatches)
            fb += "- " + m + "\n";
    }
    if (!report.config_digest.empty()) {
        fb += ai_catalog_text("\nConfig digest (JSON):\n");
        fb += report.config_digest.dump(2);
        fb += "\n";
    }
    fb += ai_catalog_text("\nGoal:\n");
    return fb;
}

}} // namespace

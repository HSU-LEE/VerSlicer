#include "OllamaAgentStateService.hpp"

#include "OllamaActionExecutor.hpp"
#include "OllamaUserFlow.hpp"

#include "../BambuSmartPrint/BambuSmartPrintService.hpp"
#include "../GUI_App.hpp"
#include "../MainFrame.hpp"
#include "../Plater.hpp"
#include "../Selection.hpp"

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
    const Selection& selection = plater->get_selection();
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
    for (size_t i = 0; i < model.objects.size(); ++i) {
        arr.push_back(nlohmann::json::object({
            {"id", i},
            {"name", model.objects[i]->name},
        }));
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

    return state;
}

std::string OllamaAgentStateService::snapshot_json()
{
    return snapshot().dump(2);
}

std::vector<std::string> OllamaAgentStateService::verify_config_applied(const nlohmann::json& expected_options,
                                                                        const std::string& preset)
{
    std::vector<std::string> mismatches;
    if (!expected_options.is_object() || !wxGetApp().preset_bundle)
        return mismatches;

    const DynamicPrintConfig cfg = wxGetApp().preset_bundle->full_config(false);
    for (auto it = expected_options.begin(); it != expected_options.end(); ++it) {
        const std::string key = OllamaActionExecutor::normalize_config_key(it.key());
        if (!cfg.has(key))
            continue;
        const ConfigOption* opt = cfg.option(key);
        if (!opt)
            continue;
        const std::string live = opt->serialize();
        std::string       expected;
        if (it.value().is_string())
            expected = it.value().get<std::string>();
        else if (it.value().is_boolean())
            expected = it.value().get<bool>() ? "1" : "0";
        else if (it.value().is_number())
            expected = std::to_string(it.value().get<double>());
        else
            expected = it.value().dump();

        if (live != expected && !(live == "1" && expected == "true") && !(live == "0" && expected == "false"))
            mismatches.push_back(key + " (expected " + expected + ", got " + live + ")");
    }
    (void) preset;
    return mismatches;
}

}} // namespace

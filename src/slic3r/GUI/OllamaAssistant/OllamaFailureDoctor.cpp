#include "OllamaFailureDoctor.hpp"

#ifndef OLLAMA_HEADLESS_TEST
#include "../DeviceCore/DevHMS.h"
#include "../DeviceCore/DevManager.h"
#include "../DeviceManager.hpp"
#include "../GUI_App.hpp"
#include "../HMS.hpp"
#endif

#include <cctype>

namespace Slic3r { namespace GUI {

OllamaFailureArea OllamaFailureDoctor::area_for_code(const std::string& code)
{
    std::string hex;
    for (char c : code)
        if (std::isxdigit(static_cast<unsigned char>(c)))
            hex += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (hex.size() < 2)
        return {"unknown", "Unrecognized error code.", {}};

    // The first byte is the Bambu module id (see ModuleID in DevHMS.h).
    const std::string module = hex.substr(0, 2);
    if (module == "07" || module == "12")
        return {"ams_feeding", "Filament feeding from the AMS (tangles, wet or brittle filament, wrong slot).",
                {"filament_type", "nozzle_temperature", "filament_max_volumetric_speed"}};
    if (module == "08")
        return {"toolhead", "Toolhead or nozzle (clog, heat creep, extruder skipping).",
                {"nozzle_temperature", "filament_max_volumetric_speed", "retraction_length", "outer_wall_speed"}};
    if (module == "03")
        return {"motion_first_layer", "Motion system or first layer (bed leveling, adhesion, collisions).",
                {"initial_layer_print_height", "initial_layer_speed", "brim_type", "bed_temperature_initial_layer"}};
    if (module == "0C")
        return {"camera_detection", "Camera detected a failure (spaghetti, first-layer defects, foreign objects).",
                {"enable_support", "brim_type", "initial_layer_speed", "sparse_infill_density"}};
    if (module == "05")
        return {"mainboard_system", "Mainboard, storage or network; usually not a slicer setting.", {}};
    return {"other", "Printer module " + module + "; check the HMS message for details.", {}};
}

nlohmann::json OllamaFailureDoctor::printer_diagnostics_json()
{
    nlohmann::json out = nlohmann::json::object();
#ifndef OLLAMA_HEADLESS_TEST
    DeviceManager* dm  = wxGetApp().getDeviceManager();
    MachineObject* obj = dm ? dm->get_selected_machine() : nullptr;
    if (!obj) {
        out["connected"] = false;
        out["note"]      = "No printer selected. Ask the user to connect one in the Device tab, or diagnose from "
                           "their description and the current settings (get_state).";
        return out;
    }

    HMSQuery* hms_query = wxGetApp().get_hms_query();
    out["connected"]     = obj->is_online();
    out["printer"]       = obj->get_dev_name();
    out["print_status"]  = obj->print_status;
    out["task"]          = obj->subtask_name;
    out["progress_pct"]  = obj->mc_print_percent;
    out["stage"]         = obj->get_curr_stage().utf8_string();

    if (obj->print_error > 0) {
        const std::string code = obj->get_print_error_str();
        nlohmann::json    err  = {{"code", code}};
        if (hms_query)
            err["message"] = hms_query->query_print_error_msg(obj, obj->print_error).utf8_string();
        const OllamaFailureArea area = area_for_code(code);
        err["area"]           = area.area;
        err["area_summary"]   = area.summary_en;
        err["settings_to_check"] = area.setting_keys;
        out["print_error"] = std::move(err);
    }

    nlohmann::json alerts = nlohmann::json::array();
    if (DevHMS* hms = obj->GetHMS()) {
        for (const DevHMSItem& item : hms->GetHMSItems()) {
            const std::string code = item.get_long_error_code();
            nlohmann::json    a    = {{"code", code}, {"level", static_cast<int>(item.get_level())}};
            if (hms_query)
                a["message"] = hms_query->query_hms_msg(obj, code).utf8_string();
            const OllamaFailureArea area = area_for_code(code);
            a["area"]              = area.area;
            a["settings_to_check"] = area.setting_keys;
            alerts.push_back(std::move(a));
        }
    }
    out["hms_alerts"] = std::move(alerts);
    if (!out.contains("print_error") && out["hms_alerts"].empty())
        out["note"] = "The printer reports no active error. Ask what the failure looked like and when it happened.";
#endif
    return out;
}

}} // namespace

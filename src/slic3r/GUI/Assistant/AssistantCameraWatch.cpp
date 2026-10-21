#include "AssistantCameraWatch.hpp"

#include "AssistantChatPanel.hpp"
#include "AssistantPrinterControl.hpp"

#include "../DeviceCore/DevConfig.h"
#include "../DeviceCore/DevManager.h"
#include "../DeviceManager.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../NotificationManager.hpp"
#include "../Plater.hpp"

#include <wx/timer.h>

#include <memory>

namespace Slic3r { namespace GUI {

namespace {

nlohmann::json status_of(MachineObject* obj)
{
    DevConfig* conf = obj->GetConfig();
    nlohmann::json s = nlohmann::json::object();
    auto add = [&s](const char* key, bool supported, bool on) {
        s[key] = supported ? nlohmann::json(on ? "on" : "off") : nlohmann::json("not supported");
    };
    add("ai_print_monitoring", conf && conf->SupportAIMonitor(), obj->xcam_ai_monitoring);
    add("spaghetti_detection", obj->is_support_spaghetti_detection, obj->xcam_spaghetti_detection);
    add("first_layer_inspection", conf && conf->SupportFirstLayerInspect(), obj->xcam_first_layer_inspector);
    add("nozzle_clumping_detection", obj->is_support_nozzleclumping_detection, obj->xcam_nozzleclumping_detection);
    add("air_printing_detection", obj->is_support_airprinting_detection, obj->xcam_airprinting_detection);
    return s;
}

bool all_supported_on(const nlohmann::json& s)
{
    for (auto it = s.begin(); it != s.end(); ++it)
        if (it.value() == "off")
            return false;
    return true;
}

void report(const wxString& text, bool error)
{
    if (AssistantChatPanel* panel = AssistantChatPanel::active_panel()) {
        panel->post_assistant_note(text, error);
        return;
    }
    if (Plater* plater = wxGetApp().plater())
        if (NotificationManager* nm = plater->get_notification_manager())
            nm->push_notification(text.utf8_string());
}

/** Wait up to ~15 s for the printer to report the checks on. */
void confirm_on_printer(const std::string& dev_id, bool lan)
{
    auto* timer = new wxTimer();
    auto  ticks = std::make_shared<int>(0);
    timer->Bind(wxEVT_TIMER, [timer, ticks, dev_id, lan](wxTimerEvent&) {
        auto done = [timer](const wxString& text, bool error) {
            timer->Stop();
            report(text, error);
            wxTheApp->CallAfter([timer]() { delete timer; });
        };
        if (wxGetApp().is_closing()) {
            timer->Stop();
            wxTheApp->CallAfter([timer]() { delete timer; });
            return;
        }
        DeviceManager* dm  = wxGetApp().getDeviceManager();
        MachineObject* obj = dm ? dm->get_selected_machine() : nullptr;
        if (!obj || obj->get_dev_id() != dev_id) {
            done(_L("The printer changed or disconnected before it confirmed the camera checks."), true);
            return;
        }
        if (all_supported_on(status_of(obj))) {
            done(_L("✓ The printer confirmed: its camera now watches the print (AI monitoring and the supported "
                    "failure checks are on). Alerts appear as printer messages."),
                 false);
            return;
        }
        if (++*ticks < 15)
            return;
        done(lan ? _L("The printer did not turn its camera checks on. Check that it is online and in Developer Mode.")
                 : _L("The printer did not turn its camera checks on. It is connected through the Bambu Lab cloud, "
                      "which ignores control from slicers other than Bambu's own apps. Turn them on in Bambu Handy "
                      "or on the printer screen, or connect the printer in LAN mode with Developer Mode."),
             true);
    });
    timer->Start(1000);
}

} // namespace

AssistantActionResult AssistantCameraWatch::apply(const nlohmann::json& action)
{
    AssistantActionResult result;
    DeviceManager* dm  = wxGetApp().getDeviceManager();
    MachineObject* obj = dm ? dm->get_selected_machine() : nullptr;
    if (!obj || !obj->is_online()) {
        result.message = "No printer is connected. Ask the user to connect one in the Device tab.";
        return result;
    }
    const nlohmann::json before = status_of(obj);
    const bool want_on = action.contains("enable") && action["enable"].is_boolean() && action["enable"].get<bool>();
    if (!want_on) {
        result.success = true;
        result.message = nlohmann::json{{"camera_checks", before},
                                        {"note", "These run on the printer's own camera. Problems they find arrive as "
                                                 "HMS messages; read them with printer_diagnostics."}}
                             .dump();
        return result;
    }
    if (all_supported_on(before)) {
        result.success = true;
        result.message = nlohmann::json{{"camera_checks", before}, {"note", "Already on."}}.dump();
        return result;
    }
    if (!AssistantPrinterControl::ensure_consent(wxGetApp().GetTopWindow())) {
        result.message = "The user has not allowed AI printer control, so the camera checks were not changed.";
        return result;
    }

    const std::string sensitivity = "medium";
    DevConfig* conf = obj->GetConfig();
    if (conf && conf->SupportAIMonitor() && !obj->xcam_ai_monitoring)
        obj->command_xcam_control_ai_monitoring(true, sensitivity);
    if (obj->is_support_spaghetti_detection && !obj->xcam_spaghetti_detection)
        obj->command_xcam_control_spaghetti_detection(true, sensitivity);
    if (conf && conf->SupportFirstLayerInspect() && !obj->xcam_first_layer_inspector)
        obj->command_xcam_control_first_layer_inspector(true, false);
    if (obj->is_support_nozzleclumping_detection && !obj->xcam_nozzleclumping_detection)
        obj->command_xcam_control_nozzleclumping_detection(true, sensitivity);
    if (obj->is_support_airprinting_detection && !obj->xcam_airprinting_detection)
        obj->command_xcam_control_airprinting_detection(true, sensitivity);

    confirm_on_printer(obj->get_dev_id(), obj->is_lan_mode_printer());
    result.success = true;
    result.message = "Asked the printer to turn on its camera checks. Waiting for it to confirm; the result will "
                     "appear in the chat.";
    return result;
}

}} // namespace Slic3r::GUI

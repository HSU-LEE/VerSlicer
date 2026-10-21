#include "AssistantPrinterControl.hpp"
#include "AiCatalog.hpp"

#include "AssistantConfig.hpp"

#ifndef ASSISTANT_HEADLESS_TEST
#include "AiLocale.hpp"
#include "../DeviceCore/DevConfig.h"
#include "../DeviceCore/DevDefs.h"
#include "../DeviceCore/DevFan.h"
#include "../DeviceCore/DevManager.h"
#include "../DeviceManager.hpp"
#include "../GUI_App.hpp"
#include "../BambuConnect.hpp"
#include "../DeviceCore/DevBed.h"
#include "../DeviceCore/DevExtruderSystem.h"
#include "AssistantChatPanel.hpp"
#include "../NotificationManager.hpp"
#include "../Plater.hpp"

#include <wx/timer.h>

#include <boost/log/trivial.hpp>
#include <wx/msgdlg.h>
#endif

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

#include <algorithm>
#include <cmath>

namespace Slic3r { namespace GUI {

namespace {

constexpr const char* kPrinterControlKey = "printer_control";

std::optional<int> int_value(const nlohmann::json& action, std::string* error)
{
    if (!action.contains("value")) {
        *error = "printer_control needs a value";
        return std::nullopt;
    }
    const nlohmann::json& v = action["value"];
    double d = 0.0;
    if (v.is_number()) {
        d = v.get<double>();
    } else if (v.is_string()) {
        try {
            size_t idx = 0;
            d = std::stod(v.get<std::string>(), &idx);
            if (idx == 0)
                throw std::invalid_argument("no number");
        } catch (const std::exception&) {
            *error = "printer_control value must be a number";
            return std::nullopt;
        }
    } else {
        *error = "printer_control value must be a number";
        return std::nullopt;
    }
    if (!std::isfinite(d)) {
        *error = "printer_control value must be a number";
        return std::nullopt;
    }
    return static_cast<int>(std::lround(d));
}

std::optional<int> speed_level(const nlohmann::json& v)
{
    if (v.is_number_integer()) {
        const int n = v.get<int>();
        if (n >= 1 && n <= 4)
            return n;
        return std::nullopt;
    }
    if (!v.is_string())
        return std::nullopt;
    std::string s = boost::algorithm::to_lower_copy(boost::algorithm::trim_copy(v.get<std::string>()));
    if (s == "1" || s == "silent")
        return 1;
    if (s == "2" || s == "standard")
        return 2;
    if (s == "3" || s == "sport")
        return 3;
    if (s == "4" || s == "ludicrous")
        return 4;
    return std::nullopt;
}

std::string range_text(int lo, int hi) { return std::to_string(lo) + "-" + std::to_string(hi); }

} // namespace

std::optional<PrinterControlRequest> AssistantPrinterControl::validate(const nlohmann::json& action,
                                                                     const PrinterControlLimits& limits, std::string* error)
{
    std::string scratch;
    if (!error)
        error = &scratch;

    if (!limits.online) {
        *error = "No printer is connected. Ask the user to connect one in the Device tab.";
        return std::nullopt;
    }
    if (!action.contains("target") || !action["target"].is_string()) {
        *error = "printer_control needs target: nozzle_temp, bed_temp, chamber_temp or print_speed";
        return std::nullopt;
    }
    const std::string target = action["target"].get<std::string>();

    if (target == "print_speed") {
        if (!limits.printing) {
            *error = "Print speed can only be changed while the printer is printing.";
            return std::nullopt;
        }
        const auto lvl = action.contains("value") ? speed_level(action["value"]) : std::nullopt;
        if (!lvl) {
            *error = "print_speed value must be silent, standard, sport or ludicrous (1-4)";
            return std::nullopt;
        }
        return PrinterControlRequest{PrinterControlTarget::PrintSpeed, *lvl};
    }

    PrinterControlTarget kind;
    if (target == "nozzle_temp")
        kind = PrinterControlTarget::NozzleTemp;
    else if (target == "bed_temp")
        kind = PrinterControlTarget::BedTemp;
    else if (target == "chamber_temp")
        kind = PrinterControlTarget::ChamberTemp;
    else {
        *error = "Unknown printer_control target: " + target + " (allowed: nozzle_temp, bed_temp, chamber_temp, print_speed)";
        return std::nullopt;
    }

    const auto temp = int_value(action, error);
    if (!temp)
        return std::nullopt;
    const int t = *temp;

    // 0 always means "heater off" and is allowed for every heater.
    switch (kind) {
    case PrinterControlTarget::NozzleTemp:
        if (t != 0 && (t < limits.nozzle_min || t > limits.nozzle_max)) {
            *error = "Nozzle temperature must be 0 (off) or " + range_text(limits.nozzle_min, limits.nozzle_max) + " °C on this printer.";
            return std::nullopt;
        }
        break;
    case PrinterControlTarget::BedTemp:
        if (t < 0 || t > limits.bed_max) {
            *error = "Bed temperature must be 0-" + std::to_string(limits.bed_max) + " °C on this printer.";
            return std::nullopt;
        }
        break;
    case PrinterControlTarget::ChamberTemp:
        if (!limits.chamber_editable) {
            *error = "This printer's chamber temperature cannot be set.";
            return std::nullopt;
        }
        if (t != 0 && (t < limits.chamber_min || t > limits.chamber_max)) {
            *error = "Chamber temperature must be 0 (off) or " + range_text(limits.chamber_min, limits.chamber_max) + " °C on this printer.";
            return std::nullopt;
        }
        if (limits.chamber_cooling_while_printing) {
            *error = "Chamber temperature cannot be changed in cooling mode while printing.";
            return std::nullopt;
        }
        if (t >= limits.chamber_heat_switch && !limits.chamber_heating_mode) {
            // The printer would switch its air duct to heating mode; the printer
            // panel asks the user to confirm that, so the AI does not do it.
            *error = "Setting the chamber to " + std::to_string(limits.chamber_heat_switch) +
                     " °C or more switches the printer to heating mode. The user must do that in the Device tab.";
            return std::nullopt;
        }
        break;
    default: break;
    }
    return PrinterControlRequest{kind, t};
}

std::string AssistantPrinterControl::describe(const PrinterControlRequest& req, bool korean)
{
    static const char* kSpeedEn[] = {"", "silent", "standard", "sport", "ludicrous"};
    const std::string v = std::to_string(req.value);
    switch (req.target) {
    case PrinterControlTarget::NozzleTemp:
        return req.value == 0 ? ai_catalog_text("nozzle heater off")
                              : (boost::format(ai_catalog_text("nozzle temperature %s °C")) % v).str();
    case PrinterControlTarget::BedTemp:
        return req.value == 0 ? ai_catalog_text("bed heater off")
                              : (boost::format(ai_catalog_text("bed temperature %s °C")) % v).str();
    case PrinterControlTarget::ChamberTemp:
        return req.value == 0 ? ai_catalog_text("chamber heater off")
                              : (boost::format(ai_catalog_text("chamber temperature %s °C")) % v).str();
    case PrinterControlTarget::PrintSpeed:
        return (boost::format(ai_catalog_text("print speed %s")) % ai_catalog_text(kSpeedEn[req.value])).str();
    }
    return {};
}

#ifndef ASSISTANT_HEADLESS_TEST

bool AssistantPrinterControl::enabled()
{
    AppConfig* cfg = wxGetApp().app_config;
    if (!cfg)
        return false;
    // AppConfig writes "1" to disk as JSON true and reads it back as "true".
    const std::string v = cfg->get(kAssistantConfigSection, kPrinterControlKey);
    return v == "1" || v == "true";
}

void AssistantPrinterControl::set_enabled(bool on)
{
    AppConfig* cfg = wxGetApp().app_config;
    if (!cfg)
        return;
    cfg->set(kAssistantConfigSection, kPrinterControlKey, on ? "1" : "0");
    cfg->save();
}

bool AssistantPrinterControl::ensure_consent(wxWindow* parent)
{
    if (enabled())
        return true;
    const bool ko = AiLocale::korean();
    const wxString body = AiLocale::text("Allow the AI assistant to change these values on your connected printer?\n\n• Nozzle, bed and chamber target temperature (only within the printer's limits)\n• Print speed level while printing (silent, standard, sport, ludicrous)\n\nThe AI cannot start or stop prints, move axes, or send files to the printer. Every change is shown in the chat.\n\nYou can turn this off later in Smart Print > Allow AI printer control.");
    wxMessageDialog dlg(parent, body, AiLocale::text("AI printer control"),
                        wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
    dlg.SetYesNoLabels(AiLocale::text("Allow"), AiLocale::text("Don't allow"));
    if (dlg.ShowModal() != wxID_YES)
        return false;
    set_enabled(true);
    return true;
}

namespace {

/** Show a printer-control result in the chat, or as an app notification when
 *  the chat panel is closed, so the outcome is never lost. */
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

/** What the printer itself reports for `target` right now. */
std::optional<int> reported_value(MachineObject* obj, PrinterControlTarget target)
{
    switch (target) {
    case PrinterControlTarget::NozzleTemp:
        if (auto* ext = obj->GetExtderSystem())
            return ext->GetNozzleTempTarget(MAIN_EXTRUDER_ID);
        return std::nullopt;
    case PrinterControlTarget::BedTemp:
        if (auto* bed = obj->GetBed())
            return static_cast<int>(std::lround(bed->GetBedTempTarget()));
        return std::nullopt;
    case PrinterControlTarget::ChamberTemp: return static_cast<int>(std::lround(obj->chamber_temp_target));
    case PrinterControlTarget::PrintSpeed: return static_cast<int>(obj->GetPrintingSpeedLevel());
    }
    return std::nullopt;
}

bool matches(const PrinterControlRequest& req, int reported)
{
    return req.target == PrinterControlTarget::PrintSpeed ? reported == req.value : std::abs(reported - req.value) <= 1;
}

/** Poll the printer's own report for up to ~15 s and tell the user what really
 *  happened. Cloud printers with Authorization Control firmware silently drop
 *  control commands from other slicers, so "sent" is never reported as "done". */
void verify_on_printer(const std::string& dev_id, PrinterControlRequest req, bool lan, std::optional<int> before)
{
    // Already at the value: a match would not prove the printer accepted anything.
    if (before && matches(req, *before)) {
        // After the reply that is being built now, so the chat reads in order.
        const wxString text = wxString::Format(_L("The printer already reports %s, so nothing changed."),
                                               wxString::FromUTF8(AssistantPrinterControl::describe(req, true)));
        wxTheApp->CallAfter([text]() { report(text, false); });
        return;
    }
    auto* timer = new wxTimer();
    auto  ticks = std::make_shared<int>(0);
    timer->Bind(wxEVT_TIMER, [timer, ticks, dev_id, req, lan](wxTimerEvent&) {
        auto finish = [timer](const wxString& text, bool error) {
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
            finish(_L("The printer changed or disconnected before it confirmed the change."), true);
            return;
        }
        const auto now = reported_value(obj, req.target);
        const wxString what = wxString::FromUTF8(AssistantPrinterControl::describe(req, true));
        if (now && matches(req, *now)) {
            finish(wxString::Format(_L("✓ The printer confirmed: %s."), what), false);
            return;
        }
        if (++*ticks < 15)
            return;
        if (lan)
            finish(wxString::Format(_L("The printer did not apply %s. Check that it is online and in Developer Mode, then try again."), what), true);
        else
            finish(wxString::Format(_L("The printer did not apply %s. It is connected through the Bambu Lab cloud, and its firmware "
                                       "(Authorization Control) ignores control commands from slicers other than Bambu's own apps.\n\n"
                                       "To let VerSlicer control it, turn on LAN Only mode and Developer Mode on the printer and add it "
                                       "here in LAN mode. Otherwise change the value on the printer's screen."), what), true);
    });
    timer->Start(1000);
}

} // namespace

AssistantActionResult AssistantPrinterControl::apply(const nlohmann::json& action)
{
    AssistantActionResult result;
    const bool ko = AiLocale::korean();

    if (!ensure_consent(wxGetApp().GetTopWindow())) {
        result.message = "The user has not allowed AI printer control. Tell them what to change and where (Device tab).";
        return result;
    }

    DeviceManager* dm  = wxGetApp().getDeviceManager();
    MachineObject* obj = dm ? dm->get_selected_machine() : nullptr;

    PrinterControlLimits limits;
    if (obj) {
        limits.online   = obj->is_online();
        limits.printing = obj->is_in_printing();
        if (obj->nozzle_temp_range.size() >= 2) {
            limits.nozzle_min = obj->nozzle_temp_range[0];
            limits.nozzle_max = obj->nozzle_temp_range[1];
        }
        limits.bed_max = obj->bed_temp_range.size() > 1 ? obj->bed_temp_range[1] : obj->get_bed_temperature_limit();
        if (DevConfig* conf = obj->GetConfig()) {
            limits.chamber_editable    = conf->SupportChamberEdit();
            limits.chamber_min         = conf->GetChamberTempEditMin();
            limits.chamber_max         = conf->GetChamberTempEditMax();
            limits.chamber_heat_switch = conf->GetChamberTempSwitchHeat();
        }
        if (auto* fan = obj->GetFan()) {
            limits.chamber_heating_mode           = fan->is_at_heating_mode();
            limits.chamber_cooling_while_printing = limits.printing && fan->GetSupportAirduct() && fan->is_at_cooling_mode();
        }
    }

    std::string error;
    const auto req = validate(action, limits, &error);
    if (!req) {
        result.message = error;
        return result;
    }

    const std::optional<int> before = reported_value(obj, req->target);
    int rc = 0;
    switch (req->target) {
    case PrinterControlTarget::NozzleTemp:
        rc = obj->is_enable_np ? obj->command_set_nozzle_new(MAIN_EXTRUDER_ID, req->value) : obj->command_set_nozzle(req->value);
        break;
    case PrinterControlTarget::BedTemp: rc = obj->command_set_bed(req->value); break;
    case PrinterControlTarget::ChamberTemp: rc = obj->command_set_chamber(req->value); break;
    case PrinterControlTarget::PrintSpeed:
        rc = obj->command_set_printing_speed(static_cast<DevPrintingSpeedLevel>(req->value));
        break;
    }
    BOOST_LOG_TRIVIAL(info) << "AI printer control: " << describe(*req, false) << " rc=" << rc;
    if (rc != 0) {
        result.message = "The printer did not accept the command (" + describe(*req, false) + ").";
        return result;
    }
    // Sent is not done: the printer's own report decides, and the result
    // follows in the chat (see verify_on_printer).
    verify_on_printer(obj->get_dev_id(), *req, obj->is_lan_mode_printer(), before);
    result.success          = true;
    result.effective_change = false;
    result.message          = (boost::format(ai_catalog_text("Sent to the printer: %s. Waiting for the printer to confirm; "
                                                             "the result will appear in the chat.")) % describe(*req, ko)).str();
    return result;
}

#endif

}} // namespace

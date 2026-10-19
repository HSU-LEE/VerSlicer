#include "OllamaPrinterControl.hpp"

#include "OllamaConfig.hpp"

#ifndef OLLAMA_HEADLESS_TEST
#include "AiLocale.hpp"
#include "../DeviceCore/DevConfig.h"
#include "../DeviceCore/DevDefs.h"
#include "../DeviceCore/DevFan.h"
#include "../DeviceCore/DevManager.h"
#include "../DeviceManager.hpp"
#include "../GUI_App.hpp"

#include <boost/log/trivial.hpp>
#include <wx/msgdlg.h>
#endif

#include <boost/algorithm/string.hpp>

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
    if (s == "1" || s == "silent" || s == "quiet" || s == "저소음" || s == "조용히")
        return 1;
    if (s == "2" || s == "standard" || s == "normal" || s == "표준" || s == "보통")
        return 2;
    if (s == "3" || s == "sport" || s == "fast" || s == "스포츠" || s == "빠르게")
        return 3;
    if (s == "4" || s == "ludicrous" || s == "rampage" || s == "루디크러스" || s == "최고속")
        return 4;
    return std::nullopt;
}

std::string range_text(int lo, int hi) { return std::to_string(lo) + "-" + std::to_string(hi); }

} // namespace

std::optional<PrinterControlRequest> OllamaPrinterControl::validate(const nlohmann::json& action,
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

std::string OllamaPrinterControl::describe(const PrinterControlRequest& req, bool korean)
{
    static const char* kSpeedEn[] = {"", "silent", "standard", "sport", "ludicrous"};
    static const char* kSpeedKo[] = {"", "저소음", "표준", "스포츠", "루디크러스"};
    const std::string v = std::to_string(req.value);
    switch (req.target) {
    case PrinterControlTarget::NozzleTemp:
        return req.value == 0 ? (korean ? "노즐 히터 끄기" : "nozzle heater off")
                              : (korean ? "노즐 온도 " + v + "°C" : "nozzle temperature " + v + " °C");
    case PrinterControlTarget::BedTemp:
        return req.value == 0 ? (korean ? "베드 히터 끄기" : "bed heater off")
                              : (korean ? "베드 온도 " + v + "°C" : "bed temperature " + v + " °C");
    case PrinterControlTarget::ChamberTemp:
        return req.value == 0 ? (korean ? "챔버 히터 끄기" : "chamber heater off")
                              : (korean ? "챔버 온도 " + v + "°C" : "chamber temperature " + v + " °C");
    case PrinterControlTarget::PrintSpeed:
        return korean ? std::string("출력 속도 ") + kSpeedKo[req.value] : std::string("print speed ") + kSpeedEn[req.value];
    }
    return {};
}

#ifndef OLLAMA_HEADLESS_TEST

bool OllamaPrinterControl::enabled()
{
    AppConfig* cfg = wxGetApp().app_config;
    if (!cfg)
        return false;
    // AppConfig writes "1" to disk as JSON true and reads it back as "true".
    const std::string v = cfg->get(kOllamaConfigSection, kPrinterControlKey);
    return v == "1" || v == "true";
}

void OllamaPrinterControl::set_enabled(bool on)
{
    AppConfig* cfg = wxGetApp().app_config;
    if (!cfg)
        return;
    cfg->set(kOllamaConfigSection, kPrinterControlKey, on ? "1" : "0");
    cfg->save();
}

bool OllamaPrinterControl::ensure_consent(wxWindow* parent)
{
    if (enabled())
        return true;
    const bool ko = AiLocale::korean();
    const wxString body = ko
        ? wxString::FromUTF8(
              "AI 도우미가 연결된 프린터의 다음 값을 직접 바꾸도록 허용할까요?\n\n"
              "• 노즐·베드·챔버 목표 온도 (프린터가 허용하는 범위 안에서만)\n"
              "• 출력 중 속도 단계 (저소음·표준·스포츠·루디크러스)\n\n"
              "AI는 출력을 시작·중지하거나, 축을 움직이거나, 파일을 프린터로 보낼 수 없습니다. "
              "바꾼 내용은 대화창에 표시됩니다.\n\n"
              "나중에 「Smart Print」 메뉴의 「AI 프린터 제어 허용」에서 끌 수 있습니다.")
        : wxString(
              "Allow the AI assistant to change these values on your connected printer?\n\n"
              "• Nozzle, bed and chamber target temperature (only within the printer's limits)\n"
              "• Print speed level while printing (silent, standard, sport, ludicrous)\n\n"
              "The AI cannot start or stop prints, move axes, or send files to the printer. "
              "Every change is shown in the chat.\n\n"
              "You can turn this off later in Smart Print > Allow AI printer control.");
    wxMessageDialog dlg(parent, body, ko ? wxString::FromUTF8("AI 프린터 제어") : wxString("AI printer control"),
                        wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
    dlg.SetYesNoLabels(ko ? wxString::FromUTF8("허용") : wxString("Allow"), ko ? wxString::FromUTF8("허용 안 함") : wxString("Don't allow"));
    if (dlg.ShowModal() != wxID_YES)
        return false;
    set_enabled(true);
    return true;
}

OllamaActionResult OllamaPrinterControl::apply(const nlohmann::json& action)
{
    OllamaActionResult result;
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
    result.success          = true;
    result.effective_change = true;
    result.message          = (ko ? "프린터 설정 변경: " : "Printer set: ") + describe(*req, ko);
    return result;
}

#endif

}} // namespace

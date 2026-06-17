#include "PrintReadinessGate.hpp"
#include "SlicePilotSetupHub.hpp"

#include "../DeviceCore/DevManager.h"
#include "../GUI_App.hpp"
#include "../GUI_Utils.hpp"
#include "../I18N.hpp"
#include "../MainFrame.hpp"
#include "../Plater.hpp"
#include "BambuSmartPrintService.hpp"
#include "FirstPrintExperience.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PresetBundle.hpp"

#include <wx/msgdlg.h>

#include <cctype>

namespace Slic3r { namespace GUI {

bool PrintReadinessGate::network_plugin_ready()
{
    if (!wxGetApp().app_config || !wxGetApp().app_config->get_bool("installed_networking"))
        return false;
    return wxGetApp().getAgent() != nullptr;
}

bool PrintReadinessGate::has_bound_printer()
{
    ::Slic3r::DeviceManager* dm = wxGetApp().getDeviceManager();
    if (!dm)
        return false;
    return dm->get_selected_machine() != nullptr;
}

bool PrintReadinessGate::try_fix_filament_mismatch(Plater* plater)
{
    if (!plater || !wxGetApp().preset_bundle)
        return false;

    const auto& ready = BambuSmartPrintService::instance().last_readiness_report();
    if (!ready.filament_mismatch)
        return false;

    PresetBundle* bundle = wxGetApp().preset_bundle;
    const std::string hint = ready.suggested_filament_hint.empty()
        ? ready.active_filament_hint
        : ready.suggested_filament_hint;
    if (hint.empty())
        return false;

    auto matches_hint = [&](const std::string& preset_name) {
        if (preset_name.empty())
            return false;
        std::string lower = preset_name;
        std::string h = hint;
        for (char& c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : h)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return lower.find(h) != std::string::npos || h.find(lower) != std::string::npos;
    };

    const Preset* best = nullptr;
    for (const Preset& p : bundle->filaments) {
        if (!p.is_compatible)
            continue;
        if (matches_hint(p.name)) {
            best = &p;
            break;
        }
    }
    if (!best)
        return false;

    bundle->filaments.select_preset_by_name(best->name, true);
    plater->on_filament_change(0);
    plater->update();
    BambuSmartPrintService::instance().refresh_plate_snapshot(plater);
    BambuSmartPrintService::instance().refresh_all_panels();
    return true;
}

wxString PrintReadinessGate::filament_fix_button_label()
{
    const auto& ready = BambuSmartPrintService::instance().last_readiness_report();
    if (!ready.suggested_filament_hint.empty())
        return wxString::Format(_L("Use %s preset"), wxString::FromUTF8(ready.suggested_filament_hint));
    return _L("Fix filament preset");
}

PrintGateResult PrintReadinessGate::run(Plater* plater, wxWindow* parent)
{
    if (!plater) {
        show_error(parent, _L("No project is open."));
        return PrintGateResult::Cancelled;
    }

    if (!BambuSmartPrintService::is_enabled())
        BambuSmartPrintService::set_enabled(true);

    if (SlicePilotSetupHub::is_enabled()) {
        bool slice_only = false;
        if (SlicePilotSetupHub::handle_print_gate(plater, parent, slice_only))
            return slice_only ? PrintGateResult::ProceedSliceOnly : PrintGateResult::Proceed;
        return PrintGateResult::Cancelled;
    }

    if (!BambuSmartPrintService::is_bbl_printer_active()) {
        wxMessageDialog dlg(parent, BambuSmartPrintService::bbl_printer_required_message(),
                            _L("Bambu Lab printer required"), wxYES_NO | wxICON_WARNING);
        dlg.SetYesNoLabels(_L("Switch to Bambu profile"), _L("Cancel"));
        if (dlg.ShowModal() == wxID_YES
            && !BambuSmartPrintService::try_activate_bbl_printer_profile(plater))
            show_error(parent, _L("No compatible Bambu Lab printer profile was found."));
        if (!BambuSmartPrintService::is_bbl_printer_active())
            return PrintGateResult::Cancelled;
    }

    if (plater->model().objects.empty()) {
        wxMessageDialog dlg(parent,
            _L("No model on the build plate yet.\n\nLoad the bundled sample cube and try Print again?"),
            _L("Sample model"), wxYES_NO | wxICON_QUESTION);
        dlg.SetYesNoLabels(_L("Load sample"), _L("Cancel"));
        if (dlg.ShowModal() != wxID_YES)
            return PrintGateResult::Cancelled;
        if (!FirstPrintExperience::open_first_print_sample(plater))
            return PrintGateResult::Cancelled;
    }

    bool slice_only = false;
    if (!network_plugin_ready()) {
        wxMessageDialog dlg(parent,
            _L("The Bambu Network plug-in is not installed.\n\n"
               "You can slice and save G-code now, or install the plug-in to send to the printer."),
            _L("Network plug-in"), wxYES_NO | wxCANCEL | wxICON_INFORMATION);
        dlg.SetYesNoCancelLabels(_L("Install plug-in"), _L("Slice only"), _L("Cancel"));
        const int rc = dlg.ShowModal();
        if (rc == wxID_CANCEL)
            return PrintGateResult::Cancelled;
        if (rc == wxID_YES) {
            wxGetApp().ShowDownNetPluginDlg();
            if (!network_plugin_ready())
                return PrintGateResult::Cancelled;
        } else {
            slice_only = true;
        }
    }

    if (!slice_only && network_plugin_ready() && !has_bound_printer()) {
        wxMessageDialog dlg(parent,
            _L("No printer is selected in the Device tab.\n\n"
               "Bind or select your printer, then press Print again."),
            _L("Printer not connected"), wxYES_NO | wxICON_INFORMATION);
        dlg.SetYesNoLabels(_L("Open Device tab"), _L("Cancel"));
        if (dlg.ShowModal() == wxID_YES && wxGetApp().mainframe)
            wxGetApp().mainframe->jump_to_monitor();
        return PrintGateResult::Cancelled;
    }

    return slice_only ? PrintGateResult::ProceedSliceOnly : PrintGateResult::Proceed;
}

}} // namespace Slic3r::GUI

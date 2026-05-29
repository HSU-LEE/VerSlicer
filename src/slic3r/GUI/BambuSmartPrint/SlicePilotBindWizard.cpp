#include "SlicePilotBindWizard.hpp"
#include "SlicePilotOnboardingFunnel.hpp"
#include "BambuSmartPrintService.hpp"
#include "PrintReadinessGate.hpp"

#include "../DeviceCore/DevManager.h"
#include "../DeviceManager.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../MainFrame.hpp"
#include "../Plater.hpp"
#include "../SelectMachinePop.hpp"
#include "../Widgets/Button.hpp"
#include "../Widgets/Label.hpp"
#include "../GUI_Utils.hpp"
#include "../wxExtensions.hpp"

#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/choicdlg.h>

namespace Slic3r { namespace GUI {

namespace {

class BindWizardDialog : public DPIDialog
{
public:
    BindWizardDialog(wxWindow* parent, Plater* plater)
        : DPIDialog(parent, wxID_ANY, _L("Connect your printer"), wxDefaultPosition,
                    wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
        , m_plater(plater)
    {
        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* intro = new wxStaticText(this, wxID_ANY,
            _L("Choose how to connect your Bambu Lab printer.\n"
               "A connected printer enables send and Smart Print failure learning."));
        intro->Wrap(FromDIP(420));
        intro->SetFont(Label::Body_12);
        root->Add(intro, 0, wxALL, FromDIP(12));

        auto* cloud_btn = new Button(this, _L("Sign in with Bambu account"));
        cloud_btn->SetMinSize(FromDIP(wxSize(280, 36)));
        root->Add(cloud_btn, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT, FromDIP(12));

        auto* scan_btn = new Button(this, _L("Pick from network scan"));
        scan_btn->SetMinSize(FromDIP(wxSize(280, 36)));
        root->Add(scan_btn, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxLEFT | wxRIGHT, FromDIP(8));

        auto* device_btn = new Button(this, _L("Open Device tab (LAN / advanced)"));
        device_btn->SetMinSize(FromDIP(wxSize(280, 36)));
        root->Add(device_btn, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxLEFT | wxRIGHT, FromDIP(8));

        auto* cancel_btn = new Button(this, _L("Cancel"));
        root->Add(cancel_btn, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, FromDIP(12));

        SetSizer(root);
        Fit();
        wxGetApp().UpdateDlgDarkUI(this);

        cloud_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            EndModal(wxID_YES);
        });
        scan_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            EndModal(wxID_OK);
        });
        device_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            EndModal(wxID_APPLY);
        });
        cancel_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            EndModal(wxID_CANCEL);
        });
    }

    void on_dpi_changed(const wxRect& /*suggested_rect*/) override { Fit(); }

    Plater* m_plater;
};

static bool pick_from_network_scan(Plater* plater, wxWindow* parent)
{
    BambuSmartPrintService::instance().scan_bambu_printers_on_network();

    ::Slic3r::DeviceManager* dm = wxGetApp().getDeviceManager();
    if (!dm) {
        show_error(parent, _L("Install the Bambu Network plug-in first."));
        return false;
    }

    const auto machines = dm->get_my_machine_list();
    if (machines.empty()) {
        wxMessageDialog dlg(parent,
            _L("No printers found on the network.\n\n"
               "Make sure your printer is on and signed in to the same account, "
               "or use LAN mode from the Device tab."),
            _L("No printers found"), wxOK | wxICON_INFORMATION);
        dlg.ShowModal();
        return false;
    }

    wxArrayString choices;
    std::vector<MachineObject*> ordered;
    ordered.reserve(machines.size());
    for (const auto& kv : machines) {
        if (!kv.second)
            continue;
        ordered.push_back(kv.second);
        wxString name = kv.second->get_dev_name().empty()
            ? wxString::FromUTF8(kv.second->get_dev_id())
            : wxString::FromUTF8(kv.second->get_dev_name());
        choices.Add(name);
    }
    if (ordered.empty())
        return false;

    wxSingleChoiceDialog pick(parent, _L("Select a printer:"), _L("Network scan"), choices);
    if (pick.ShowModal() != wxID_OK)
        return false;

    const int idx = pick.GetSelection();
    if (idx < 0 || idx >= int(ordered.size()))
        return false;

    dm->set_selected_machine(ordered[idx]->get_dev_id());
    if (plater)
        BambuSmartPrintService::instance().load_printer_profile_for_selected_device(plater);
    SlicePilotOnboardingFunnel::record_printer_bound();
    return PrintReadinessGate::has_bound_printer();
}

} // namespace

bool SlicePilotBindWizard::run(Plater* plater, wxWindow* parent)
{
    if (PrintReadinessGate::has_bound_printer()) {
        SlicePilotOnboardingFunnel::record_printer_bound();
        return true;
    }

    wxWindow* dlg_parent = parent;
    if (!dlg_parent)
        dlg_parent = plater ? static_cast<wxWindow*>(plater)
                            : static_cast<wxWindow*>(wxGetApp().mainframe);

    BindWizardDialog dlg(dlg_parent, plater);
    const int rc = dlg.ShowModal();

    if (rc == wxID_CANCEL)
        return false;

    if (rc == wxID_YES) {
        BambuSmartPrintService::instance().prompt_bambu_login(dlg_parent);
        if (PrintReadinessGate::has_bound_printer()) {
            SlicePilotOnboardingFunnel::record_printer_bound();
            return true;
        }
        return pick_from_network_scan(plater, dlg_parent);
    }

    if (rc == wxID_OK)
        return pick_from_network_scan(plater, dlg_parent);

    if (rc == wxID_APPLY) {
        if (wxGetApp().mainframe)
            wxGetApp().mainframe->jump_to_monitor();
        return PrintReadinessGate::has_bound_printer();
    }

    return false;
}

}} // namespace Slic3r::GUI

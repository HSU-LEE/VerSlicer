#include "SlicePilotSetupHub.hpp"
#include "SlicePilotBindWizard.hpp"
#include "SlicePilotOnboardingFunnel.hpp"
#include "BambuSmartPrintService.hpp"
#include "BambuSmartPrintUi.hpp"
#include "FirstPrintExperience.hpp"
#include "PrintReadinessGate.hpp"

#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../MainFrame.hpp"
#include "../Plater.hpp"
#include "../Widgets/Button.hpp"
#include "../Widgets/Label.hpp"

#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statline.h>

namespace Slic3r { namespace GUI {

using namespace SlicePilotUi;

namespace {

static constexpr const char* kHubEnabledKey = "slicepilot_setup_hub_enabled";

SlicePilotSetupHub* g_active_hub = nullptr;

static wxString step_label(int i)
{
    switch (i) {
    case 0: return _L("1 Printer");
    case 1: return _L("2 Plug-in");
    case 2: return _L("3 Connect");
    case 3: return _L("4 Model");
    default: return wxString{};
    }
}

} // namespace

bool SlicePilotSetupHub::is_enabled()
{
    if (!wxGetApp().app_config)
        return true;
    if (!wxGetApp().app_config->has("smart_print", kHubEnabledKey))
        return true;
    return wxGetApp().app_config->get("smart_print", kHubEnabledKey) != "0";
}

bool SlicePilotSetupHub::step_complete(SetupHubStep step)
{
    switch (step) {
    case SetupHubStep::Printer:
        return BambuSmartPrintService::is_bbl_printer_active();
    case SetupHubStep::Plugin:
        return PrintReadinessGate::network_plugin_ready();
    case SetupHubStep::Connect:
        return PrintReadinessGate::has_bound_printer();
    case SetupHubStep::Model:
        if (Plater* plater = wxGetApp().plater()) {
            try {
                return !plater->model().objects.empty();
            } catch (...) {
            }
        }
        return false;
    default:
        return false;
    }
}

int SlicePilotSetupHub::completed_count()
{
    int n = 0;
    for (int i = 0; i < int(SetupHubStep::Count); ++i) {
        if (step_complete(static_cast<SetupHubStep>(i)))
            ++n;
    }
    return n;
}

wxString SlicePilotSetupHub::print_button_label()
{
    if (!is_enabled())
        return _L("Print");
    const int done = completed_count();
    if (done >= int(SetupHubStep::Count))
        return _L("Print");
    return wxString::Format(_L("Print (%d/4 ready)"), done);
}

void SlicePilotSetupHub::refresh_all(Plater* plater)
{
    if (g_active_hub)
        g_active_hub->refresh(plater);
}

void SlicePilotSetupHub::highlight_step(SetupHubStep step)
{
    if (!g_active_hub)
        return;
    if (step >= SetupHubStep::Count)
        return;
    if (Button* btn = g_active_hub->m_step_btns[int(step)]) {
        btn->SetFocus();
        btn->SetBackgroundColour(Theme::border());
        btn->Refresh();
    }
}

void SlicePilotSetupHub::activate_step(SetupHubStep step, Plater* plater)
{
    if (!plater)
        return;

    switch (step) {
    case SetupHubStep::Printer:
        if (!BambuSmartPrintService::try_activate_bbl_printer_profile(plater)) {
            wxGetApp().ShowUserGuide();
        }
        break;
    case SetupHubStep::Plugin:
        wxGetApp().ShowDownNetPluginDlg();
        if (PrintReadinessGate::network_plugin_ready())
            SlicePilotOnboardingFunnel::record_plugin_installed();
        break;
    case SetupHubStep::Connect:
        SlicePilotBindWizard::run(plater);
        break;
    case SetupHubStep::Model:
        FirstPrintExperience::open_first_print_sample(plater);
        break;
    default:
        break;
    }
    refresh_all(plater);
    BambuSmartPrintService::instance().refresh_all_panels();
}

bool SlicePilotSetupHub::handle_print_gate(Plater* plater, wxWindow* parent, bool& slice_only)
{
    slice_only = false;
    if (!is_enabled() || !plater)
        return false;

    if (!step_complete(SetupHubStep::Printer)) {
        highlight_step(SetupHubStep::Printer);
        wxMessageDialog dlg(parent, BambuSmartPrintService::bbl_printer_required_message(),
                            _L("Bambu Lab printer required"), wxYES_NO | wxICON_WARNING);
        dlg.SetYesNoLabels(_L("Set up printer"), _L("Cancel"));
        if (dlg.ShowModal() == wxID_YES)
            activate_step(SetupHubStep::Printer, plater);
        return step_complete(SetupHubStep::Printer);
    }

    if (!step_complete(SetupHubStep::Model)) {
        highlight_step(SetupHubStep::Model);
        wxMessageDialog dlg(parent,
            _L("No model on the build plate yet.\n\nLoad the bundled sample cube and try Print again?"),
            _L("Sample model"), wxYES_NO | wxICON_QUESTION);
        dlg.SetYesNoLabels(_L("Load sample"), _L("Cancel"));
        if (dlg.ShowModal() != wxID_YES)
            return false;
        if (!FirstPrintExperience::open_first_print_sample(plater))
            return false;
    }

    if (!step_complete(SetupHubStep::Plugin)) {
        highlight_step(SetupHubStep::Plugin);
        wxMessageDialog dlg(parent,
            _L("The Bambu Network plug-in is not installed.\n\n"
               "You can slice and save G-code now, or install the plug-in to send to the printer."),
            _L("Network plug-in"), wxYES_NO | wxCANCEL | wxICON_INFORMATION);
        dlg.SetYesNoCancelLabels(_L("Install plug-in"), _L("Slice only"), _L("Cancel"));
        const int rc = dlg.ShowModal();
        if (rc == wxID_CANCEL)
            return false;
        if (rc == wxID_YES) {
            wxGetApp().ShowDownNetPluginDlg();
            if (PrintReadinessGate::network_plugin_ready())
                SlicePilotOnboardingFunnel::record_plugin_installed();
            if (!PrintReadinessGate::network_plugin_ready())
                return false;
        } else {
            slice_only = true;
            return true;
        }
    }

    if (!slice_only && !step_complete(SetupHubStep::Connect)) {
        highlight_step(SetupHubStep::Connect);
        wxMessageDialog dlg(parent,
            _L("No printer is connected yet.\n\n"
               "Connect your Bambu printer to send jobs and enable failure learning."),
            _L("Printer not connected"), wxYES_NO | wxICON_INFORMATION);
        dlg.SetYesNoLabels(_L("Connect printer"), _L("Cancel"));
        if (dlg.ShowModal() == wxID_YES)
            SlicePilotBindWizard::run(plater, parent);
        return step_complete(SetupHubStep::Connect) || slice_only;
    }

    return true;
}

SlicePilotSetupHub::SlicePilotSetupHub(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    g_active_hub = this;
    SetBackgroundColour(prepare_strip_background());

    auto* root = new wxBoxSizer(wxHORIZONTAL);
    const int gap = FromDIP(3);

    for (int i = 0; i < int(SetupHubStep::Count); ++i) {
        m_step_btns[i] = new Button(this, step_label(i));
        style_prepare_toolbar_button(m_step_btns[i], false);
        m_step_btns[i]->SetFont(Label::Body_10);
        m_step_btns[i]->SetMinSize(FromDIP(wxSize(72, 22)));
        root->Add(m_step_btns[i], 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
    }

    auto* sep = new wxStaticLine(this, wxID_ANY, wxDefaultPosition,
                                 FromDIP(wxSize(1, 14)), wxLI_VERTICAL);
    sep->SetForegroundColour(Theme::border());
    root->Add(sep, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

    m_summary = new wxStaticText(this, wxID_ANY, wxString{});
    m_summary->SetFont(Label::Body_10);
    m_summary->SetForegroundColour(Theme::text_muted());
    root->Add(m_summary, 0, wxALIGN_CENTER_VERTICAL);

    SetSizer(root);
    bind_step_buttons();
    refresh(wxGetApp().plater());
}

SlicePilotSetupHub::~SlicePilotSetupHub()
{
    if (g_active_hub == this)
        g_active_hub = nullptr;
}

void SlicePilotSetupHub::bind_step_buttons()
{
    for (int i = 0; i < int(SetupHubStep::Count); ++i) {
        m_step_btns[i]->Bind(wxEVT_BUTTON, [i](wxCommandEvent&) {
            if (Plater* plater = wxGetApp().plater())
                activate_step(static_cast<SetupHubStep>(i), plater);
        });
    }
}

void SlicePilotSetupHub::update_step_buttons(Plater* /*plater*/)
{
    for (int i = 0; i < int(SetupHubStep::Count); ++i) {
        const bool done = step_complete(static_cast<SetupHubStep>(i));
        wxString label = done ? (wxString::FromUTF8("✓ ") + step_label(i).Mid(2))
                              : (wxString::FromUTF8("○ ") + step_label(i).Mid(2));
        m_step_btns[i]->SetLabel(label);
        m_step_btns[i]->SetBackgroundColour(done ? Theme::border() : prepare_strip_background());
    }
    if (m_summary) {
        const int done = completed_count();
        if (done >= int(SetupHubStep::Count))
            m_summary->SetLabel(_L("Ready to Print"));
        else
            m_summary->SetLabel(wxString::Format(_L("%d/4 setup steps"), done));
    }
}

void SlicePilotSetupHub::refresh(Plater* plater)
{
    update_step_buttons(plater);
    Show(is_enabled() && completed_count() < int(SetupHubStep::Count));
    GetParent()->Layout();
}

}} // namespace Slic3r::GUI

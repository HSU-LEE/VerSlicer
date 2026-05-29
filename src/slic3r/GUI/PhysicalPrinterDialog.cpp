#include "PhysicalPrinterDialog.hpp"

#include "libslic3r/libslic3r.h"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"

namespace Slic3r {
namespace GUI {

PhysicalPrinterDialog::PhysicalPrinterDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Printer connection"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_valid_type(Valid)
{
    Bind(wxEVT_SHOW, [this](wxShowEvent& evt) {
        if (!evt.IsShown()) {
            evt.Skip();
            return;
        }
        evt.Skip();
        wxGetApp().CallAfter([this]() {
            MessageDialog dlg(
                this,
                wxString::Format(
                    _L("%s connects to Bambu Lab printers via the network plugin. Use Send to Printer or the Device tab."),
                    wxString::FromUTF8(SLIC3R_APP_FULL_NAME)),
                _L("Connection"),
                wxOK | wxICON_INFORMATION);
            dlg.ShowModal();
            EndModal(wxID_CANCEL);
        });
    });
}

PhysicalPrinterDialog::~PhysicalPrinterDialog() = default;

void PhysicalPrinterDialog::build_printhost_settings(ConfigOptionsGroup*) {}

void PhysicalPrinterDialog::update(bool) {}

void PhysicalPrinterDialog::update_host_type(bool) {}

void PhysicalPrinterDialog::update_printer_agent_type() {}

void PhysicalPrinterDialog::update_preset_input() {}

void PhysicalPrinterDialog::update_printhost_buttons() {}

void PhysicalPrinterDialog::update_printers() {}

void PhysicalPrinterDialog::update_ports() {}

void PhysicalPrinterDialog::update_webui() {}

void PhysicalPrinterDialog::on_dpi_changed(const wxRect&) {}

void PhysicalPrinterDialog::check_host_key_valid() {}

void PhysicalPrinterDialog::OnOK(wxEvent&) { EndModal(wxID_CANCEL); }

} // namespace GUI
} // namespace Slic3r

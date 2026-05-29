#include "BambuSmartPrintDialog.hpp"
#include "BambuSmartPrintPanel.hpp"
#include "BambuSmartPrintUi.hpp"

#include "../GUI_App.hpp"
#include "../I18N.hpp"

namespace Slic3r { namespace GUI {

BambuSmartPrintDialog::BambuSmartPrintDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Smart Print"),
                wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    SlicePilotUi::apply_dialog_chrome(this, _L("Smart Print"));

    m_panel = new BambuSmartPrintPanel(this);

    auto* topsizer = new wxBoxSizer(wxVERTICAL);
    topsizer->Add(m_panel, 1, wxEXPAND);
    SetSizer(topsizer);

    const wxSize min_size(FromDIP(760), FromDIP(640));
    SetMinSize(min_size);
    SetClientSize(min_size);
    Layout();
    CenterOnParent();

    wxGetApp().UpdateDlgDarkUI(this);
}

void BambuSmartPrintDialog::on_dpi_changed(const wxRect& /*suggested_rect*/)
{
    if (m_panel)
        m_panel->Layout();
}

}} // namespace

#include "OllamaChatDialog.hpp"
#include "OllamaChatPanel.hpp"

#include "../BambuSmartPrint/BambuSmartPrintUi.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../MainFrame.hpp"
#include "../Plater.hpp"

#include <wx/glcanvas.h>
#include <wx/sizer.h>

namespace Slic3r { namespace GUI {

OllamaChatDialog::OllamaChatDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Ollama Assistant"),
                wxDefaultPosition, wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX | wxSYSTEM_MENU | wxRESIZE_BORDER)
{
    SlicePilotUi::apply_dialog_chrome(this, _L("Ollama Assistant"));

#ifdef __APPLE__
    // In macOS native fullscreen (Spaces), non-stay-on-top dialogs may be pushed behind
    // or into a different space. Keep this floating chat visible above the fullscreen app.
    if (wxGetApp().mainframe && wxGetApp().mainframe->get_mac_full_screen()) {
        SetWindowStyleFlag(GetWindowStyleFlag() | wxSTAY_ON_TOP);
    }
#endif

    // Hide the panel header because the dialog already has a title bar.
    m_panel = new OllamaChatPanel(this, false);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_panel, 1, wxEXPAND);
    SetSizer(sizer);

    const wxSize min_size(FromDIP(520), FromDIP(380));
    SetMinSize(min_size);
    SetClientSize(min_size);
    CenterOnParent();

    wxGetApp().UpdateDlgDarkUI(this);
}

void OllamaChatDialog::on_dpi_changed(const wxRect& /*suggested_rect*/)
{
    Layout();
}

void OllamaChatDialog::toggle()
{
    if (IsShown()) {
        Hide();
        return;
    }
#ifdef __APPLE__
    if (wxGetApp().mainframe && wxGetApp().mainframe->get_mac_full_screen()) {
        SetWindowStyleFlag(GetWindowStyleFlag() | wxSTAY_ON_TOP);
    }
#endif
    ensure_visible_near_canvas();
    Show();
    Raise();
}

void OllamaChatDialog::ensure_visible_near_canvas()
{
    // Place it near the top-right of the 3D canvas area (KakaoTalk-like floating window).
    auto* plater  = wxGetApp().plater();
    auto* canvas3 = plater ? plater->get_view3D_canvas3D() : nullptr;
    auto* canvas  = canvas3 ? canvas3->get_wxglcanvas() : nullptr;
    if (!canvas) {
        CenterOnParent();
        return;
    }

    const wxPoint canvas_pos = canvas->GetScreenPosition();
    const wxSize  canvas_sz  = canvas->GetSize();
    const wxRect  canvas_rect(canvas_pos, canvas_sz);
    const wxSize dlg_size    = GetSize();

    wxPoint pos(canvas_rect.GetRight() - dlg_size.GetWidth() - FromDIP(12),
                canvas_rect.GetTop() + FromDIP(12));
    SetPosition(pos);
}

void OllamaChatDialog::submit_text_and_send(const wxString& text)
{
    if (m_panel)
        m_panel->submit_text_and_send(text);
}

}} // namespace


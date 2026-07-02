#include "AIModelCreateDialog.hpp"
#include "AIModelCreateEngine.hpp"
#include "AIModelCreateSketchPanel.hpp"

#include "../GUI_App.hpp"
#include "../MainFrame.hpp"
#include "../GUI_ObjectList.hpp"
#include "../I18N.hpp"
#include "../Plater.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace Slic3r { namespace GUI {

namespace {

AIModelCreateDialog* s_dialog{ nullptr };
const wxColour       kVerslicerOrange(0xFF, 0x8F, 0x4A);

} // namespace

AIModelCreateDialog::AIModelCreateDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Create Model"),
                wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    SetBackgroundColour(*wxWHITE);

    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* hint = new wxStaticText(this, wxID_ANY,
        _L("Sketch an outline and describe the model. AI will build a mesh and place it on the plater."));
    hint->Wrap(FromDIP(480));
    root->Add(hint, 0, wxEXPAND | wxALL, FromDIP(12));

    m_sketch = new AIModelCreateSketchPanel(this);
    root->Add(m_sketch, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(12));

    auto* prompt_label = new wxStaticText(this, wxID_ANY, _L("Description"));
    root->Add(prompt_label, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    m_prompt = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE);
    m_prompt->SetHint(_L("e.g. A rounded box with a cylinder on top"));
    m_prompt->SetMinSize(FromDIP(wxSize(420, 72)));
    root->Add(m_prompt, 0, wxEXPAND | wxALL, FromDIP(12));

    m_status = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_status->SetForegroundColour(wxColour(100, 100, 100));
    root->Add(m_status, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

    auto* btn_row = new wxBoxSizer(wxHORIZONTAL);
    auto* clear_btn = new wxButton(this, wxID_ANY, _L("Clear sketch"));
    clear_btn->Bind(wxEVT_BUTTON, &AIModelCreateDialog::on_clear_sketch, this);
    btn_row->Add(clear_btn, 0, wxRIGHT, FromDIP(8));
    btn_row->AddStretchSpacer();

    m_generate_btn = new wxButton(this, wxID_ANY, _L("Generate model"));
    m_generate_btn->SetBackgroundColour(kVerslicerOrange);
    m_generate_btn->SetForegroundColour(*wxWHITE);
    m_generate_btn->Bind(wxEVT_BUTTON, &AIModelCreateDialog::on_generate, this);
    btn_row->Add(m_generate_btn, 0);

    root->Add(btn_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));

    SetSizer(root);
    const wxSize dlg_size(FromDIP(520), FromDIP(520));
    SetMinSize(dlg_size);
    SetSize(dlg_size);
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void AIModelCreateDialog::on_dpi_changed(const wxRect& /*suggested_rect*/)
{
    Layout();
}

void AIModelCreateDialog::show_and_raise()
{
    if (!IsShown())
        Show();
    Raise();
    m_prompt->SetFocus();
}

void AIModelCreateDialog::on_clear_sketch(wxCommandEvent& /*evt*/)
{
    if (m_sketch)
        m_sketch->clear_strokes();
}

void AIModelCreateDialog::on_generate(wxCommandEvent& /*evt*/)
{
    if (m_busy)
        return;

    const wxString prompt = m_prompt->GetValue().Trim();
    if (prompt.IsEmpty() && (!m_sketch || !m_sketch->has_strokes())) {
        m_status->SetLabel(_L("Enter a description and/or draw a sketch."));
        return;
    }

    m_busy = true;
    m_generate_btn->Disable();
    m_status->SetLabel(_L("Generating model…"));

    const std::string               text_utf8 = prompt.ToUTF8().data();
    std::vector<std::vector<Vec2d>> strokes   = m_sketch ? m_sketch->normalized_strokes()
                                                         : std::vector<std::vector<Vec2d>>{};

    wxWindow* self = this;
    ai_model_create_generate_async(text_utf8, strokes, [self](AIModelCreateResult result) {
        wxGetApp().CallAfter([self, result = std::move(result)]() mutable {
            if (!self || self->IsBeingDeleted())
                return;
            auto* dlg = dynamic_cast<AIModelCreateDialog*>(self);
            if (!dlg)
                return;
            dlg->on_generation_done(std::move(result));
        });
    });
}

void AIModelCreateDialog::on_generation_done(AIModelCreateResult result)
{
    m_busy = false;
    m_generate_btn->Enable();

    if (!result.success || result.mesh.empty()) {
        wxString msg = wxString::FromUTF8(result.error.empty() ? result.summary : result.error);
        if (msg.IsEmpty())
            msg = _L("Could not generate a model.");
        m_status->SetLabel(msg);
        return;
    }

    Plater* plater = wxGetApp().plater();
    if (!plater || !wxGetApp().obj_list()) {
        m_status->SetLabel(_L("Plater is not ready."));
        return;
    }

    {
        Plater::TakeSnapshot snapshot(plater, "Add AI model");
        wxGetApp().obj_list()->load_mesh_object(result.mesh, _L("AI Model"));
    }

    if (plater->get_current_canvas3D())
        plater->get_current_canvas3D()->zoom_to_selection();

    m_status->SetLabel(_L("Model added to the plater."));
}

void show_ai_model_create_dialog()
{
    if (!s_dialog && wxGetApp().mainframe) {
        s_dialog = new AIModelCreateDialog(wxGetApp().mainframe);
        s_dialog->Bind(wxEVT_DESTROY, [](wxWindowDestroyEvent& e) {
            if (s_dialog == e.GetEventObject())
                s_dialog = nullptr;
        });
    }
    if (s_dialog)
        s_dialog->show_and_raise();
}

}} // namespace

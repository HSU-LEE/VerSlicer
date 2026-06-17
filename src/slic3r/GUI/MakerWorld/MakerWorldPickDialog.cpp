#include "MakerWorldPickDialog.hpp"

#include "../GUI_App.hpp"
#include "../I18N.hpp"

#include <wx/button.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace Slic3r { namespace GUI {

namespace {

wxString format_candidate_line(const MakerWorldCandidate& c, int index)
{
    wxString line = wxString::Format("%d. %s", index + 1, wxString::FromUTF8(c.title));
    if (!c.author.empty())
        line += wxString::Format(" — %s", wxString::FromUTF8(c.author));
    if (c.download_count > 0)
        line += wxString::Format(" (%d)", c.download_count);
    return line;
}

} // namespace

MakerWorldPickDialog::MakerWorldPickDialog(wxWindow* parent, const std::vector<MakerWorldCandidate>& candidates)
    : wxDialog(parent, wxID_ANY, _L("MakerWorld models"), wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_candidates(candidates)
{
    auto* topsizer = new wxBoxSizer(wxVERTICAL);
    topsizer->Add(new wxStaticText(this, wxID_ANY,
        _L("Choose a model to import into the slicer:")),
        0, wxALL, wxGetApp().em_unit());

    m_list = new wxListBox(this, wxID_ANY);
    for (size_t i = 0; i < m_candidates.size(); ++i)
        m_list->Append(format_candidate_line(m_candidates[i], static_cast<int>(i)));
    if (!m_candidates.empty()) {
        m_list->SetSelection(0);
        m_selected_index = 0;
    }
    topsizer->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, wxGetApp().em_unit());

    auto* btn_row = new wxBoxSizer(wxHORIZONTAL);
    auto* import_btn = new wxButton(this, wxID_OK, _L("Import into slicer"));
    auto* cancel_btn = new wxButton(this, wxID_CANCEL, _L("Cancel"));
    btn_row->AddStretchSpacer();
    btn_row->Add(import_btn, 0, wxRIGHT, wxGetApp().em_unit() / 2);
    btn_row->Add(cancel_btn, 0);
    topsizer->Add(btn_row, 0, wxEXPAND | wxALL, wxGetApp().em_unit());

    SetSizerAndFit(topsizer);
    SetMinSize(wxSize(FromDIP(420), FromDIP(280)));
    CentreOnParent();

    auto accept_selection = [this]() {
        const int sel = m_list->GetSelection();
        if (sel >= 0 && sel < static_cast<int>(m_candidates.size())) {
            m_selected_index = sel;
            EndModal(wxID_OK);
        }
    };

    import_btn->Bind(wxEVT_BUTTON, [accept_selection](wxCommandEvent&) { accept_selection(); });
    m_list->Bind(wxEVT_LISTBOX_DCLICK, [accept_selection](wxCommandEvent&) { accept_selection(); });
    m_list->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& e) {
        const int sel = e.GetSelection();
        if (sel >= 0)
            m_selected_index = sel;
    });
}

MakerWorldCandidate MakerWorldPickDialog::selected_candidate() const
{
    if (m_selected_index >= 0 && m_selected_index < static_cast<int>(m_candidates.size()))
        return m_candidates[static_cast<size_t>(m_selected_index)];
    return {};
}

}} // namespace

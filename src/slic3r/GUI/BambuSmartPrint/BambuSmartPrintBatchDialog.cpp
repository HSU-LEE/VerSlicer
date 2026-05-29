#include "BambuSmartPrintBatchDialog.hpp"
#include "BambuSmartPrintUi.hpp"

#include "../I18N.hpp"
#include "../GUI_App.hpp"
#include "../Widgets/Button.hpp"
#include "libslic3r/libslic3r.h"

#include <wx/listctrl.h>
#include <wx/statline.h>

namespace Slic3r { namespace GUI {

using namespace SlicePilotUi;

BambuSmartPrintBatchDialog::BambuSmartPrintBatchDialog(wxWindow* parent,
                                                       const BambuSmartPrint::PlateBatchSummary& summary)
    : DPIDialog(parent, wxID_ANY, _L("All plates"),
                wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_summary(summary)
{
    apply_dialog_chrome(this);

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(create_header(this, _L("Batch analysis"),
        wxString::Format(_L("%d plate(s) with models · average readiness %.0f%%"),
            m_summary.plates_with_models, m_summary.average_readiness),
        true),
        0, wxEXPAND);

    wxBoxSizer* body = nullptr;
    auto* scroll = create_scroll_body(this, &body);
    const int pad = FromDIP(12);

    wxBoxSizer* summary_inner = nullptr;
    wxPanel* summary_body = nullptr;
    auto* summary_card = create_card(scroll, &summary_inner, &summary_body, 12);
    add_card_section_title(summary_body, summary_inner, _L("Summary"), {});
    auto* summary_stats = new wxFlexGridSizer(3, FromDIP(8), FromDIP(10));
    for (int c = 0; c < 3; ++c)
        summary_stats->AddGrowableCol(c);
    wxStaticText* plates_val = nullptr;
    summary_stats->Add(create_stat_chip(summary_body, _L("Plates"), &plates_val), 1, wxEXPAND);
    if (plates_val)
        plates_val->SetLabel(wxString::Format("%d", m_summary.plates_with_models));
    wxStaticText* avg_val = nullptr;
    summary_stats->Add(create_stat_chip(summary_body, _L("Avg ready"), &avg_val), 1, wxEXPAND);
    if (avg_val)
        avg_val->SetLabel(wxString::Format("%.0f%%", m_summary.average_readiness));
    wxStaticText* changes_val = nullptr;
    summary_stats->Add(create_stat_chip(summary_body, _L("Changes"), &changes_val), 1, wxEXPAND);
    if (changes_val)
        changes_val->SetLabel(wxString::Format("%zu", m_summary.total_suggested_changes));
    summary_inner->Add(summary_stats, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
    body->Add(summary_card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    wxBoxSizer* list_inner = nullptr;
    wxPanel* list_body = nullptr;
    auto* list_card = create_card(scroll, &list_inner, &list_body, 8);
    const std::vector<std::pair<wxString, int>> batch_cols = {
        {_L("Plate"), 52}, {_L("Ready"), 58}, {_L("Changes"), 68},
        {_L("Material"), 72}, {_L("Complexity"), 80},
    };
    list_inner->Add(create_orca_list_column_header(list_body, batch_cols), 0, wxEXPAND | wxBOTTOM, FromDIP(4));
    m_list = new wxListCtrl(list_body, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                            wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
    style_orca_list(m_list);
    m_list->AppendColumn(_L("Plate"), wxLIST_FORMAT_LEFT, FromDIP(52));
    m_list->AppendColumn(_L("Ready"), wxLIST_FORMAT_LEFT, FromDIP(58));
    m_list->AppendColumn(_L("Changes"), wxLIST_FORMAT_LEFT, FromDIP(68));
    m_list->AppendColumn(_L("Material"), wxLIST_FORMAT_LEFT, FromDIP(72));
    m_list->AppendColumn(_L("Complexity"), wxLIST_FORMAT_LEFT, FromDIP(80));
    list_inner->Add(m_list, 1, wxEXPAND);
    body->Add(list_card, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    int row = 0;
    for (const BambuSmartPrint::PlateBatchEntry& e : m_summary.plates) {
        if (e.empty)
            continue;
        const long idx = m_list->InsertItem(row, wxString::Format("%d", e.plate_index + 1));
        m_list->SetItem(idx, 1, wxString::Format("%d%%", int(std::round(e.readiness_score))));
        m_list->SetItem(idx, 2, wxString::Format("%zu", e.change_count));
        m_list->SetItem(idx, 3, wxString::FromUTF8(e.suggested_material));
        m_list->SetItem(idx, 4, wxString::Format("%d", e.complexity_score));
        m_list->SetItemData(idx, static_cast<wxUIntPtr>(e.plate_index));
        if (m_selected_plate < 0)
            m_selected_plate = e.plate_index;
        ++row;
    }

    wxBoxSizer* detail_inner = nullptr;
    wxPanel* detail_body = nullptr;
    auto* detail_card = create_card(scroll, &detail_inner, &detail_body, 12);
    m_detail = new wxStaticText(detail_body, wxID_ANY, "");
    style_body_text(m_detail, true);
    wrap_static_text(m_detail, scroll, 520);
    detail_inner->Add(m_detail, 0, wxEXPAND);
    body->Add(detail_card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    root->Add(scroll, 1, wxEXPAND);

    wxBoxSizer* btn_row = nullptr;
    auto* footer = create_modal_footer(this, &btn_row);

    auto* btn_close = new Button(footer, _L("Close"));
    style_secondary_button(btn_close);
    size_action_button(footer, btn_close);

    auto* btn_open = new Button(footer, _L("Open selected plate"));
    style_primary_button(btn_open);
    size_action_button(footer, btn_open);

    btn_row->AddStretchSpacer();
    btn_row->Add(btn_close, 0, wxRIGHT, FromDIP(8));
    btn_row->Add(btn_open, 0);
    root->Add(new wxStaticLine(this), 0, wxEXPAND);
    root->Add(footer, 0, wxEXPAND);
    SetSizer(root);

    m_list->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& e) {
        m_selected_plate = int(m_list->GetItemData(e.GetIndex()));
        refresh_selection();
    });

    btn_close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });
    btn_open->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        m_open_plate = true;
        EndModal(wxID_OK);
    });

    if (m_list->GetItemCount() > 0) {
        m_list->SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
        m_selected_plate = int(m_list->GetItemData(0));
    }
    refresh_selection();

    finalize_modal_dialog(this, wxSize(600, 480), wxSize(720, 580), scroll, 280);
    CentreOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void BambuSmartPrintBatchDialog::refresh_selection()
{
    if (!m_detail)
        return;

    wxString text;
    if (m_summary.lowest_readiness_plate >= 0) {
        text << wxString::Format(_L("Lowest readiness: plate %d. "),
            m_summary.lowest_readiness_plate + 1);
    }
    if (m_summary.best_plate_index >= 0) {
        text << wxString::Format(_L("Most suggested changes: plate %d. "),
            m_summary.best_plate_index + 1);
    }
    text << wxString::Format(_L("Total adjustments across project: %zu."),
        m_summary.total_suggested_changes);
    if (m_selected_plate >= 0)
        text << "\n" << wxString::Format(_L("Selected: plate %d."), m_selected_plate + 1);

    m_detail->SetLabel(text);
    wrap_static_text(m_detail, this, 520);
}

void BambuSmartPrintBatchDialog::confirm_auto_open()
{
    m_open_plate = true;
}

void BambuSmartPrintBatchDialog::on_dpi_changed(const wxRect& /*suggested_rect*/) {}

}} // namespace

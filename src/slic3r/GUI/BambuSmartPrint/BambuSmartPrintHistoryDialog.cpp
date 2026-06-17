#include "BambuSmartPrintHistoryDialog.hpp"
#include "BambuSmartPrintService.hpp"
#include "BambuSmartPrintUi.hpp"

#include "../I18N.hpp"
#include "../GUI_App.hpp"
#include "../Plater.hpp"
#include "../GUI.hpp"
#include "../Widgets/Button.hpp"
#include "libslic3r/BambuSmartPrint/FailureDatabase.hpp"
#include "libslic3r/BambuSmartPrint/SettingsOptimizer.hpp"
#include "libslic3r/BambuSmartPrint/PrinterLearningStore.hpp"
#include "libslic3r/BambuSmartPrint/ConfigSnapshot.hpp"
#include "libslic3r/libslic3r.h"
#include <nlohmann/json.hpp>

#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/datetime.h>
#include <wx/notebook.h>

namespace Slic3r { namespace GUI {

using namespace SlicePilotUi;

namespace {

wxString format_timestamp_ms(int64_t ms)
{
    if (ms <= 0)
        return {};
    const time_t sec = static_cast<time_t>(ms / 1000);
    wxDateTime dt(sec);
    if (!dt.IsValid())
        return {};
    return dt.Format("%Y-%m-%d %H:%M");
}

wxString format_printer_label(const BambuSmartPrint::FailureDatabase::SuccessRecord& s)
{
    if (!s.printer_name.empty())
        return wxString::FromUTF8(s.printer_name);
    return wxString::FromUTF8(s.printer_id);
}

wxString format_job_label(const std::string& gcode_file)
{
    if (gcode_file.empty())
        return _L("(no job name)");
    const size_t slash = gcode_file.find_last_of("/\\");
    const std::string base = slash == std::string::npos ? gcode_file : gcode_file.substr(slash + 1);
    return wxString::FromUTF8(base);
}

} // namespace

BambuSmartPrintHistoryDialog::BambuSmartPrintHistoryDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Print history"),
                wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    apply_dialog_chrome(this);

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(create_header(this, _L("Print history"),
        _L("Failures and successes stored locally to improve future recommendations")),
        0, wxEXPAND);

    wxBoxSizer* body = nullptr;
    auto* scroll = create_scroll_body(this, &body);
    const int side = FromDIP(12);

    wxBoxSizer* overview_inner = nullptr;
    wxPanel* overview_body = nullptr;
    auto* overview_card = create_card(scroll, &overview_inner, &overview_body, 12);
    add_card_section_title(overview_body, overview_inner, _L("Overview"), _L("Stored on this device"));
    auto* stats = new wxFlexGridSizer(3, FromDIP(8), FromDIP(10));
    for (int c = 0; c < 3; ++c)
        stats->AddGrowableCol(c);
    wxStaticText* lbl_tmp = nullptr;
    stats->Add(create_stat_chip(overview_body, _L("Failures"), &m_stat_failures, &lbl_tmp), 1, wxEXPAND);
    stats->Add(create_stat_chip(overview_body, _L("Successes"), &m_stat_successes, &lbl_tmp), 1, wxEXPAND);
    stats->Add(create_stat_chip(overview_body, _L("30 days"), &m_stat_recent, &lbl_tmp), 1, wxEXPAND);
    overview_inner->Add(stats, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
    body->Add(overview_card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, side);

    m_notebook = new wxNotebook(scroll, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP);
    style_orca_notebook(m_notebook);
    m_notebook->SetMinSize(FromDIP(wxSize(-1, 280)));

    auto* fail_page = new wxPanel(m_notebook);
    fail_page->SetBackgroundColour(Theme::surface());
    auto* fail_sz = new wxBoxSizer(wxVERTICAL);
    const std::vector<std::pair<wxString, int>> fail_cols = {
        {_L("Time"), 130}, {_L("Printer"), 110}, {_L("Diagnosis"), 300},
    };
    fail_sz->Add(create_orca_list_column_header(fail_page, fail_cols), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    m_fail_list = new wxListCtrl(fail_page, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
    style_orca_list(m_fail_list);
    m_fail_list->AppendColumn(_L("Time"), wxLIST_FORMAT_LEFT, FromDIP(130));
    m_fail_list->AppendColumn(_L("Printer"), wxLIST_FORMAT_LEFT, FromDIP(110));
    m_fail_list->AppendColumn(_L("Diagnosis"), wxLIST_FORMAT_LEFT, FromDIP(300));
    fail_sz->Add(m_fail_list, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
    fail_page->SetSizer(fail_sz);
    m_notebook->AddPage(fail_page, _L("Failures"));

    auto* ok_page = new wxPanel(m_notebook);
    ok_page->SetBackgroundColour(Theme::surface());
    auto* ok_sz = new wxBoxSizer(wxVERTICAL);
    const std::vector<std::pair<wxString, int>> ok_cols = {
        {_L("Time"), 130}, {_L("Printer"), 120}, {_L("Job"), 300},
    };
    ok_sz->Add(create_orca_list_column_header(ok_page, ok_cols), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    m_success_list = new wxListCtrl(ok_page, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                    wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
    style_orca_list(m_success_list);
    m_success_list->AppendColumn(_L("Time"), wxLIST_FORMAT_LEFT, FromDIP(130));
    m_success_list->AppendColumn(_L("Printer"), wxLIST_FORMAT_LEFT, FromDIP(120));
    m_success_list->AppendColumn(_L("Job"), wxLIST_FORMAT_LEFT, FromDIP(300));
    ok_sz->Add(m_success_list, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
    ok_page->SetSizer(ok_sz);
    m_notebook->AddPage(ok_page, _L("Successes"));

    body->Add(m_notebook, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, side);

    wxBoxSizer* detail_inner = nullptr;
    wxPanel* detail_body = nullptr;
    auto* detail_card = create_card(scroll, &detail_inner, &detail_body, 12);
    m_detail = new wxStaticText(detail_body, wxID_ANY, _L("Select a row to see details and give feedback."));
    wrap_static_text(m_detail, scroll, 560);
    style_body_text(m_detail, true);
    detail_inner->Add(m_detail, 0, wxEXPAND);
    body->Add(detail_card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    body->AddSpacer(FromDIP(8));

    root->Add(scroll, 1, wxEXPAND);

    wxBoxSizer* btn_row = nullptr;
    auto* footer = create_modal_footer(this, &btn_row);

    m_btn_helpful = new Button(footer, _L("Helpful"));
    style_secondary_button(m_btn_helpful);
    size_action_button(footer, m_btn_helpful);
    m_btn_not = new Button(footer, _L("Not helpful"));
    style_secondary_button(m_btn_not);
    size_action_button(footer, m_btn_not);
    m_btn_reapply = new Button(footer, _L("Re-apply fixes"));
    style_primary_button(m_btn_reapply);
    size_action_button(footer, m_btn_reapply);
    auto* btn_close = new Button(footer, _L("Close"));
    style_secondary_button(btn_close);
    size_action_button(footer, btn_close);

    m_btn_helpful->Enable(false);
    m_btn_not->Enable(false);
    m_btn_reapply->Enable(false);

    btn_row->Add(m_btn_helpful, 0, wxRIGHT, FromDIP(8));
    btn_row->Add(m_btn_not, 0, wxRIGHT, FromDIP(8));
    btn_row->Add(m_btn_reapply, 0, wxRIGHT, FromDIP(8));
    btn_row->AddStretchSpacer();
    btn_row->Add(btn_close, 0);
    root->Add(new wxStaticLine(this), 0, wxEXPAND);
    root->Add(footer, 0, wxEXPAND);

    SetSizer(root);
    finalize_modal_dialog(this, wxSize(740, 560), wxSize(860, 660), scroll, 340);
    CentreOnParent();

    m_fail_list->Bind(wxEVT_LIST_ITEM_SELECTED, &BambuSmartPrintHistoryDialog::on_failure_selection, this);
    m_fail_list->Bind(wxEVT_LIST_ITEM_DESELECTED, [this](wxListEvent&) {
        if (m_notebook && m_notebook->GetSelection() == 0) {
            m_selected_record_id.clear();
            m_detail->SetLabel(_L("Select a failure to see details and give feedback."));
            if (m_btn_helpful) m_btn_helpful->Enable(false);
            if (m_btn_not) m_btn_not->Enable(false);
            if (m_btn_reapply) m_btn_reapply->Enable(false);
        }
    });
    m_success_list->Bind(wxEVT_LIST_ITEM_SELECTED, &BambuSmartPrintHistoryDialog::on_success_selection, this);
    m_notebook->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& e) {
        e.Skip();
        if (e.GetSelection() == 0) {
            m_detail->SetLabel(_L("Select a failure to see details and give feedback."));
            if (m_btn_helpful) m_btn_helpful->Enable(false);
            if (m_btn_not) m_btn_not->Enable(false);
            if (m_btn_reapply) m_btn_reapply->Enable(false);
        } else {
            m_selected_record_id.clear();
            m_detail->SetLabel(_L("Select a success to see job details."));
            if (m_btn_helpful) m_btn_helpful->Enable(false);
            if (m_btn_not) m_btn_not->Enable(false);
            if (m_btn_reapply) m_btn_reapply->Enable(false);
        }
    });
    m_btn_helpful->Bind(wxEVT_BUTTON, &BambuSmartPrintHistoryDialog::on_helpful, this);
    m_btn_not->Bind(wxEVT_BUTTON, &BambuSmartPrintHistoryDialog::on_not_helpful, this);
    m_btn_reapply->Bind(wxEVT_BUTTON, &BambuSmartPrintHistoryDialog::on_reapply, this);
    btn_close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });

    refresh_overview_stats();
    refresh_failures();
    refresh_successes();
    wxGetApp().UpdateDlgDarkUI(this);
}

void BambuSmartPrintHistoryDialog::on_dpi_changed(const wxRect& /*suggested_rect*/) {}

void BambuSmartPrintHistoryDialog::refresh_overview_stats()
{
    auto& db = BambuSmartPrint::FailureDatabase::instance();
    if (m_stat_failures)
        m_stat_failures->SetLabel(wxString::Format("%d", db.count_all_failures()));
    if (m_stat_successes)
        m_stat_successes->SetLabel(wxString::Format("%d", db.count_all_successes()));
    if (m_stat_recent)
        m_stat_recent->SetLabel(wxString::Format("%d",
            db.count_all_failures_recent(30LL * 24 * 3600 * 1000)));
}

void BambuSmartPrintHistoryDialog::refresh_failures(bool keep_selection)
{
    const std::string keep_id = keep_selection ? m_selected_record_id : std::string{};

    m_fail_list->DeleteAllItems();
    m_row_record_ids.clear();
    m_selected_record_id.clear();
    m_detail->SetLabel(_L("Select a failure to see details and give feedback."));
    if (m_btn_helpful) m_btn_helpful->Enable(false);
    if (m_btn_not) m_btn_not->Enable(false);
    if (m_btn_reapply) m_btn_reapply->Enable(false);

    long reselect_idx = -1;
    for (const auto& r : BambuSmartPrint::FailureDatabase::instance().all_records(200)) {
        long idx = m_fail_list->InsertItem(m_fail_list->GetItemCount(), format_timestamp_ms(r.timestamp_utc_ms));
        m_fail_list->SetItem(idx, 1, wxString::FromUTF8(r.printer_name.empty() ? r.printer_id : r.printer_name));
        wxString title = wxString::FromUTF8(r.diagnosis.title);
        if (!r.user_feedback.empty())
            title += wxString::Format(" [%s]", wxString::FromUTF8(r.user_feedback));
        m_fail_list->SetItem(idx, 2, title);
        m_row_record_ids.push_back(r.record_id);
        if (!keep_id.empty() && r.record_id == keep_id)
            reselect_idx = idx;
    }

    if (reselect_idx >= 0) {
        m_fail_list->SetItemState(reselect_idx, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
        m_fail_list->SetFocus();
    }
    refresh_overview_stats();
}

void BambuSmartPrintHistoryDialog::refresh_successes()
{
    m_success_list->DeleteAllItems();
    m_success_rows = BambuSmartPrint::FailureDatabase::instance().success_records(200);
    for (const auto& s : m_success_rows) {
        long idx = m_success_list->InsertItem(m_success_list->GetItemCount(), format_timestamp_ms(s.timestamp_utc_ms));
        m_success_list->SetItem(idx, 1, format_printer_label(s));
        m_success_list->SetItem(idx, 2, format_job_label(s.gcode_file));
    }
    refresh_overview_stats();
}

void BambuSmartPrintHistoryDialog::show_failure_detail(const std::string& record_id)
{
    BambuSmartPrint::PrintFailureRecord record;
    if (!BambuSmartPrint::FailureDatabase::instance().find_record(record_id, &record))
        return;

    m_selected_record_id = record_id;
    wxString detail;
    detail << wxString::FromUTF8(record.diagnosis.title);
    if (!record.diagnosis.description.empty())
        detail << "\n\n" << wxString::FromUTF8(record.diagnosis.description);
    if (!record.hms_codes.empty()) {
        detail << "\n\n" << _L("HMS codes:") << "\n";
        for (const std::string& code : record.hms_codes)
            detail << "  • " << wxString::FromUTF8(code) << "\n";
    }
    if (!record.user_feedback.empty())
        detail << "\n" << _L("Your feedback:") << " " << wxString::FromUTF8(record.user_feedback);

    m_detail->SetLabel(detail);
    m_btn_helpful->Enable(true);
    m_btn_not->Enable(true);
    m_btn_reapply->Enable(!record.diagnosis.recommended_fixes.empty());
}

void BambuSmartPrintHistoryDialog::show_success_detail(size_t success_index)
{
    if (success_index >= m_success_rows.size())
        return;
    const auto& s = m_success_rows[success_index];
    m_detail->SetLabel(wxString::Format(
        _L("Printer: %s\nJob: %s\nCompleted: %s"),
        format_printer_label(s), format_job_label(s.gcode_file),
        format_timestamp_ms(s.timestamp_utc_ms)));
}

void BambuSmartPrintHistoryDialog::on_failure_selection(wxListEvent& evt)
{
    const long idx = evt.GetIndex();
    if (idx < 0 || idx >= static_cast<long>(m_row_record_ids.size()))
        return;
    show_failure_detail(m_row_record_ids[idx]);
}

void BambuSmartPrintHistoryDialog::on_success_selection(wxListEvent& evt)
{
    show_success_detail(static_cast<size_t>(evt.GetIndex()));
}

void BambuSmartPrintHistoryDialog::on_helpful(wxCommandEvent&)
{
    if (m_selected_record_id.empty()) return;
    BambuSmartPrint::FailureDatabase::instance().set_record_feedback(m_selected_record_id, "helpful");
    refresh_failures(true);
}

void BambuSmartPrintHistoryDialog::on_not_helpful(wxCommandEvent&)
{
    if (m_selected_record_id.empty()) return;
    BambuSmartPrint::FailureDatabase::instance().set_record_feedback(m_selected_record_id, "not_helpful");
    refresh_failures(true);
}

void BambuSmartPrintHistoryDialog::on_reapply(wxCommandEvent&)
{
    if (m_selected_record_id.empty()) return;
    BambuSmartPrint::PrintFailureRecord record;
    if (!BambuSmartPrint::FailureDatabase::instance().find_record(m_selected_record_id, &record))
        return;
    if (record.diagnosis.recommended_fixes.empty())
        return;

    Plater* plater = wxGetApp().plater();
    if (!plater) return;

    const BambuSmartPrint::PrinterLearningProfile learning =
        BambuSmartPrint::PrinterLearningStore::instance().get_profile(record.printer_id);
    const BambuSmartPrint::AutoSettingsResult fixes =
        BambuSmartPrint::SettingsOptimizer::optimize_from_diagnosis(
            record.config_snapshot, record.diagnosis, &learning);

    BambuSmartPrintService::instance().apply_config_to_plater(
        plater, record.config_snapshot, fixes.config_delta, true, true);
}

}} // namespace

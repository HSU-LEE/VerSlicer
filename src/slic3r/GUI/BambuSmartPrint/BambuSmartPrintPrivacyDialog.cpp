#include "BambuSmartPrintPrivacyDialog.hpp"
#include "BambuSmartPrintService.hpp"
#include "BambuSmartPrintUi.hpp"

#include "../I18N.hpp"
#include "../GUI_App.hpp"
#include "../Widgets/Button.hpp"
#include "../Widgets/CheckBox.hpp"
#include "../Widgets/Label.hpp"
#include "libslic3r/BambuSmartPrint/FailureDatabase.hpp"
#include "libslic3r/BambuSmartPrint/PrinterLearningStore.hpp"
#include "libslic3r/BambuSmartPrint/BambuSmartPrintPaths.hpp"
#include "libslic3r/BambuSmartPrint/SmartPrintDataManager.hpp"
#include "libslic3r/libslic3r.h"

#include <wx/checkbox.h>
#include <wx/clipbrd.h>
#include <wx/datetime.h>
#include <wx/sizer.h>
#include <wx/textdlg.h>

#include <algorithm>
#include <string>
#include <vector>
#include <boost/filesystem.hpp>

namespace Slic3r { namespace GUI {

using namespace SlicePilotUi;

namespace {

namespace fs = boost::filesystem;

void open_folder_in_shell(const std::string& path)
{
#ifdef _WIN32
    const wxString widepath = from_u8(path);
    const wchar_t* argv[] = { L"explorer", widepath.GetData(), nullptr };
    ::wxExecute(argv, wxEXEC_ASYNC, nullptr);
#elif __APPLE__
    const char* argv[] = { "open", path.data(), nullptr };
    ::wxExecute(argv, wxEXEC_ASYNC, nullptr);
#else
    const char* argv[] = { "xdg-open", path.data(), nullptr };
    ::wxExecute(argv, wxEXEC_ASYNC, nullptr);
#endif
}

bool copy_text_to_clipboard(const wxString& text)
{
    if (!wxTheClipboard->Open())
        return false;
    wxTheClipboard->SetData(new wxTextDataObject(text));
    wxTheClipboard->Close();
    return true;
}

uintmax_t directory_size_bytes(const fs::path& root)
{
    if (!fs::exists(root))
        return 0;
    boost::system::error_code ec;
    uintmax_t total = 0;
    if (fs::is_directory(root, ec)) {
        for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
            if (ec)
                break;
            if (fs::is_regular_file(it->path(), ec))
                total += fs::file_size(it->path(), ec);
        }
    } else if (fs::is_regular_file(root, ec)) {
        total = fs::file_size(root, ec);
    }
    return total;
}

wxString format_storage_size(uintmax_t bytes)
{
    if (bytes < 1024)
        return wxString::Format(_L("%llu B"), static_cast<unsigned long long>(bytes));
    const double kb = static_cast<double>(bytes) / 1024.0;
    if (kb < 1024.0)
        return wxString::Format(_L("%.1f KB"), kb);
    const double mb = kb / 1024.0;
    if (mb < 1024.0)
        return wxString::Format(_L("%.1f MB"), mb);
    return wxString::Format(_L("%.2f GB"), mb / 1024.0);
}

wxString ellipsize_path_middle(const wxString& path, int max_chars)
{
    if (max_chars < 12 || static_cast<int>(path.length()) <= max_chars)
        return path;
    const int head = max_chars * 2 / 5;
    const int tail = max_chars - head - 1;
    return path.substr(0, head) + wxString::FromUTF8("…") + path.substr(path.length() - tail);
}

wxStaticText* add_privacy_pillar(wxWindow* parent, wxBoxSizer* card_sz,
                                 std::vector<wxStaticText*>& wrap_out,
                                 const wxString& title, const wxString& detail)
{
    auto* row = new wxPanel(parent, wxID_ANY);
    row->SetBackgroundColour(Theme::surface_alt());
    auto* row_sz = new wxBoxSizer(wxHORIZONTAL);

    auto* marker = new wxPanel(row, wxID_ANY, wxDefaultPosition,
                               parent->FromDIP(wxSize(4, -1)));
    marker->SetBackgroundColour(Theme::primary());
    marker->SetMinSize(parent->FromDIP(wxSize(4, 40)));
    row_sz->Add(marker, 0, wxEXPAND | wxRIGHT, parent->FromDIP(10));

    auto* text_col = new wxBoxSizer(wxVERTICAL);
    auto* t = new wxStaticText(row, wxID_ANY, title);
    style_section_title(t);
    text_col->Add(t, 0, wxEXPAND);
    auto* d = new wxStaticText(row, wxID_ANY, detail, wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
    style_body_text(d, true);
    text_col->Add(d, 0, wxEXPAND | wxTOP, parent->FromDIP(4));
    row_sz->Add(text_col, 1, wxEXPAND);

    row->SetSizer(row_sz);
    card_sz->Add(row, 0, wxEXPAND | wxBOTTOM, parent->FromDIP(10));
    wrap_out.push_back(d);
    return d;
}

wxStaticText* add_bullet_line(wxWindow* parent, wxBoxSizer* card_sz,
                              std::vector<wxStaticText*>& wrap_out, const wxString& text)
{
    auto* line = new wxStaticText(parent, wxID_ANY, wxString::FromUTF8("• ") + text,
                                  wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
    style_body_text(line, true);
    card_sz->Add(line, 0, wxEXPAND | wxBOTTOM, parent->FromDIP(6));
    wrap_out.push_back(line);
    return line;
}

} // namespace

BambuSmartPrintPrivacyDialog::BambuSmartPrintPrivacyDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Smart Print data"),
                wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    apply_dialog_chrome(this);

    m_storage_path = BambuSmartPrint::FailureDatabase::instance().storage_path();
    m_catalog_path = BambuSmartPrint::user_error_catalog_path();

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(create_header(this, _L("Privacy & data"),
        _L("Local learning only — your print history never leaves this computer")),
        0, wxEXPAND);

    wxBoxSizer* body = nullptr;
    m_scroll = create_scroll_body(this, &body);
    const int pad = FromDIP(12);

    auto* hero = create_banner(m_scroll, &m_hero_text, BannerKind::Success);
    if (m_hero_text)
        m_hero_text->SetLabel(
            _L("Smart Print improves recommendations from failures, successes, and per-printer "
               "profiles stored on disk. No cloud AI or telemetry is used for this data."));
    register_wrap_label(m_hero_text);
    body->Add(hero, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    m_load_error_banner = create_banner(m_scroll, &m_load_error_text, BannerKind::Warning);
    if (m_load_error_text)
        m_load_error_text->SetLabel(_L("Some Smart Print data could not be loaded. Details are shown below."));
    register_wrap_label(m_load_error_text);
    m_load_error_banner->Hide();
    body->Add(m_load_error_banner, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));

    wxBoxSizer* overview_inner = nullptr;
    wxPanel* overview_body = nullptr;
    auto* overview_card = create_card(m_scroll, &overview_inner, &overview_body, 12);
    add_card_section_title(overview_body, overview_inner, _L("On this device"),
                           _L("Live counts from your local database"));

    auto* stats_grid = new wxFlexGridSizer(3, FromDIP(8), FromDIP(10));
    for (int c = 0; c < 3; ++c)
        stats_grid->AddGrowableCol(c);

    wxStaticText* lbl_tmp = nullptr;
    stats_grid->Add(create_stat_chip(overview_body, _L("Failures"), &m_stat_failures, &lbl_tmp),
                    1, wxEXPAND);
    register_wrap_label(lbl_tmp);
    stats_grid->Add(create_stat_chip(overview_body, _L("Successes"), &m_stat_successes, &lbl_tmp),
                    1, wxEXPAND);
    register_wrap_label(lbl_tmp);
    stats_grid->Add(create_stat_chip(overview_body, _L("30 days"), &m_stat_recent, &lbl_tmp),
                    1, wxEXPAND);
    register_wrap_label(lbl_tmp);
    stats_grid->Add(create_stat_chip(overview_body, _L("Printers"), &m_stat_printers, &lbl_tmp),
                    1, wxEXPAND);
    register_wrap_label(lbl_tmp);
    stats_grid->Add(create_stat_chip(overview_body, _L("Disk used"), &m_stat_disk, &lbl_tmp),
                    1, wxEXPAND);
    register_wrap_label(lbl_tmp);
    auto* stat_pad = new wxPanel(overview_body, wxID_ANY);
    stat_pad->SetBackgroundColour(Theme::surface_alt());
    stats_grid->Add(stat_pad, 1, wxEXPAND);

    overview_inner->Add(stats_grid, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
    body->Add(overview_card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    wxBoxSizer* privacy_inner = nullptr;
    wxPanel* privacy_body = nullptr;
    auto* privacy_card = create_card(m_scroll, &privacy_inner, &privacy_body, 12);
    add_card_section_title(privacy_body, privacy_inner, _L("What stays local"),
                           _L("Everything below is stored under your Verslicer data folder"));
    add_privacy_pillar(privacy_body, privacy_inner, m_wrap_labels,
        _L("Print failures & diagnoses"),
        _L("Error codes, suggested fixes, and your helpful / not helpful feedback."));
    add_privacy_pillar(privacy_body, privacy_inner, m_wrap_labels,
        _L("Successful print snapshots"),
        _L("Printer, job name, and a settings snapshot to compare future jobs."));
    add_privacy_pillar(privacy_body, privacy_inner, m_wrap_labels,
        _L("Per-printer learning"),
        _L("Small setting adjustments learned from repeated outcomes on each printer."));
    privacy_inner->AddSpacer(FromDIP(6));
    wxStaticText* never_txt = nullptr;
    auto* never_banner = create_banner(privacy_body, &never_txt, BannerKind::Info);
    if (never_txt)
        never_txt->SetLabel(
            _L("Not collected here: cloud AI uploads, anonymous telemetry, or your Bambu account password."));
    register_wrap_label(never_txt);
    privacy_inner->Add(never_banner, 0, wxEXPAND);
    body->Add(privacy_card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    wxBoxSizer* storage_inner = nullptr;
    wxPanel* storage_body = nullptr;
    auto* storage_card = create_card(m_scroll, &storage_inner, &storage_body, 12);
    add_card_section_title(storage_body, storage_inner, _L("Storage"),
                           _L("Open the folder or copy paths for backup scripts"));
    m_path_text = new wxStaticText(storage_body, wxID_ANY, wxString::FromUTF8(m_storage_path),
                                   wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
    style_body_text(m_path_text, true);
    m_path_text->SetToolTip(wxString::FromUTF8(m_storage_path));
    register_wrap_label(m_path_text);
    storage_inner->Add(m_path_text, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    auto* path_actions = new wxBoxSizer(wxHORIZONTAL);
    auto* btn_open = new Button(storage_body, _L("Open folder"));
    style_secondary_button(btn_open);
    size_action_button(storage_body, btn_open);
    auto* btn_copy_path = new Button(storage_body, _L("Copy path"));
    style_secondary_button(btn_copy_path);
    size_action_button(storage_body, btn_copy_path);
    path_actions->Add(btn_open, 0, wxRIGHT, FromDIP(6));
    path_actions->Add(btn_copy_path, 0);
    storage_inner->Add(path_actions, 0, wxEXPAND | wxBOTTOM, FromDIP(10));

    m_catalog_text = new wxStaticText(storage_body, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                      wxST_NO_AUTORESIZE);
    style_body_text(m_catalog_text, true);
    register_wrap_label(m_catalog_text);
    storage_inner->Add(m_catalog_text, 0, wxEXPAND);
    body->Add(storage_card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    wxBoxSizer* manage_inner = nullptr;
    wxPanel* manage_body = nullptr;
    auto* manage_card = create_card(m_scroll, &manage_inner, &manage_body, 12);
    add_card_section_title(manage_body, manage_inner, _L("Manage data"),
                           _L("Review history or remove specific categories"));
    add_bullet_line(manage_body, manage_inner, m_wrap_labels,
        _L("Export creates a timestamped folder you can archive or move to another PC."));
    add_bullet_line(manage_body, manage_inner, m_wrap_labels,
        _L("Selective delete keeps other categories intact; “Delete everything” clears all local Smart Print data."));

    auto* manage_actions = new wxBoxSizer(wxVERTICAL);
    auto* btn_history = new Button(manage_body, _L("View print history"));
    style_primary_button(btn_history);
    size_action_button(manage_body, btn_history);
    manage_actions->Add(btn_history, 0, wxEXPAND | wxBOTTOM, FromDIP(8));

    auto* manage_row = new wxBoxSizer(wxHORIZONTAL);
    auto* btn_clear_history = new Button(manage_body, _L("Clear history"));
    style_secondary_button(btn_clear_history);
    size_action_button(manage_body, btn_clear_history);
    auto* btn_clear_learning = new Button(manage_body, _L("Clear learning"));
    style_secondary_button(btn_clear_learning);
    size_action_button(manage_body, btn_clear_learning);
    manage_row->Add(btn_clear_history, 1, wxEXPAND | wxRIGHT, FromDIP(6));
    manage_row->Add(btn_clear_learning, 1, wxEXPAND);
    manage_actions->Add(manage_row, 0, wxEXPAND);
    manage_inner->Add(manage_actions, 0, wxEXPAND | wxTOP, FromDIP(8));
    body->Add(manage_card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
    body->AddSpacer(FromDIP(8));

    root->Add(m_scroll, 1, wxEXPAND);

    auto* footer_outer = new wxBoxSizer(wxVERTICAL);
    m_delete_confirm_cb = new CheckBox(this);
    auto* confirm_lbl = new wxStaticText(this, wxID_ANY,
        _L("I understand that deleting Smart Print data cannot be undone"),
        wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
    confirm_lbl->SetFont(Label::Body_13);
    confirm_lbl->SetForegroundColour(Theme::text_muted());
    wrap_static_text(confirm_lbl, this, 560);
    auto* confirm_row = new wxBoxSizer(wxHORIZONTAL);
    confirm_row->Add(m_delete_confirm_cb, 0, wxALIGN_TOP | wxRIGHT, FromDIP(6));
    confirm_row->Add(confirm_lbl, 1, wxEXPAND);
    footer_outer->Add(confirm_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(10));

    wxBoxSizer* row = nullptr;
    auto* footer = create_modal_footer(this, &row);
    auto* btn_import = new Button(footer, _L("Import backup…"));
    style_secondary_button(btn_import);
    size_action_button(footer, btn_import);
    auto* btn_export = new Button(footer, _L("Export backup…"));
    style_secondary_button(btn_export);
    size_action_button(footer, btn_export);
    auto* btn_catalog = new Button(footer, _L("Add MC error code…"));
    style_secondary_button(btn_catalog);
    size_action_button(footer, btn_catalog);
    m_btn_delete = new Button(footer, _L("Delete everything"));
    style_accent_button(m_btn_delete, Theme::danger());
    size_action_button(footer, m_btn_delete);
    m_btn_delete->Enable(false);
    auto* btn_close = new Button(footer, _L("Close"));
    style_primary_button(btn_close);
    size_action_button(footer, btn_close);

    row->Add(btn_import, 0, wxRIGHT, FromDIP(8));
    row->Add(btn_export, 0, wxRIGHT, FromDIP(8));
    row->Add(btn_catalog, 0, wxRIGHT, FromDIP(8));
    row->Add(m_btn_delete, 0, wxRIGHT, FromDIP(8));
    row->AddStretchSpacer();
    row->Add(btn_close, 0);
    footer_outer->Add(footer, 0, wxEXPAND);
    root->Add(new wxStaticLine(this), 0, wxEXPAND);
    root->Add(footer_outer, 0, wxEXPAND);

    SetSizer(root);
    finalize_modal_dialog(this, wxSize(660, 600), wxSize(800, 720), m_scroll, 360);
    CentreOnParent();

    refresh_overview();
    refresh_storage_details();
    relayout_wrapped_content();

    Bind(wxEVT_SHOW, [this](wxShowEvent& e) {
        e.Skip();
        if (e.IsShown())
            relayout_wrapped_content();
    });

    m_delete_confirm_cb->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&) { update_delete_button_state(); });

    btn_open->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        open_folder_in_shell(m_storage_path);
    });
    btn_copy_path->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        const wxString full = wxString::FromUTF8(m_storage_path);
        if (copy_text_to_clipboard(full))
            wxMessageBox(_L("Storage path copied to clipboard."), SLIC3R_APP_FULL_NAME,
                         wxOK | wxICON_INFORMATION, this);
        else
            wxMessageBox(_L("Could not access the clipboard."), SLIC3R_APP_FULL_NAME,
                         wxOK | wxICON_WARNING, this);
    });

    btn_history->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        BambuSmartPrintService::instance().show_history_dialog(this);
        refresh_overview();
    });

    btn_clear_history->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (wxMessageBox(_L("Delete all failure and success history?\n\nPrinter learning profiles will be kept."),
                         SLIC3R_APP_FULL_NAME, wxYES_NO | wxICON_WARNING, this) != wxYES)
            return;
        BambuSmartPrint::FailureDatabase::instance().clear_all_data();
        wxMessageBox(_L("Print history cleared."), SLIC3R_APP_FULL_NAME, wxOK | wxICON_INFORMATION, this);
        refresh_overview();
    });

    btn_clear_learning->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (wxMessageBox(_L("Delete all per-printer learning profiles?\n\nFailure and success history will be kept."),
                         SLIC3R_APP_FULL_NAME, wxYES_NO | wxICON_WARNING, this) != wxYES)
            return;
        BambuSmartPrint::PrinterLearningStore::instance().clear_all_data();
        wxMessageBox(_L("Printer learning cleared."), SLIC3R_APP_FULL_NAME, wxOK | wxICON_INFORMATION, this);
        refresh_overview();
    });

    btn_import->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        wxDirDialog dlg(this, _L("Select a Smart Print backup folder to import"),
                        wxEmptyString, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
        if (dlg.ShowModal() != wxID_OK)
            return;
        const bool merge = wxMessageBox(
            _L("Merge with existing data?\n\nYes = keep current entries and add backup files.\nNo = replace history and learning with backup."),
            SLIC3R_APP_FULL_NAME, wxYES_NO | wxICON_QUESTION, this) == wxYES;
        const auto result = BambuSmartPrint::import_backup_folder(dlg.GetPath().ToUTF8().data(), merge);
        if (result.ok) {
            wxMessageBox(_L("Smart Print data imported successfully."), SLIC3R_APP_FULL_NAME,
                         wxOK | wxICON_INFORMATION, this);
            refresh_overview();
            refresh_storage_details();
        } else {
            wxMessageBox(wxString::Format(_L("Import failed:\n\n%s"), wxString::FromUTF8(result.error)),
                         SLIC3R_APP_FULL_NAME, wxOK | wxICON_ERROR, this);
        }
    });

    btn_export->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        wxDirDialog dlg(this, _L("Choose where to save a Smart Print backup"),
                        wxEmptyString, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
        if (dlg.ShowModal() != wxID_OK)
            return;
        const auto result = BambuSmartPrint::export_backup_folder(dlg.GetPath().ToUTF8().data());
        if (result.ok) {
            wxMessageBox(wxString::Format(_L("Backup saved to:\n\n%s"), wxString::FromUTF8(result.path)),
                         SLIC3R_APP_FULL_NAME, wxOK | wxICON_INFORMATION, this);
            if (wxMessageBox(_L("Open the backup folder now?"), SLIC3R_APP_FULL_NAME,
                             wxYES_NO | wxICON_QUESTION, this) == wxYES)
                open_folder_in_shell(result.path);
        } else {
            wxMessageBox(wxString::Format(_L("Export failed:\n\n%s"), wxString::FromUTF8(result.error)),
                         SLIC3R_APP_FULL_NAME, wxOK | wxICON_ERROR, this);
        }
    });

    btn_catalog->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        wxTextEntryDialog code_dlg(this, _L("MC print error code (integer from printer HMS / log):"),
                                   _L("Add error catalog entry"));
        if (code_dlg.ShowModal() != wxID_OK)
            return;
        long code_val = 0;
        if (!code_dlg.GetValue().ToLong(&code_val) || code_val <= 0) {
            wxMessageBox(_L("Enter a valid positive integer MC code."), SLIC3R_APP_FULL_NAME,
                         wxOK | wxICON_WARNING, this);
            return;
        }
        const wxString categories[] = { "adhesion", "filament", "temperature", "mechanical", "gcode", "network" };
        int cat_idx = wxGetSingleChoiceIndex(
            _L("Failure category"), _L("Category"), 6, categories, this);
        if (cat_idx < 0)
            return;
        wxTextEntryDialog title_dlg(this, _L("Short title shown in diagnosis:"), _L("Title"));
        if (title_dlg.ShowModal() != wxID_OK)
            return;
        wxTextEntryDialog desc_dlg(this, _L("Description for users:"), _L("Description"));
        if (desc_dlg.ShowModal() != wxID_OK)
            return;
        std::string err;
        if (BambuSmartPrint::append_user_mc_error(int(code_val), categories[cat_idx].ToUTF8().data(),
                title_dlg.GetValue().ToUTF8().data(), desc_dlg.GetValue().ToUTF8().data(), &err)) {
            wxMessageBox(_L("Error catalog updated. Future failures with this MC code will use the new mapping."),
                         SLIC3R_APP_FULL_NAME, wxOK | wxICON_INFORMATION, this);
            refresh_storage_details();
        } else {
            wxMessageBox(wxString::Format(_L("Could not update catalog:\n\n%s"), wxString::FromUTF8(err)),
                         SLIC3R_APP_FULL_NAME, wxOK | wxICON_ERROR, this);
        }
    });

    m_btn_delete->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (!m_delete_confirm_cb || !m_delete_confirm_cb->GetValue()) {
            wxMessageBox(_L("Check the confirmation box before deleting all Smart Print data."),
                         SLIC3R_APP_FULL_NAME, wxOK | wxICON_INFORMATION, this);
            return;
        }
        if (wxMessageBox(_L("Permanently delete failure history, success snapshots, printer learning, "
                             "and your custom error catalog?\n\nThis cannot be undone."),
                         SLIC3R_APP_FULL_NAME, wxYES_NO | wxICON_WARNING, this) != wxYES)
            return;
        BambuSmartPrint::FailureDatabase::instance().clear_all_data();
        BambuSmartPrint::PrinterLearningStore::instance().clear_all_data();
        boost::system::error_code ec;
        fs::remove(m_catalog_path, ec);
        m_delete_confirm_cb->SetValue(false);
        update_delete_button_state();
        refresh_overview();
        refresh_storage_details();
        wxMessageBox(_L("All Smart Print data has been removed from this computer."),
                     SLIC3R_APP_FULL_NAME, wxOK | wxICON_INFORMATION, this);
    });

    btn_close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });

    Bind(wxEVT_SIZE, [this](wxSizeEvent& e) {
        e.Skip();
        relayout_wrapped_content();
    });

    wxGetApp().UpdateDlgDarkUI(this);
}

void BambuSmartPrintPrivacyDialog::register_wrap_label(wxStaticText* label)
{
    if (!label)
        return;
    if (std::find(m_wrap_labels.begin(), m_wrap_labels.end(), label) == m_wrap_labels.end())
        m_wrap_labels.push_back(label);
}

void BambuSmartPrintPrivacyDialog::relayout_wrapped_content()
{
    if (!m_scroll)
        return;
    relayout_scroll_wrapped_texts(m_scroll, m_wrap_labels, 52);
    m_scroll->FitInside();
    m_scroll->Layout();
    Layout();
}

void BambuSmartPrintPrivacyDialog::refresh_overview()
{
    auto& db = BambuSmartPrint::FailureDatabase::instance();
    auto& learn = BambuSmartPrint::PrinterLearningStore::instance();

    const int failures = db.count_all_failures();
    const int successes = db.count_all_successes();
    const int recent = db.count_all_failures_recent(30LL * 24 * 3600 * 1000);

    if (m_stat_failures)
        m_stat_failures->SetLabel(wxString::Format("%d", failures));
    if (m_stat_successes)
        m_stat_successes->SetLabel(wxString::Format("%d", successes));
    if (m_stat_recent)
        m_stat_recent->SetLabel(wxString::Format("%d", recent));
    if (m_stat_printers)
        m_stat_printers->SetLabel(wxString::Format("%llu",
            static_cast<unsigned long long>(learn.profile_count())));
    if (m_stat_disk) {
        const uintmax_t bytes = directory_size_bytes(fs::path(m_storage_path));
        uintmax_t total = bytes;
        if (fs::exists(m_catalog_path))
            total += directory_size_bytes(fs::path(m_catalog_path));
        m_stat_disk->SetLabel(format_storage_size(total));
    }

    bool show_load_warning = db.had_load_error() || learn.had_load_error();
    if (m_load_error_banner) {
        if (show_load_warning) {
            wxString detail;
            for (const auto& msg : db.load_error_messages())
                detail << wxString::FromUTF8(msg) << "\n";
            for (const auto& msg : learn.load_error_messages())
                detail << wxString::FromUTF8(msg) << "\n";
            if (!detail.IsEmpty() && m_load_error_text)
                m_load_error_text->SetLabel(
                    wxString::Format(_L("Some Smart Print data could not be loaded:\n\n%s"), detail));
            m_load_error_banner->Show();
        } else {
            m_load_error_banner->Hide();
        }
    }
    relayout_wrapped_content();
}

void BambuSmartPrintPrivacyDialog::refresh_storage_details()
{
    if (m_path_text) {
        const wxString full = wxString::FromUTF8(m_storage_path);
        const int wrap_px = content_wrap_width_px(m_scroll, 520);
        const int approx_chars = std::max(24, wrap_px / std::max(6, m_path_text->GetFont().GetPointSize()));
        m_path_text->SetLabel(ellipsize_path_middle(full, approx_chars));
        m_path_text->SetToolTip(full);
    }
    if (m_catalog_text) {
        namespace fs = boost::filesystem;
        wxString line;
        if (fs::exists(m_catalog_path)) {
            boost::system::error_code ec;
            const uintmax_t sz = fs::file_size(m_catalog_path, ec);
            line = wxString::Format(_L("Custom error catalog: %s (%s)"),
                                    wxString::FromUTF8(m_catalog_path),
                                    format_storage_size(sz));
            m_catalog_text->SetForegroundColour(Theme::text_muted());
        } else {
            line = wxString::Format(_L("Custom error catalog: not created yet (%s)"),
                                    wxString::FromUTF8(m_catalog_path));
            m_catalog_text->SetForegroundColour(Theme::text_muted());
        }
        m_catalog_text->SetLabel(line);
        m_catalog_text->SetToolTip(wxString::FromUTF8(m_catalog_path));
    }
    relayout_wrapped_content();
}

void BambuSmartPrintPrivacyDialog::update_delete_button_state()
{
    if (m_btn_delete && m_delete_confirm_cb)
        m_btn_delete->Enable(m_delete_confirm_cb->GetValue());
}

void BambuSmartPrintPrivacyDialog::on_dpi_changed(const wxRect& /*suggested_rect*/)
{
    refresh_storage_details();
    if (m_scroll)
        m_scroll->SetScrollRate(0, FromDIP(10));
    relayout_wrapped_content();
}

}} // namespace

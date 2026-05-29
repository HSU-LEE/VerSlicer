#include "BambuSmartPrintCompareDialog.hpp"
#include "BambuSmartPrintUi.hpp"

#include "../I18N.hpp"
#include "../GUI_App.hpp"
#include "../Widgets/Button.hpp"
#include "../Widgets/StateColor.hpp"
#include "libslic3r/BambuSmartPrint/ConfigSnapshot.hpp"
#include "libslic3r/libslic3r.h"

#include <wx/listctrl.h>
#include <wx/statline.h>

namespace Slic3r { namespace GUI {

using namespace SlicePilotUi;

BambuSmartPrintCompareDialog::BambuSmartPrintCompareDialog(wxWindow* parent,
                                                           const DynamicPrintConfig& before,
                                                           const DynamicPrintConfig& after,
                                                           const std::string& title,
                                                           const std::vector<BambuSmartPrint::SettingChange>* change_reasons,
                                                           bool approval_mode)
    : DPIDialog(parent, wxID_ANY,
                title.empty() ? _L("Setting changes") : wxString::FromUTF8(title),
                wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    apply_dialog_chrome(this);

    auto changes = BambuSmartPrint::ConfigSnapshot::diff(before, after);
    if (change_reasons) {
        for (BambuSmartPrint::SettingChange& ch : changes) {
            for (const BambuSmartPrint::SettingChange& src : *change_reasons) {
                if (src.key == ch.key && !src.reason.empty()) {
                    ch.reason = src.reason;
                    break;
                }
            }
        }
    }

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(create_header(this, _L("Setting changes"),
        changes.empty() ? _L("No differences between before and after")
                        : wxString::Format(_L("%zu parameters will be updated"), changes.size())),
        0, wxEXPAND);

    wxBoxSizer* body = nullptr;
    auto* scroll = create_scroll_body(this, &body);
    const int pad = FromDIP(12);

    if (changes.empty()) {
        body->Add(create_empty_state(scroll, _L("No changes"),
            _L("The current configuration already matches the suggested profile.")),
            0, wxEXPAND | wxALL, pad);
        root->Add(scroll, 1, wxEXPAND);
        wxBoxSizer* btn_row = nullptr;
        auto* footer = create_modal_footer(this, &btn_row);
        auto* ok = new Button(footer, _L("OK"));
        style_primary_button(ok);
        size_action_button(footer, ok);
        btn_row->AddStretchSpacer();
        btn_row->Add(ok, 0);
        ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_OK); });
        root->Add(new wxStaticLine(this), 0, wxEXPAND);
        root->Add(footer, 0, wxEXPAND);
        SetSizer(root);
        finalize_modal_dialog(this, wxSize(520, 320), wxSize(600, 380), scroll, 180);
        CentreOnParent();
        wxGetApp().UpdateDlgDarkUI(this);
        return;
    }

    const std::vector<std::pair<wxString, int>> columns = {
        {_L("Setting"), 160},
        {_L("Before"), 140},
        {_L("After"), 140},
        {_L("Why"), 180},
    };
    body->Add(create_orca_list_column_header(scroll, columns), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    auto* list = new wxListCtrl(scroll, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(280)),
                                wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
    style_orca_list(list);
    list->AppendColumn(_L("Setting"), wxLIST_FORMAT_LEFT, FromDIP(160));
    list->AppendColumn(_L("Before"), wxLIST_FORMAT_LEFT, FromDIP(140));
    list->AppendColumn(_L("After"), wxLIST_FORMAT_LEFT, FromDIP(140));
    list->AppendColumn(_L("Why"), wxLIST_FORMAT_LEFT, FromDIP(180));

    for (const auto& c : changes) {
        long idx = list->InsertItem(list->GetItemCount(), wxString::FromUTF8(c.key));
        list->SetItem(idx, 1, wxString::FromUTF8(c.old_value));
        list->SetItem(idx, 2, wxString::FromUTF8(c.new_value));
        list->SetItem(idx, 3, wxString::FromUTF8(c.reason));
        if (approval_mode) {
            list->SetItemBackgroundColour(idx,
                StateColor::darkModeColorFor(wxColour(255, 248, 230)));
        }
    }
    body->Add(list, 1, wxEXPAND | wxALL, pad);

    root->Add(scroll, 1, wxEXPAND);

    wxBoxSizer* btn_row = nullptr;
    auto* footer = create_modal_footer(this, &btn_row);
    if (approval_mode) {
        auto* reject = new Button(footer, _L("Reject"));
        style_secondary_button(reject);
        auto* approve = new Button(footer, _L("Approve & apply"));
        style_primary_button(approve);
        size_action_button(footer, reject);
        size_action_button(footer, approve);
        btn_row->AddStretchSpacer();
        btn_row->Add(reject, 0, wxRIGHT, FromDIP(8));
        btn_row->Add(approve, 0);
        reject->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });
        approve->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_OK); });
        SetEscapeId(wxID_CANCEL);
    } else {
        auto* ok = new Button(footer, _L("OK"));
        style_primary_button(ok);
        size_action_button(footer, ok);
        btn_row->AddStretchSpacer();
        btn_row->Add(ok, 0);
        ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_OK); });
    }
    root->Add(new wxStaticLine(this), 0, wxEXPAND);
    root->Add(footer, 0, wxEXPAND);

    SetSizer(root);
    finalize_modal_dialog(this, wxSize(660, 460), wxSize(760, 540), scroll, 320);
    CentreOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void BambuSmartPrintCompareDialog::on_dpi_changed(const wxRect& /*suggested_rect*/) {}

}} // namespace

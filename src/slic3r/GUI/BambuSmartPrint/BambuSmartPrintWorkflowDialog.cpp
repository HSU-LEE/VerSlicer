#include "BambuSmartPrintWorkflowDialog.hpp"
#include "BambuSmartPrintUi.hpp"

#include "../I18N.hpp"
#include "../GUI_App.hpp"
#include "../Widgets/Button.hpp"
#include "../Widgets/Label.hpp"
#include "libslic3r/libslic3r.h"

#include <wx/gauge.h>
#include <wx/scrolwin.h>
#include <wx/statline.h>

namespace Slic3r { namespace GUI {

using namespace SlicePilotUi;

BambuSmartPrintWorkflowDialog::BambuSmartPrintWorkflowDialog(wxWindow* parent, const SmartPrintWorkflowContent& content)
    : DPIDialog(parent, wxID_ANY, _L("Smart Print"),
                wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    apply_dialog_chrome(this);

    wxString header_sub;
    if (content.is_failure_workflow)
        header_sub = _L("Review diagnosis and suggested fixes");
    else if (content.is_smart_slice_result)
        header_sub = _L("Smart slice results for the current plate");
    else
        header_sub.clear();

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->Add(create_header(this, _L("Smart Print"), header_sub, true), 0, wxEXPAND);

    wxBoxSizer* body = nullptr;
    auto* scroll = create_scroll_body(this, &body);
    const int pad = FromDIP(12);

    auto add_block = [&](const wxString& label, const wxString& text) {
        wxBoxSizer* inner = nullptr;
        wxPanel* card_body = nullptr;
        auto* card = create_card(scroll, &inner, &card_body, 12);
        add_card_section_title(card_body, inner, label, {});
        auto* val = new wxStaticText(card_body, wxID_ANY, text);
        style_body_text(val, true);
        wrap_static_text(val, scroll, 480);
        inner->Add(val, 0, wxEXPAND);
        body->Add(card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
    };

    if (content.show_success_gauge && !content.is_smart_slice_result) {
        const int rate = int(std::round(content.success_rate));
        wxString headline = content.readiness_headline.empty()
            ? wxString::FromUTF8(content.prediction_summary)
            : wxString::FromUTF8(content.readiness_headline);
        body->Add(create_readiness_meter(scroll, rate, headline), 0,
                  wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
    }

    add_block(_L("Summary"), wxString::FromUTF8(content.summary));

    if (!content.suggested_material.empty()) {
        wxString mat = wxString::FromUTF8(content.suggested_material);
        if (content.complexity_score > 0)
            mat += wxString::Format(_L(" · complexity %d/100"), content.complexity_score);
        add_block(_L("Suggested material"), mat);
    }

    if (content.filament_mismatch) {
        wxStaticText* warn_txt = nullptr;
        auto* warn = create_banner(scroll, &warn_txt, BannerKind::Warning);
        warn_txt->SetLabel(wxString::Format(
            _L("Active filament (%s) differs from geometry recommendation (%s)."),
            wxString::FromUTF8(content.active_filament),
            wxString::FromUTF8(content.suggested_material)));
        body->Add(warn, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
    }

    if (!content.diagnosis_title.empty())
        add_block(_L("Diagnosis"), wxString::FromUTF8(content.diagnosis_title));

    if (content.diagnosis_uncertain) {
        wxStaticText* warn_txt = nullptr;
        auto* warn = create_banner(scroll, &warn_txt, BannerKind::Warning);
        warn_txt->SetLabel(
            _L("This failure is not fully mapped in the error catalog. "
               "Verify HMS codes on the printer before applying fixes."));
        body->Add(warn, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
    }

    if (!content.insights.empty()) {
        wxBoxSizer* ins_inner = nullptr;
        wxPanel* ins_body = nullptr;
        auto* ins_card = create_card(scroll, &ins_inner, &ins_body, 12);
        add_card_section_title(ins_body, ins_inner, _L("Model insights"), {});
        add_insight_list(ins_body, ins_inner, content.insights, 32);
        body->Add(ins_card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
    }

    if (content.is_failure_workflow) {
        if (!content.diagnosis_title.empty())
            add_block(_L("Failure diagnosis"), wxString::FromUTF8(content.diagnosis_title));
        if (!content.prediction_summary.empty())
            add_block(_L("Details"), wxString::FromUTF8(content.prediction_summary));
        if (content.diagnosis_confidence > 0.f) {
            add_block(_L("Confidence"),
                wxString::Format(_L("%d%%"), int(std::round(content.diagnosis_confidence * 100.f))));
        }
        if (content.diagnosis_uncertain) {
            add_block(_L("Improve future diagnosis"),
                _L("Add MC error codes under Smart Print → Privacy & data. HMS codes are listed below when available."));
        }
    }

    if (!content.change_preview.empty()) {
        wxBoxSizer* ch_inner = nullptr;
        wxPanel* ch_body = nullptr;
        auto* ch_card = create_card(scroll, &ch_inner, &ch_body, 12);
        add_card_section_title(ch_body, ch_inner,
            wxString::Format(_L("Planned adjustments (%zu)"), content.change_preview.size()), {});
        for (const std::string& line : content.change_preview) {
            auto* row = new wxStaticText(ch_body, wxID_ANY, wxString::FromUTF8("• " + line),
                                         wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
            style_body_text(row, true);
            wrap_static_text(row, scroll, 480);
            ch_inner->Add(row, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
        }
        body->Add(ch_card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
    }

    if (!content.risk_factors.empty()) {
        wxString factors;
        for (const std::string& f : content.risk_factors)
            factors << "• " << wxString::FromUTF8(f) << "\n";
        add_block(_L("Things to watch"), factors);
    }

    wxBoxSizer* hint_inner = nullptr;
    wxPanel* hint_body = nullptr;
    auto* hint_card = create_card(scroll, &hint_inner, &hint_body, 12);
    auto* hint = new wxStaticText(hint_body, wxID_ANY, {});
    wrap_static_text(hint, scroll, 480);
    if (content.change_count > 0) {
        hint->SetLabel(wxString::Format(
            _L("%zu setting change(s) suggested. Preview the diff before applying."),
            content.change_count));
        style_body_text(hint, false);
    } else {
        hint->SetLabel(_L("No setting changes recommended. You can close this dialog and continue slicing."));
        style_body_text(hint, true);
    }
    hint_inner->Add(hint, 0, wxEXPAND);
    body->Add(hint_card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    root->Add(scroll, 1, wxEXPAND);

    wxBoxSizer* btn_row = nullptr;
    auto* footer = create_modal_footer(this, &btn_row);

    auto* btn_later = new Button(footer, _L("Not now"));
    style_dialog_button(btn_later, false);
    size_action_button(footer, btn_later);

    auto* btn_preview = new Button(footer, _L("Preview changes"));
    style_dialog_button(btn_preview, false);
    size_action_button(footer, btn_preview);

    const bool has_changes = content.change_count > 0;
    wxString apply_label;
    if (content.is_smart_slice_result)
        apply_label = has_changes ? _L("Apply & re-slice") : _L("Got it");
    else if (content.is_failure_workflow)
        apply_label = has_changes ? _L("Apply fixes & reprint") : _L("Reprint");
    else
        apply_label = has_changes ? _L("Apply & slice") : _L("Got it");
    auto* btn_apply = new Button(footer, apply_label);
    style_dialog_button(btn_apply, has_changes);
    size_action_button(footer, btn_apply);

    btn_row->AddStretchSpacer();
    btn_row->Add(btn_later, 0, wxRIGHT, FromDIP(8));
    if (has_changes)
        btn_row->Add(btn_preview, 0, wxRIGHT, FromDIP(8));
    btn_row->Add(btn_apply, 0);
    root->Add(new wxStaticLine(this), 0, wxEXPAND);
    root->Add(footer, 0, wxEXPAND);

    btn_later->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { m_apply = false; EndModal(wxID_CANCEL); });
    btn_preview->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { m_preview = true; EndModal(wxID_OK); });
    btn_apply->Bind(wxEVT_BUTTON, [this, has_changes](wxCommandEvent&) {
        m_apply = has_changes;
        EndModal(wxID_OK);
    });

    SetSizer(root);
    finalize_modal_dialog(this, wxSize(640, 540), wxSize(780, 660), scroll, 340);
    CentreOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void BambuSmartPrintWorkflowDialog::confirm_auto_apply()
{
    m_preview = false;
    m_apply   = true;
}

void BambuSmartPrintWorkflowDialog::on_dpi_changed(const wxRect& /*suggested_rect*/) {}

}} // namespace

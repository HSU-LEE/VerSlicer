#include "BambuSmartPrintPrepareBar.hpp"
#include "BambuSmartPrintService.hpp"
#include "BambuSmartPrintUi.hpp"
#include "FirstPrintExperience.hpp"
#include "PrintReadinessGate.hpp"
#include "SlicePilotSetupHub.hpp"
#include "SlicePilotOnboardingFunnel.hpp"

#include "../GUI_App.hpp"
#include "../MainFrame.hpp"
#include "../I18N.hpp"
#include "../Plater.hpp"
#include "../Widgets/Button.hpp"
#include "../Widgets/Label.hpp"
#include "../Widgets/StaticLine.hpp"

#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/statline.h>

namespace Slic3r { namespace GUI {

using namespace SlicePilotUi;

BambuSmartPrintPrepareBar::BambuSmartPrintPrepareBar(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    BambuSmartPrintService::instance().register_prepare_bar(this);
    on_color_mode_changed(wxGetApp().app_config->get("dark_color_mode") == "1");

    const int pad_h = FromDIP(6);
    const int gap   = FromDIP(4);

    auto* root = new wxBoxSizer(wxHORIZONTAL);

    auto* title_line = new StaticLine(this, false, _L("Smart Print"));
    title_line->SetFont(Label::Body_12);
    title_line->SetForegroundColour(Theme::text());
    title_line->SetLineColour(Theme::border());
    root->Add(title_line, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, pad_h);

    auto* title_sep = new wxStaticLine(this, wxID_ANY, wxDefaultPosition,
                                       FromDIP(wxSize(1, 14)), wxLI_VERTICAL);
    title_sep->SetForegroundColour(Theme::border());
    root->Add(title_sep, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, pad_h);

    m_setup_hub = new SlicePilotSetupHub(this);
    root->Add(m_setup_hub, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, pad_h);

    m_status = new wxStaticText(this, wxID_ANY, _L("Load a model, then press Print"));
    m_status->SetFont(Label::Body_12);
    m_status->SetForegroundColour(Theme::text_muted());
    root->Add(m_status, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, pad_h);

    m_failure_hint = new wxStaticText(this, wxID_ANY, wxString{});
    m_failure_hint->SetFont(Label::Body_12);
    m_failure_hint->SetForegroundColour(Theme::warning());
    m_failure_hint->Hide();
    root->Add(m_failure_hint, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);

    m_first_failure_detail = new wxStaticText(this, wxID_ANY, wxString{});
    m_first_failure_detail->SetFont(Label::Body_10);
    m_first_failure_detail->SetForegroundColour(Theme::text_muted());
    m_first_failure_detail->Hide();
    root->Add(m_first_failure_detail, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);

    m_failure_reprint_btn = new Button(this, _L("Reprint"));
    style_prepare_toolbar_button(m_failure_reprint_btn, true);
    size_prepare_strip_button(this, m_failure_reprint_btn);
    m_failure_reprint_btn->Hide();
    root->Add(m_failure_reprint_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);

    m_filament_fix_btn = new Button(this, _L("Fix filament"));
    style_prepare_toolbar_button(m_filament_fix_btn, false);
    size_prepare_strip_button(this, m_filament_fix_btn);
    m_filament_fix_btn->Hide();
    root->Add(m_filament_fix_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);

    m_print_btn = new Button(this, _L("Print"));
    style_primary_button(m_print_btn);
    m_print_btn->SetPaddingSize(FromDIP(wxSize(10, 4)));
    m_print_btn->SetCornerRadius(FromDIP(6));
    m_print_btn->SetFont(Label::Body_12);
  {
        wxClientDC dc(m_print_btn);
        dc.SetFont(m_print_btn->GetFont());
        wxSize te;
        dc.GetTextExtent(m_print_btn->GetLabel(), &te.x, &te.y);
        const int h = te.y + FromDIP(10);
        const int w = te.x + FromDIP(24);
        m_print_btn->SetMinSize(wxSize(w, h));
        m_print_btn->SetMaxSize(wxSize(w, h));
    }
    root->Add(m_print_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);

    m_ai_btn = new Button(this, _L("AI"));
    style_prepare_toolbar_button(m_ai_btn, false);
    size_prepare_strip_button(this, m_ai_btn);
    root->Add(m_ai_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);

    m_more_btn = new Button(this, wxString::FromUTF8("⋯"));
    style_prepare_toolbar_button(m_more_btn, false);
    size_prepare_strip_button(this, m_more_btn);
    root->Add(m_more_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, pad_h);

    auto* sep = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
    sep->SetForegroundColour(Theme::border());
    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(root, 0, wxEXPAND);
    outer->Add(sep, 0, wxEXPAND);
    SetSizer(outer);
    SetMinSize(FromDIP(wxSize(-1, 36)));
    SetMaxSize(FromDIP(wxSize(-1, 44)));

    bind_actions();
    refresh();
}

BambuSmartPrintPrepareBar::~BambuSmartPrintPrepareBar()
{
    BambuSmartPrintService::instance().unregister_prepare_bar(this);
}

void BambuSmartPrintPrepareBar::bind_actions()
{
    m_print_btn->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
        if (Plater* plater = wxGetApp().plater())
            BambuSmartPrintService::instance().run_one_click_print(plater);
    });
    m_ai_btn->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
        if (MainFrame* frame = wxGetApp().mainframe)
            frame->jump_to_smart_print();
    });
    m_more_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { show_more_menu(); });
    m_failure_reprint_btn->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
        if (Plater* plater = wxGetApp().plater())
            BambuSmartPrintService::instance().run_reprint_with_failure_fixes(plater);
    });
    m_filament_fix_btn->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
        if (Plater* plater = wxGetApp().plater())
            PrintReadinessGate::try_fix_filament_mismatch(plater);
    });

    m_status->SetToolTip(_L("Print: analyze, apply Smart Print settings, slice, then send to the printer."));
    m_print_btn->SetToolTip(_L("One-click: apply suggestions, slice, then send (or slice only without the network plug-in)"));
    m_more_btn->SetToolTip(_L("Sample model, bed fit, Smart Print panel, and more"));
}

void BambuSmartPrintPrepareBar::show_more_menu()
{
    enum {
        ID_SAMPLE = wxID_HIGHEST + 1,
        ID_FIX,
        ID_REPRINT,
        ID_SETTINGS,
        ID_PANEL,
    };

    wxMenu menu;
    menu.Append(ID_SAMPLE, _L("Load sample model"));
    menu.Append(ID_FIX, _L("Fix bed fit"));
    if (BambuSmartPrintService::instance().has_reprintable_failure())
        menu.Append(ID_REPRINT, _L("Reprint with fixes"));
    menu.AppendSeparator();
    menu.Append(ID_SETTINGS, _L("Smart Print settings"));
    menu.Append(ID_PANEL, _L("Open Smart Print tab"));

    const int cmd = GetPopupMenuSelectionFromUser(menu, wxDefaultPosition);
    if (cmd == wxID_NONE)
        return;

    Plater* plater = wxGetApp().plater();
    switch (cmd) {
    case ID_SAMPLE:
        if (plater)
            FirstPrintExperience::open_first_print_sample(plater);
        break;
    case ID_FIX:
        if (plater)
            FirstPrintExperience::apply_bed_fit_fix(plater);
        break;
    case ID_REPRINT:
        if (plater)
            BambuSmartPrintService::instance().run_reprint_with_failure_fixes(plater);
        break;
    case ID_SETTINGS:
        wxGetApp().open_smart_print();
        break;
    case ID_PANEL:
        wxGetApp().open_smart_print();
        break;
    default:
        break;
    }
}

void BambuSmartPrintPrepareBar::on_color_mode_changed(bool /*is_dark*/)
{
    SetBackgroundColour(prepare_strip_background());
    if (m_status)
        m_status->SetForegroundColour(Theme::text_muted());
    if (m_failure_hint)
        m_failure_hint->SetForegroundColour(Theme::warning());
    Refresh();
}

void BambuSmartPrintPrepareBar::update_failure_action_card()
{
    auto& svc = BambuSmartPrintService::instance();
    const bool storage_err = svc.has_storage_save_error();
    const bool has_failure = svc.has_reprintable_failure();

    if (storage_err && m_failure_hint) {
        m_failure_hint->SetLabel(_L("⚠ Smart Print data could not be saved"));
        m_failure_hint->SetToolTip(_L("Check disk space and folder permissions under Privacy & data."));
        m_failure_hint->Show();
        if (m_failure_reprint_btn)
            m_failure_reprint_btn->Hide();
        return;
    }

    if (has_failure && m_failure_hint) {
        m_failure_hint->SetLabel(svc.pending_failure_action_line());
        m_failure_hint->SetToolTip(svc.pending_failure_summary());
        m_failure_hint->Show();
        if (m_failure_reprint_btn)
            m_failure_reprint_btn->Show();
        if (m_first_failure_detail && !SlicePilotOnboardingFunnel::first_reprint()) {
            m_first_failure_detail->SetLabel(
                _L("Smart Print detected a failure — Reprint applies suggested fixes."));
            m_first_failure_detail->Show();
        } else if (m_first_failure_detail) {
            m_first_failure_detail->Hide();
        }
        return;
    }

    if (m_first_failure_detail)
        m_first_failure_detail->Hide();

    if (m_failure_hint)
        m_failure_hint->Hide();
    if (m_failure_reprint_btn)
        m_failure_reprint_btn->Hide();
}

void BambuSmartPrintPrepareBar::refresh_action_state()
{
    const bool bbl     = BambuSmartPrintService::is_bbl_printer_active();
    const bool busy    = BambuSmartPrintService::instance().one_click_print_active();
    const bool has_failure = BambuSmartPrintService::instance().has_reprintable_failure();
    const auto& mesh     = BambuSmartPrintService::instance().last_mesh_analysis();
    const auto& ready    = BambuSmartPrintService::instance().last_readiness_report();

    if (m_filament_fix_btn) {
        const bool show_filament = bbl && ready.filament_mismatch && !busy;
        if (show_filament)
            m_filament_fix_btn->SetLabel(PrintReadinessGate::filament_fix_button_label());
        m_filament_fix_btn->Show(show_filament);
    }
    if (m_print_btn)
        m_print_btn->Enable(bbl && !busy);
    if (m_print_btn && bbl && !busy)
        m_print_btn->SetLabel(SlicePilotSetupHub::print_button_label());
    if (m_more_btn)
        m_more_btn->Enable(!busy);

    update_failure_action_card();
}

void BambuSmartPrintPrepareBar::refresh()
{
    bool has_model = false;
    Plater* plater = wxGetApp().plater();
    if (plater) {
        try {
            BambuSmartPrintService::instance().refresh_plate_snapshot(plater);
        } catch (...) {
        }
        const auto& mesh = BambuSmartPrintService::instance().last_mesh_analysis();
        has_model = mesh.volume_mm3 > 0.0;
    }

    if (m_setup_hub)
        m_setup_hub->refresh(plater);

    refresh_action_state();

    const auto& ready = BambuSmartPrintService::instance().last_readiness_report();
    const wxString estimate = BambuSmartPrintService::instance().last_slice_estimate_text();

    if (m_status) {
        const auto phase = BambuSmartPrintService::instance().one_click_phase();
        if (phase != BambuSmartPrintService::OneClickPhase::None) {
            m_status->SetLabel(one_click_phase_status_text(phase));
        } else if (!BambuSmartPrintService::is_bbl_printer_active()) {
            m_status->SetLabel(_L("Select a Bambu Lab printer profile"));
        } else if (BambuSmartPrintService::instance().has_reprintable_failure()) {
            m_status->SetLabel(_L("Print failed — use Reprint or open ⋯ for options"));
        } else if (ready.filament_mismatch) {
            wxString line = wxString::Format(_L("Filament mismatch — %d%% ready"),
                static_cast<int>(ready.score + 0.5f));
            if (!estimate.empty())
                line += wxString(" · ") + estimate;
            m_status->SetLabel(line);
        } else {
            wxString line = format_prepare_bar_status(ready, has_model, estimate);
            if (has_model && ready.score > 0.f) {
                const int pct = static_cast<int>(ready.score + 0.5f);
                if (!line.Contains(wxString::Format("%d", pct)))
                    line = wxString::Format(_L("%d%% · %s"), pct, line);
            }
            m_status->SetLabel(line);
        }
    }
}

}} // namespace

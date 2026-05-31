#include "BambuSmartPrintPanel.hpp"
#include "BambuSmartPrintService.hpp"
#include "BambuSmartPrintUi.hpp"
#include "SlicePilotOnboardingFunnel.hpp"

#include "../I18N.hpp"
#include "../GUI_App.hpp"
#include "../Plater.hpp"
#include "../DeviceCore/DevManager.h"
#include "slic3r/Utils/NetworkAgent.hpp"
#include "../Widgets/Button.hpp"
#include "../Widgets/CheckBox.hpp"
#include "../Widgets/ComboBox.hpp"
#include "../Widgets/Label.hpp"

#include "libslic3r/BambuSmartPrint/FailureDatabase.hpp"
#include "libslic3r/BambuSmartPrint/PrinterLearningStore.hpp"
#include "libslic3r/libslic3r.h"

#include <wx/sizer.h>
#include <wx/statline.h>

#include <boost/log/trivial.hpp>

namespace Slic3r { namespace GUI {

using namespace SlicePilotUi;

BambuSmartPrintPanel::BambuSmartPrintPanel(wxWindow* parent, LayoutStyle style)
    : wxPanel(parent, wxID_ANY)
    , m_style(style)
{
    const bool in_preferences = (m_style == LayoutStyle::Preferences);

    if (in_preferences)
        BambuSmartPrintService::instance().register_preferences_panel(this);
    else
        BambuSmartPrintService::instance().register_main_panel(this);
    apply_panel_chrome(this);

    auto* root = new wxBoxSizer(wxVERTICAL);

    if (in_preferences)
        root->Add(create_header(this, _L("Smart Print"), wxString{}, true), 0, wxEXPAND);
    else
        root->Add(create_header(this, _L("Smart Print"), wxString{}, false), 0, wxEXPAND);

    wxBoxSizer* body = nullptr;
    wxScrolledWindow* scroll = nullptr;
    wxWindow* content = this;
    if (in_preferences) {
        body = new wxBoxSizer(wxVERTICAL);
    } else {
        scroll = create_scroll_body(this, &body);
        content = scroll;
    }
    const int gap = FromDIP(12);
    const int side = content_side_margin_dip(this, in_preferences);

    m_scope_banner = create_banner(content, &m_scope_notice, BannerKind::Warning);
    m_scope_banner->Hide();
    body->Add(m_scope_banner, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, side);

    body->Add(create_readiness_hero(content, true,
        &m_hero_score, &m_hero_gauge, &m_hero_headline,
        &m_stat_failures, &m_stat_successes, &m_stat_recent, true),
        0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, side);

    wxBoxSizer* workflow_inner = nullptr;
    wxPanel* workflow_body = nullptr;
    auto* workflow_card = create_card(content, &workflow_inner, &workflow_body, 12);
    add_card_section_title(workflow_body, workflow_inner, _L("Plate workflow"),
        _L("Analyze settings, then slice the plate"));
    {
        std::vector<Button*> workflow_actions;
        workflow_inner->Add(create_workflow_stack(workflow_body,
            {
                { 1, _L("Analyze"), _L("Check geometry and settings for this plate"), _L("Analyze") },
                { 2, _L("Smart slice"), _L("Apply suggestions and slice the plate"), _L("Slice") },
            },
            workflow_actions),
            0, wxEXPAND | wxBOTTOM, FromDIP(8));
        if (workflow_actions.size() >= 2) {
            m_analyze_btn      = workflow_actions[0];
            m_smart_slice_btn  = workflow_actions[1];
        }
    }

    m_print_btn = new Button(workflow_body, _L("Print"));
    style_dialog_button(m_print_btn, true);
    size_action_button(workflow_body, m_print_btn, 34);

    m_analyze_all_btn = new Button(workflow_body, _L("All plates"));
    m_quick_apply_btn = new Button(workflow_body, _L("Quick apply"));
    m_open_details_btn = new Button(workflow_body, _L("Review settings"));
    m_export_btn = new Button(workflow_body, _L("Export report"));
    for (Button* b : { m_analyze_all_btn, m_quick_apply_btn, m_open_details_btn, m_export_btn })
        style_dialog_button(b, false);
    for (Button* b : { m_analyze_all_btn, m_quick_apply_btn, m_open_details_btn, m_export_btn })
        size_action_button(workflow_body, b, 34);
    add_tool_button_rows(workflow_body, workflow_inner, { m_print_btn }, 1, true);
    add_tool_button_rows(workflow_body, workflow_inner,
        { m_analyze_all_btn, m_quick_apply_btn, m_open_details_btn, m_export_btn }, 2, true);
    body->Add(workflow_card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);

    if (in_preferences) {
        wxBoxSizer* net_inner = nullptr;
        wxPanel* net_body = nullptr;
        auto* net_card = create_card(content, &net_inner, &net_body, 12);
        add_card_section_title(net_body, net_inner, _L("Connection status"),
            _L("Smart Print learning from failures needs a Bambu printer on the network"));
        auto add_net_row = [&](const wxString& label, wxStaticText** out) {
            auto* row = new wxBoxSizer(wxHORIZONTAL);
            auto* lbl = new wxStaticText(net_body, wxID_ANY, label);
            style_body_text(lbl, true);
            row->Add(lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
            *out = new wxStaticText(net_body, wxID_ANY, "—");
            style_body_text(*out, false);
            row->Add(*out, 1, wxEXPAND);
            net_inner->Add(row, 0, wxEXPAND | wxBOTTOM, FromDIP(6));
        };
        add_net_row(_L("Network plug-in:"), &m_network_plugin_status);
        add_net_row(_L("Printers found:"), &m_network_printer_status);
        add_net_row(_L("Bambu account:"), &m_network_account_status);
        body->Add(net_card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);
    }

    wxBoxSizer* insights_inner = nullptr;
    m_insights_card = create_card(content, &m_insights_inner, &m_insights_body, 12);
    add_card_section_title(m_insights_body, m_insights_inner, _L("Model insights"), _L("Current plate"));
    m_insights_inner->Add(create_empty_state_subtle(m_insights_body, _L("No model on plate"), {}),
        0, wxEXPAND);
    body->Add(m_insights_card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);

    m_filament_banner = create_banner(content, &m_filament_notice, BannerKind::Warning);
    m_filament_banner->Hide();
    body->Add(m_filament_banner, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);

    wxBoxSizer* account_inner = nullptr;
    wxPanel* account_body = nullptr;
    auto* account_card = create_card(content, &account_inner, &account_body, 12);
    add_card_section_title(account_body, account_inner, _L("Bambu Lab account"),
        _L("Required to sync printers"));
    m_account_status = new wxStaticText(account_body, wxID_ANY, "");
    style_body_text(m_account_status, true);
    account_inner->Add(m_account_status, 0, wxEXPAND | wxBOTTOM, FromDIP(10));

    auto* status_row = new wxBoxSizer(wxHORIZONTAL);
    m_account_badge = new wxStaticText(account_body, wxID_ANY, "");
    m_account_badge->Hide();

    auto* login = new Button(account_body, _L("Sign in"));
    style_dialog_button(login, true);
    size_action_button(account_body, login);
    m_login_btn = login;
    m_login_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        BambuSmartPrintService::instance().prompt_bambu_login(this);
        refresh_all();
    });

    m_logout_btn = new Button(account_body, _L("Sign out"));
    style_dialog_button(m_logout_btn, false);
    size_action_button(account_body, m_logout_btn);
    m_logout_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        BambuSmartPrintService::instance().prompt_bambu_logout();
        refresh_all();
    });

    status_row->AddStretchSpacer();
    status_row->Add(m_login_btn, 0, wxRIGHT, FromDIP(6));
    status_row->Add(m_logout_btn, 0);
    account_inner->Add(status_row, 0, wxEXPAND);
    body->Add(account_card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);

    wxBoxSizer* assistant_inner = nullptr;
    wxPanel* assistant_body = nullptr;
    auto* assistant_card = create_card(content, &assistant_inner, &assistant_body, 12);
    add_card_section_title(assistant_body, assistant_inner, _L("Assistant"), {});
    add_settings_checkbox_row(assistant_body, assistant_inner,
        _L("Enable Smart Print recommendations"), &m_enable_cb,
        _L("Show readiness hints and suggested setting changes for the current plate"));
    m_enable_cb->SetValue(BambuSmartPrintService::is_enabled());
    m_enable_cb->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& e) {
        BambuSmartPrintService::set_enabled(m_enable_cb->GetValue());
        refresh_action_state();
        e.Skip();
    });

    add_settings_combobox_row(assistant_body, assistant_inner, _L("When a model is loaded"),
        &m_auto_load_choice,
        {
            _L("Do not show automatically"),
            _L("Brief notification (recommended)"),
            _L("Apply AI recommendations (Ollama)"),
        },
        _L("How the assistant responds after you load a model on the plate"));
    {
        const int mode = int(BambuSmartPrintService::auto_load_mode());
        m_auto_load_choice->SetSelection(std::max(0, std::min(2, mode)));
    }
    m_auto_load_choice->GetDropDown().Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& e) {
        BambuSmartPrintService::set_auto_load_mode(
            static_cast<BambuSmartPrintService::AutoLoadMode>(e.GetSelection()));
        e.Skip();
    });

    add_settings_checkbox_row(assistant_body, assistant_inner,
        _L("Apply printer learning automatically after failures"), &m_learning_auto_cb,
        _L("Apply per-printer tuning learned from past failures when a new diagnosis is shown"));
    m_learning_auto_cb->SetValue(BambuSmartPrintService::learning_auto_apply());
    m_learning_auto_cb->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& e) {
        BambuSmartPrintService::set_learning_auto_apply(m_learning_auto_cb->GetValue());
        refresh_printer_learning();
        e.Skip();
    });

    add_settings_checkbox_row(assistant_body, assistant_inner,
        _L("Safe mode — limit risky temperature/speed changes"), &m_safe_mode_cb,
        _L("Cap aggressive suggestions that could affect print stability"));
    m_safe_mode_cb->SetValue(BambuSmartPrintService::safe_mode_enabled());
    m_safe_mode_cb->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& e) {
        BambuSmartPrintService::set_safe_mode_enabled(m_safe_mode_cb->GetValue());
        e.Skip();
    });

    m_rollback_btn = new Button(assistant_body, _L("Rollback last apply"));
    m_scan_btn = new Button(assistant_body, _L("Scan Bambu printers"));
    m_load_profile_btn = new Button(assistant_body, _L("Load printer profile"));
    for (Button* b : { m_rollback_btn, m_scan_btn, m_load_profile_btn }) {
        style_dialog_button(b, false);
        size_action_button(assistant_body, b, 34);
    }
    m_rollback_btn->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
        if (Plater* plater = wxGetApp().plater())
            BambuSmartPrintService::instance().rollback_last_apply(plater);
    });
    m_scan_btn->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
        BambuSmartPrintService::instance().scan_bambu_printers_on_network();
    });
    m_load_profile_btn->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
        if (Plater* plater = wxGetApp().plater())
            BambuSmartPrintService::instance().load_printer_profile_for_selected_device(plater);
    });
    add_tool_button_rows(assistant_body, assistant_inner,
        { m_rollback_btn, m_scan_btn, m_load_profile_btn }, 3, true);

    body->Add(assistant_card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);

    wxBoxSizer* history_inner = nullptr;
    wxPanel* history_body = nullptr;
    auto* history_card = create_card(content, &history_inner, &history_body, 12);
    add_card_section_title(history_body, history_inner, _L("Print history"), {});
    m_history_btn = new Button(history_body, _L("View history"));
    style_dialog_button(m_history_btn, true);
    size_action_button(history_body, m_history_btn, 34);
    m_history_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        BambuSmartPrintService::instance().show_history_dialog(this);
        refresh_stats();
    });
    m_privacy_btn = new Button(history_body, _L("Privacy & data"));
    style_dialog_button(m_privacy_btn, false);
    size_action_button(history_body, m_privacy_btn, 34);
    m_privacy_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        BambuSmartPrintService::instance().show_privacy_dialog(this);
    });
    add_tool_button_rows(history_body, history_inner, { m_history_btn, m_privacy_btn }, 2, true);
    body->Add(history_card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);

    wxBoxSizer* learn_inner = nullptr;
    wxPanel* learn_body = nullptr;
    auto* learn_card = create_card(content, &learn_inner, &learn_body, 12);
    add_card_section_title(learn_body, learn_inner, _L("Printer learning"), {});
    m_printer_learning = new wxStaticText(learn_body, wxID_ANY, "");
    style_body_text(m_printer_learning, true);
    learn_inner->Add(m_printer_learning, 0, wxEXPAND);
    body->Add(learn_card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);

    if (m_analyze_btn) {
        m_analyze_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (Plater* plater = wxGetApp().plater()) {
                BambuSmartPrintService::instance().analyze_current_plate(plater);
                refresh_model_snapshot();
            }
        });
    }
    if (m_smart_slice_btn) {
        m_smart_slice_btn->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
            if (Plater* plater = wxGetApp().plater())
                BambuSmartPrintService::instance().run_smart_slice(plater);
        });
    }
    m_print_btn->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
        if (Plater* plater = wxGetApp().plater())
            BambuSmartPrintService::instance().run_one_click_print(plater);
    });
    m_analyze_all_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (Plater* plater = wxGetApp().plater()) {
            BambuSmartPrintService::instance().analyze_all_plates(plater);
            refresh_all();
        }
    });
    m_quick_apply_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (Plater* plater = wxGetApp().plater()) {
            BambuSmartPrintService::instance().quick_apply_current_plate(plater, this);
            refresh_model_snapshot();
        }
    });
    m_open_details_btn->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
        if (Plater* plater = wxGetApp().plater())
            BambuSmartPrintService::instance().open_full_workflow_for_current_plate(plater);
    });
    m_export_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (Plater* plater = wxGetApp().plater())
            BambuSmartPrintService::instance().export_current_plate_report(plater, this);
    });
    wxStaticText* tip_txt = nullptr;
    auto* tip_banner = create_banner(content, &tip_txt, BannerKind::Info);
    m_tip_text = tip_txt;
    body->Add(tip_banner, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);
    body->AddSpacer(FromDIP(12));

    if (in_preferences) {
        root->Add(body, 0, wxEXPAND);
    } else {
        root->Add(scroll, 1, wxEXPAND);
    }
    SetSizer(root);

    Bind(wxEVT_SHOW, [this, scroll](wxShowEvent& e) {
        e.Skip();
        if (e.IsShown()) {
            refresh_all();
            if (scroll) {
                scroll->FitInside();
                scroll->Layout();
            }
            if (m_style == LayoutStyle::Preferences)
                refit_for_preferences();
        }
    });
    if (scroll) {
        Bind(wxEVT_SIZE, [scroll](wxSizeEvent& e) {
            e.Skip();
            scroll->FitInside();
        });
    }
    refresh_all();
    if (in_preferences)
        refit_for_preferences();
}

void BambuSmartPrintPanel::update_text_layout()
{
    wrap_static_text(m_account_status, this);
    wrap_static_text(m_tip_text, this);
    if (m_printer_learning)
        wrap_static_text(m_printer_learning, this);
    if (m_scope_notice)
        wrap_static_text(m_scope_notice, this);
    if (m_login_btn)
        size_action_button(this, m_login_btn);
    if (m_logout_btn)
        size_action_button(this, m_logout_btn);
    if (m_history_btn)
        size_action_button(this, m_history_btn);
    if (m_privacy_btn)
        size_action_button(this, m_privacy_btn);
    if (m_analyze_btn)
        size_action_button(this, m_analyze_btn);
    if (m_smart_slice_btn)
        size_action_button(this, m_smart_slice_btn);
    if (m_print_btn)
        size_action_button(this, m_print_btn);
    if (m_analyze_all_btn)
        size_action_button(this, m_analyze_all_btn);
    if (m_quick_apply_btn)
        size_action_button(this, m_quick_apply_btn);
    if (m_open_details_btn)
        size_action_button(this, m_open_details_btn);
    if (m_export_btn)
        size_action_button(this, m_export_btn);
}

void BambuSmartPrintPanel::refit_for_preferences()
{
    if (m_style != LayoutStyle::Preferences)
        return;

    update_text_layout();
    Layout();
    Fit();

    for (wxWindow* w = GetParent(); w; w = w->GetParent()) {
        if (auto* scrolled = dynamic_cast<wxScrolledWindow*>(w)) {
            scrolled->FitInside();
            scrolled->Layout();
            break;
        }
    }
}

BambuSmartPrintPanel::~BambuSmartPrintPanel()
{
    if (m_style == LayoutStyle::Preferences)
        BambuSmartPrintService::instance().unregister_preferences_panel(this);
    else
        BambuSmartPrintService::instance().unregister_main_panel(this);
}

void BambuSmartPrintPanel::refresh_account_status()
{
    if (!m_account_status)
        return;

    const bool logged_in = BambuSmartPrintService::instance().is_bambu_account_logged_in();
    m_account_status->SetLabel(BambuSmartPrintService::instance().bambu_account_status_text());

    if (m_account_status) {
        if (logged_in)
            m_account_status->SetForegroundColour(Theme::text());
        else
            m_account_status->SetForegroundColour(Theme::text_muted());
    }

    if (m_login_btn)
        m_login_btn->Show(!logged_in);
    if (m_logout_btn)
        m_logout_btn->Show(logged_in);

    update_text_layout();
    if (m_style == LayoutStyle::Preferences)
        refit_for_preferences();
    else
        Layout();
}

void BambuSmartPrintPanel::refresh_scope_notice()
{
    if (!m_scope_banner || !m_scope_notice)
        return;

    const bool bbl = BambuSmartPrintService::is_bbl_printer_active();
    if (bbl) {
        m_scope_notice->SetLabel({});
        m_scope_banner->Hide();
    } else {
        m_scope_notice->SetLabel(BambuSmartPrintService::bbl_printer_required_message());
        m_scope_banner->Show();
    }
    if (m_style == LayoutStyle::Preferences)
        refit_for_preferences();
    else
        Layout();
}

void BambuSmartPrintPanel::refresh_action_state()
{
    const bool enabled = BambuSmartPrintService::is_enabled();
    const bool bbl     = BambuSmartPrintService::is_bbl_printer_active();
    const bool actions = enabled && bbl;
    if (m_auto_load_choice)
        m_auto_load_choice->Enable(enabled && bbl);
    if (m_learning_auto_cb)
        m_learning_auto_cb->Enable(enabled && bbl);
    if (m_analyze_btn)
        m_analyze_btn->Enable(actions);
    if (m_smart_slice_btn)
        m_smart_slice_btn->Enable(actions);
    if (m_print_btn)
        m_print_btn->Enable(actions);
    if (m_analyze_all_btn)
        m_analyze_all_btn->Enable(actions);
    if (m_quick_apply_btn)
        m_quick_apply_btn->Enable(actions);
    if (m_open_details_btn)
        m_open_details_btn->Enable(actions);
    if (m_export_btn)
        m_export_btn->Enable(actions);
    if (m_history_btn)
        m_history_btn->Enable(enabled);
}

void BambuSmartPrintPanel::refresh_printer_learning()
{
    if (!m_printer_learning)
        return;

    std::string printer_id = "local";
    if (wxGetApp().getDeviceManager()) {
        if (MachineObject* sel = wxGetApp().getDeviceManager()->get_selected_machine())
            printer_id = sel->get_dev_id();
    }

    const BambuSmartPrint::PrinterLearningProfile p =
        BambuSmartPrint::PrinterLearningStore::instance().get_profile(printer_id);

    if (p.total_prints == 0) {
        m_printer_learning->SetLabel(_L("No print history for this printer yet — failures and successes improve future suggestions."));
    } else {
        const int rate = int(std::round(100.f * float(p.successful_prints) / float(p.total_prints)));
        wxString msg = wxString::Format(
            _L("%d jobs tracked · %d%% success · %d failures learned from"),
            p.total_prints, rate, p.failed_prints);
        if (!p.pending_learning.empty())
            msg << wxString::Format(_L("\n%d learning suggestion(s) awaiting approval — open Privacy & data or approve below."),
                                    int(p.pending_learning.size()));
        if (p.applied_learning_count > 0)
            msg << wxString::Format(_L("\n%d Smart Print suggestions applied on this printer."), p.applied_learning_count);
        m_printer_learning->SetLabel(msg);
    }
    wrap_static_text(m_printer_learning, this);
}

void BambuSmartPrintPanel::refresh_model_snapshot()
{
    if (!m_insights_inner || !m_insights_body)
        return;

    try {
        if (Plater* plater = wxGetApp().plater())
            BambuSmartPrintService::instance().update_plate_assessment_data(plater);
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint refresh_model_snapshot: " << ex.what();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint refresh_model_snapshot: unknown error";
    }

    const auto& mesh = BambuSmartPrintService::instance().last_mesh_analysis();
    const auto& ready = BambuSmartPrintService::instance().last_readiness_report();

    auto update_hero = [&](int score, const wxString& headline, bool has_model) {
        if (m_hero_score) {
            if (has_model) {
                m_hero_score->SetLabel(wxString::Format("%d", score));
                m_hero_score->SetForegroundColour(Theme::text_muted());
            } else {
                m_hero_score->SetLabel("—");
                m_hero_score->SetForegroundColour(Theme::text_muted());
            }
        }
        if (m_hero_gauge) {
            if (has_model)
                apply_gauge_score(m_hero_gauge, score);
            else
                apply_gauge_score(m_hero_gauge, 0);
        }
        if (m_hero_headline)
            m_hero_headline->SetLabel(headline);
    };

    if (mesh.volume_mm3 <= 0.0) {
        if (m_hero_score)
            update_hero(0, _L("Load a model on the build plate to analyze"), false);
        if (m_filament_banner)
            m_filament_banner->Hide();
        m_insights_inner->Clear(true);
        m_insights_inner->Add(create_empty_state_subtle(m_insights_body, _L("No model on plate"), {}),
            0, wxEXPAND);
    } else {
        const int score = int(std::round(ready.score));
        wxString headline = ready.headline.empty()
            ? wxString::Format(_L("Suggested %s - complexity %d/100"),
                wxString::FromUTF8(mesh.suggested_material), mesh.complexity_score)
            : format_prepare_bar_status(score, ready.headline, true);
        if (m_hero_score)
            update_hero(score, headline, true);

        if (m_filament_banner && m_filament_notice) {
            if (ready.filament_mismatch) {
                m_filament_notice->SetLabel(wxString::Format(
                    _L("Active filament (%s) differs from recommendation (%s)."),
                    wxString::FromUTF8(ready.active_filament_hint),
                    wxString::FromUTF8(ready.suggested_filament_hint)));
                m_filament_banner->Show();
            } else {
                m_filament_banner->Hide();
            }
        }

        m_insights_inner->Clear(true);
        std::vector<BambuSmartPrint::PrintInsight> insights = ready.insights;
        if (insights.empty()) {
            BambuSmartPrint::PrintInsight hint;
            hint.label  = "Geometry";
            hint.detail = mesh.suggested_material + ", complexity " + std::to_string(mesh.complexity_score) + "/100";
            insights.push_back(std::move(hint));
        }
        add_insight_list(m_insights_body, m_insights_inner, insights, 5);
    }

    if (m_style == LayoutStyle::Preferences)
        refit_for_preferences();
    else
        Layout();
}

void BambuSmartPrintPanel::refresh_stats()
{
    const int failures  = BambuSmartPrint::FailureDatabase::instance().count_all_failures();
    const int successes = BambuSmartPrint::FailureDatabase::instance().count_all_successes();

    int recent_failures = 0;
    if (wxGetApp().getDeviceManager()) {
        if (MachineObject* sel = wxGetApp().getDeviceManager()->get_selected_machine())
            recent_failures = BambuSmartPrint::FailureDatabase::instance().count_failures_recent(sel->get_dev_id());
    }

    if (m_stat_failures)
        m_stat_failures->SetLabel(wxString::Format("%d", failures));
    if (m_stat_successes)
        m_stat_successes->SetLabel(wxString::Format("%d", successes));
    if (m_stat_recent)
        m_stat_recent->SetLabel(wxString::Format("%d", recent_failures));

    refresh_tip();
}

void BambuSmartPrintPanel::refresh_tip()
{
    if (!m_tip_text)
        return;
    wxString line;
    if (!BambuSmartPrintService::is_enabled()) {
        line = _L("Print = full one-click flow. Analyze = preview only. Smart slice = apply + slice.");
    } else if (BambuSmartPrintService::instance().is_bambu_account_logged_in())
        line = _L("Print on the Prepare bar runs analyze, apply, slice, and send. Analyze previews without slicing.");
    else
        line = _L("Sign in to Bambu Lab to sync printers and record failures for learning.");

    line << "\n\n" << _L("Setup progress (local):") << "\n" << SlicePilotOnboardingFunnel::summary_text();
    m_tip_text->SetLabel(line);
}

void BambuSmartPrintPanel::refresh_network_status()
{
    if (!m_network_plugin_status)
        return;

    const bool plugin = wxGetApp().ensure_bambu_network_plugin_installed(false);
    m_network_plugin_status->SetLabel(plugin
        ? _L("Ready")
        : _L("Not installed — use Preferences → Online"));

    if (DeviceManager* dm = wxGetApp().getDeviceManager()) {
        if (MachineObject* sel = dm->get_selected_machine())
            m_network_printer_status->SetLabel(wxString::Format(_L("Selected: %s"),
                wxString::FromUTF8(sel->get_dev_name())));
        else
            m_network_printer_status->SetLabel(_L("None selected — use Scan Bambu printers"));
    } else {
        m_network_printer_status->SetLabel(_L("Device manager unavailable"));
    }

    m_network_account_status->SetLabel(
        BambuSmartPrintService::instance().is_bambu_account_logged_in()
            ? BambuSmartPrintService::instance().bambu_account_status_text()
            : _L("Not signed in"));
}

void BambuSmartPrintPanel::refresh_all()
{
    if (m_refreshing)
        return;
    m_refreshing = true;

    try {
        refresh_scope_notice();
        refresh_network_status();
        refresh_account_status();
        refresh_stats();
        refresh_printer_learning();
        refresh_model_snapshot();
        refresh_action_state();
        update_text_layout();
        if (m_style == LayoutStyle::Preferences)
            refit_for_preferences();
        else
            Layout();
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrintPanel::refresh_all: " << ex.what();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrintPanel::refresh_all: unknown error";
    }

    m_refreshing = false;
}

}} // namespace

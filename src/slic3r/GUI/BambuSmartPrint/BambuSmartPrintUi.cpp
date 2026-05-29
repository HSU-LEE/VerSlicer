#include "BambuSmartPrintUi.hpp"
#include "BambuSmartPrintService.hpp"

#include "../I18N.hpp"
#include "../Widgets/Button.hpp"
#include "../Widgets/CheckBox.hpp"
#include "../Widgets/ComboBox.hpp"
#include "../Widgets/Label.hpp"
#include "../Widgets/StateColor.hpp"
#include "../Widgets/StaticLine.hpp"
#include "../Widgets/ProgressBar.hpp"

#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/timer.h>

#include <algorithm>
#include <chrono>
#include <vector>

namespace Slic3r { namespace GUI {

namespace SlicePilotUi {

namespace {

// Match Preferences.hpp / PresetBundleDialog.hpp design tokens.
wxColour c_gray900() { return StateColor::darkModeColorFor(wxColour("#363636")); }
wxColour c_gray600() { return StateColor::darkModeColorFor(wxColour("#ACACAC")); }
wxColour c_panel_alt() { return StateColor::darkModeColorFor(wxColour(248, 248, 248)); }
wxColour c_footer() { return StateColor::darkModeColorFor(wxColour(248, 248, 248)); }
wxColour c_line() { return StateColor::darkModeColorFor(wxColour("#EEEEEE")); }
wxColour c_primary() { return StateColor::darkModeColorFor(wxColour("#FF8F4A")); }
wxColour c_primary_hov() { return StateColor::darkModeColorFor(wxColour("#FFB07A")); }
wxColour c_primary_soft() { return StateColor::darkModeColorFor(wxColour(255, 240, 229)); }

StateColor orca_primary_btn_bg()
{
    return StateColor(
        std::make_pair(wxColour(0xF0F0F1), (int) StateColor::Disabled),
        std::make_pair(c_primary_hov(), (int) StateColor::Hovered | StateColor::Checked),
        std::make_pair(c_primary(), (int) StateColor::Checked),
        std::make_pair(wxColour(0xEEEEEE), (int) StateColor::Hovered),
        std::make_pair(c_primary(), (int) StateColor::Normal));
}

StateColor orca_primary_btn_text()
{
    return StateColor(
        std::make_pair(wxColour(0xACACAC), (int) StateColor::Disabled),
        std::make_pair(*wxWHITE, (int) StateColor::Normal));
}

} // namespace

wxColour Theme::background() { return StateColor::darkModeColorFor(*wxWHITE); }
wxColour Theme::surface() { return background(); }
wxColour Theme::surface_alt() { return background(); }
wxColour Theme::footer() { return background(); }
wxColour Theme::border() { return c_line(); }
wxColour Theme::primary() { return c_primary(); }
wxColour Theme::primary_soft() { return c_primary_soft(); }
wxColour Theme::text() { return c_gray900(); }
wxColour Theme::text_muted() { return c_gray600(); }
wxColour Theme::success() { return c_primary(); }
wxColour Theme::warning() { return StateColor::darkModeColorFor(wxColour("#E58600")); }
wxColour Theme::danger() { return StateColor::darkModeColorFor(wxColour("#E14747")); }

void apply_dialog_chrome(wxWindow* dialog, const wxString& title)
{
    if (!dialog)
        return;
    dialog->SetBackgroundColour(Theme::background());
    if (!title.IsEmpty())
        dialog->SetLabel(title);
}

void apply_panel_chrome(wxPanel* panel)
{
    if (!panel)
        return;
    panel->SetBackgroundColour(Theme::background());
}

wxColour prepare_strip_background()
{
    return Theme::surface_alt();
}

int content_side_margin_dip(wxWindow* w, bool preferences_style)
{
    if (!w)
        return 14;
    return w->FromDIP(preferences_style ? 25 : 14);
}

wxPanel* create_modal_footer(wxWindow* parent, wxBoxSizer** actions_out)
{
    auto* footer = new wxPanel(parent, wxID_ANY);
    footer->SetBackgroundColour(Theme::footer());
    auto* col = new wxBoxSizer(wxVERTICAL);
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    const int pad = parent->FromDIP(10);
    col->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, pad);
    footer->SetSizer(col);
    if (actions_out)
        *actions_out = row;
    return footer;
}

wxColour gauge_colour_for_score(int score_percent)
{
    (void) score_percent;
    return Theme::border();
}

void apply_gauge_score(ProgressBar* gauge, int score_percent)
{
    if (!gauge)
        return;
    const int clamped = std::max(0, std::min(100, score_percent));
    gauge->SetProgressBackgroundColour(Theme::border());
    gauge->SetProgressForedColour(Theme::primary());
    gauge->SetValue(clamped);
}

wxPanel* create_header(wxWindow* parent, const wxString& title, const wxString& subtitle, bool compact)
{
    // Preferences titles use DESIGN_LEFT_MARGIN - 10 (15 DIP); dialogs use 20 DIP.
    const int hpad = parent->FromDIP(compact ? 15 : 20);
    const int vtop = parent->FromDIP(compact ? 10 : 14);
    const int vgap = parent->FromDIP(6);

    auto* panel = new wxPanel(parent, wxID_ANY);
    panel->SetBackgroundColour(Theme::background());

    auto* sz = new wxBoxSizer(wxVERTICAL);
    auto* text_col = new wxBoxSizer(wxVERTICAL);

    auto* line = new StaticLine(panel, false, title);
    line->SetForegroundColour(Theme::text());
    line->SetFont(Label::Head_14);
    line->SetLineColour(Theme::border());
    text_col->Add(line, 0, wxEXPAND);

    if (!subtitle.IsEmpty()) {
        auto* sub = new wxStaticText(panel, wxID_ANY, subtitle, wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
        sub->SetFont(Label::Body_13);
        sub->SetForegroundColour(Theme::text_muted());
        wrap_static_text(sub, parent, 0);
        text_col->Add(sub, 0, wxEXPAND | wxTOP, vgap);
    }

    sz->Add(text_col, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, hpad);
    sz->AddSpacer(parent->FromDIP(compact ? 8 : 12));
    panel->SetSizer(sz);
    return panel;
}

wxString format_prepare_bar_status(int score_percent, const std::string& tier_headline, bool has_model)
{
    if (!has_model)
        return _L("Load a model, then press Print");
    if (tier_headline.empty())
        return _L("Press Print when ready");
    return wxString::FromUTF8(tier_headline);
}

static wxString readiness_action_text(const BambuSmartPrint::ReadinessReport& ready)
{
    if (ready.filament_mismatch)
        return _L("Load the suggested filament, then Print");

    switch (ready.tier) {
    case BambuSmartPrint::ReadinessTier::Excellent:
    case BambuSmartPrint::ReadinessTier::Good:
        return _L("Ready to Print");
    case BambuSmartPrint::ReadinessTier::Fair:
        if (!ready.action_items.empty())
            return wxString::Format(_L("Print OK — %s"), wxString::FromUTF8(ready.action_items.front()));
        return _L("Print OK — minor tweaks suggested");
    default:
        if (!ready.action_items.empty())
            return wxString::Format(_L("Review before Print — %s"), wxString::FromUTF8(ready.action_items.front()));
        return _L("Review before Print — higher failure risk");
    }
}

wxString format_prepare_bar_status(const BambuSmartPrint::ReadinessReport& ready, bool has_model)
{
    if (!has_model)
        return _L("Load a model, then press Print");
    return readiness_action_text(ready);
}

wxString format_prepare_bar_status(const BambuSmartPrint::ReadinessReport& ready, bool has_model,
                                   const wxString& slice_estimate)
{
    wxString base = format_prepare_bar_status(ready, has_model);
    if (slice_estimate.empty())
        return base;
    return base + wxString(" · ") + slice_estimate;
}

wxString one_click_phase_status_text(BambuSmartPrintService::OneClickPhase phase)
{
    switch (phase) {
    case BambuSmartPrintService::OneClickPhase::Analyzing:
        return _L("Smart Print: analyzing model…");
    case BambuSmartPrintService::OneClickPhase::Applying:
        return _L("Smart Print: applying suggested settings…");
    case BambuSmartPrintService::OneClickPhase::Slicing:
        return _L("Smart Print: slicing…");
    case BambuSmartPrintService::OneClickPhase::Exporting:
        return _L("Smart Print: sending to printer…");
    default:
        return {};
    }
}

wxString format_slice_estimate_summary(const std::string& print_time, double filament_g)
{
    wxString out;
    if (!print_time.empty())
        out = wxString::Format(_L("~%s"), wxString::FromUTF8(print_time));
    if (filament_g > 0.5) {
        wxString fg = wxString::Format(_L("~%.0fg"), filament_g);
        if (out.empty())
            out = fg;
        else
            out += wxString(" · ") + fg;
    }
    return out;
}

wxPanel* create_flat_section(wxWindow* parent, const wxString& title, wxBoxSizer** inner_out)
{
    const int side = content_side_margin_dip(parent, false);
    auto* panel = new wxPanel(parent, wxID_ANY);
    panel->SetBackgroundColour(Theme::background());
    auto* root = new wxBoxSizer(wxVERTICAL);
    if (!title.IsEmpty()) {
        auto* hdr = new StaticLine(panel, false, title);
        hdr->SetFont(Label::Head_14);
        hdr->SetForegroundColour(Theme::text());
        root->Add(hdr, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, side);
    }
    auto* inner = new wxBoxSizer(wxVERTICAL);
    root->Add(inner, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, side);
    panel->SetSizer(root);
    if (inner_out)
        *inner_out = inner;
    return panel;
}

wxPanel* create_card(wxWindow* parent, wxBoxSizer** inner, wxPanel** body_out, int outer_margin, bool flat)
{
    if (flat) {
        auto* panel = new wxPanel(parent, wxID_ANY);
        panel->SetBackgroundColour(Theme::background());
        auto* inner_sz = new wxBoxSizer(wxVERTICAL);
        panel->SetSizer(inner_sz);
        if (inner)
            *inner = inner_sz;
        if (body_out)
            *body_out = panel;
        return panel;
    }

    auto* frame = new wxPanel(parent, wxID_ANY);
    frame->SetBackgroundColour(Theme::border());

    auto* card = new wxPanel(frame, wxID_ANY);
    card->SetBackgroundColour(Theme::background());
    auto* card_sz = new wxBoxSizer(wxVERTICAL);
    const int m = parent->FromDIP(outer_margin > 0 ? outer_margin : 14);
    card_sz->AddSpacer(m);
    auto* inner_sz = new wxBoxSizer(wxVERTICAL);
    card_sz->Add(inner_sz, 1, wxEXPAND | wxLEFT | wxRIGHT, m);
    card_sz->AddSpacer(m);
    card->SetSizer(card_sz);

    auto* frame_sz = new wxBoxSizer(wxVERTICAL);
    const int border = std::max(1, parent->FromDIP(1));
    frame_sz->Add(card, 1, wxEXPAND | wxALL, border);
    frame->SetSizer(frame_sz);

    if (inner)
        *inner = inner_sz;
    if (body_out)
        *body_out = card;
    return frame;
}

wxPanel* card_body_panel(wxPanel* card_frame)
{
    if (!card_frame)
        return nullptr;
    wxSizer* frame_sz = card_frame->GetSizer();
    if (!frame_sz || frame_sz->GetChildren().GetCount() == 0)
        return card_frame;
    wxSizerItem* item = frame_sz->GetItem(static_cast<size_t>(0));
    if (!item)
        return card_frame;
    return dynamic_cast<wxPanel*>(item->GetWindow());
}

wxPanel* create_empty_state(wxWindow* parent, const wxString& title, const wxString& detail)
{
    auto* box = new wxPanel(parent, wxID_ANY);
    box->SetBackgroundColour(Theme::primary_soft());
    auto* sz = new wxBoxSizer(wxHORIZONTAL);
    const int pad = parent->FromDIP(14);

    auto* text_col = new wxBoxSizer(wxVERTICAL);
    auto* t = new wxStaticText(box, wxID_ANY, title);
    t->SetFont(Label::Body_14);
    t->SetForegroundColour(Theme::primary());
    wxFont tf = t->GetFont();
    tf.SetWeight(wxFONTWEIGHT_BOLD);
    t->SetFont(tf);
    text_col->Add(t, 0, wxEXPAND);

    auto* d = new wxStaticText(box, wxID_ANY, detail, wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
    style_body_text(d, true);
    wrap_static_text(d, parent, 440);
    text_col->Add(d, 0, wxEXPAND | wxTOP, parent->FromDIP(4));
    sz->Add(text_col, 1, wxEXPAND | wxTOP | wxBOTTOM | wxRIGHT, pad);
    sz->AddSpacer(pad);
    box->SetSizer(sz);
    return box;
}

wxPanel* create_empty_state_subtle(wxWindow* parent, const wxString& title, const wxString& detail)
{
    auto* row = new wxPanel(parent, wxID_ANY);
    row->SetBackgroundColour(Theme::background());
    auto* sz = new wxBoxSizer(wxVERTICAL);
    const int pad = parent->FromDIP(10);

    auto* t = new wxStaticText(row, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
    style_body_text(t, true);
    wxFont tf = t->GetFont();
    tf.SetWeight(wxFONTWEIGHT_BOLD);
    t->SetFont(tf);
    sz->Add(t, 0, wxEXPAND | wxALL, pad);

    if (!detail.IsEmpty()) {
        auto* d = new wxStaticText(row, wxID_ANY, detail, wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
        style_body_text(d, true);
        wrap_static_text(d, parent, 0);
        sz->Add(d, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad);
    }

    row->SetSizer(sz);
    return row;
}

wxPanel* create_banner(wxWindow* parent, wxStaticText** text_out, BannerKind kind)
{
    wxColour bg = Theme::primary_soft();
    wxColour fg = Theme::text_muted();
    wxColour accent = Theme::primary();
    if (kind == BannerKind::Warning) {
        bg = StateColor::darkModeColorFor(wxColour(255, 248, 230));
        fg = Theme::warning();
        accent = Theme::warning();
    } else if (kind == BannerKind::Success) {
        bg = Theme::primary_soft();
        fg = Theme::text();
        accent = Theme::success();
    } else if (kind == BannerKind::Info) {
        bg = Theme::background();
        fg = Theme::text_muted();
        accent = Theme::background();
    }

    auto* banner = new wxPanel(parent, wxID_ANY);
    banner->SetBackgroundColour(bg);
    auto* sz = new wxBoxSizer(wxHORIZONTAL);
    const int pad = parent->FromDIP(12);
    const int strip = std::max(3, parent->FromDIP(4));

    auto* marker = new wxPanel(banner, wxID_ANY, wxDefaultPosition, parent->FromDIP(wxSize(strip, -1)));
    marker->SetBackgroundColour(accent);
    marker->SetMinSize(parent->FromDIP(wxSize(strip, 36)));
    sz->Add(marker, 0, wxEXPAND | wxTOP | wxBOTTOM | wxLEFT, pad);

    auto* txt = new wxStaticText(banner, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                 wxST_NO_AUTORESIZE | wxALIGN_LEFT);
    txt->SetFont(Label::Body_13);
    txt->SetForegroundColour(fg);
    sz->Add(txt, 1, wxEXPAND | wxALL, pad);
    banner->SetSizer(sz);
    if (text_out)
        *text_out = txt;
    return banner;
}

wxPanel* create_stat_chip(wxWindow* parent, const wxString& label, wxStaticText** value_out,
                          wxStaticText** label_out)
{
    auto* frame = new wxPanel(parent, wxID_ANY);
    frame->SetBackgroundColour(Theme::border());
    auto* chip = new wxPanel(frame, wxID_ANY);
    chip->SetBackgroundColour(Theme::background());
    auto* sz = new wxBoxSizer(wxVERTICAL);
    const int pad = parent->FromDIP(10);
    auto* val = new wxStaticText(chip, wxID_ANY, "0", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
    wxFont vf = Label::Head_14;
    vf.SetPointSize(vf.GetPointSize() + 1);
    vf.SetWeight(wxFONTWEIGHT_BOLD);
    val->SetFont(vf);
    val->SetForegroundColour(Theme::text_muted());
    auto* lbl = new wxStaticText(chip, wxID_ANY, label, wxDefaultPosition, wxDefaultSize,
                                 wxST_NO_AUTORESIZE | wxALIGN_CENTER_HORIZONTAL);
    lbl->SetFont(Label::Body_12);
    lbl->SetForegroundColour(Theme::text_muted());
    sz->AddStretchSpacer(1);
    sz->Add(val, 0, wxALIGN_CENTER_HORIZONTAL);
    sz->Add(lbl, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, parent->FromDIP(4));
    sz->AddStretchSpacer(1);
    chip->SetSizer(sz);
    chip->SetMinSize(parent->FromDIP(wxSize(88, 72)));
    auto* frame_sz = new wxBoxSizer(wxVERTICAL);
    frame_sz->Add(chip, 1, wxEXPAND | wxALL, std::max(1, parent->FromDIP(1)));
    frame->SetSizer(frame_sz);
    if (value_out)
        *value_out = val;
    if (label_out)
        *label_out = lbl;
    return frame;
}

wxPanel* create_readiness_hero(wxWindow* parent, bool compact,
                               wxStaticText** score_out, ProgressBar** gauge_out,
                               wxStaticText** headline_out,
                               wxStaticText** failures_out, wxStaticText** successes_out,
                               wxStaticText** recent_out,
                               bool show_stat_chips)
{
    wxBoxSizer* inner = nullptr;
    wxPanel* body = nullptr;
    auto* frame = create_card(parent, &inner, &body, compact ? 10 : 12);
    if (!body)
        body = card_body_panel(frame);

    auto* cap = new wxStaticText(body, wxID_ANY, _L("Print readiness"));
    style_section_title(cap);
    inner->Add(cap, 0, wxEXPAND | wxBOTTOM, parent->FromDIP(8));

    auto* score_row = new wxBoxSizer(wxHORIZONTAL);
    auto* score = new wxStaticText(body, wxID_ANY, "—");
    wxFont sf = Label::Head_14;
    sf.SetPointSize(compact ? 26 : 32);
    sf.SetWeight(wxFONTWEIGHT_BOLD);
    score->SetFont(sf);
    score->SetForegroundColour(Theme::text_muted());
    score_row->Add(score, 0, wxALIGN_BOTTOM);
    auto* pct = new wxStaticText(body, wxID_ANY, "%");
    wxFont pf = score->GetFont();
    pf.SetPointSize(std::max(12, pf.GetPointSize() - 10));
    pct->SetFont(pf);
    pct->SetForegroundColour(Theme::text_muted());
    score_row->Add(pct, 0, wxALIGN_BOTTOM | wxLEFT, body->FromDIP(2));

    auto* headline = new wxStaticText(body, wxID_ANY, _L("Load a model on the build plate to analyze"));
    style_body_text(headline, true);
    wrap_static_text(headline, parent, compact ? 280 : 360);

    auto* score_gauge_row = new wxBoxSizer(wxHORIZONTAL);
    score_gauge_row->Add(score_row, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, parent->FromDIP(12));

    const int gauge_h = parent->FromDIP(compact ? 12 : 14);
    const int gauge_w = parent->FromDIP(compact ? 96 : 120);
    wxSize gauge_size(gauge_w, gauge_h);
    auto* gauge = new ProgressBar(body, wxID_ANY, 100, wxDefaultPosition, gauge_size);
    gauge->SetMinSize(gauge_size);
    gauge->SetRadius(gauge_h / 2.4);
    apply_gauge_score(gauge, 0);
    score_gauge_row->Add(gauge, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

    inner->Add(score_gauge_row, 0, wxEXPAND | wxBOTTOM, parent->FromDIP(6));
    inner->Add(headline, 0, wxEXPAND | wxBOTTOM, parent->FromDIP(12));

    if (show_stat_chips) {
        const int stat_gap = parent->FromDIP(8);
        auto* stats = new wxFlexGridSizer(3, stat_gap, 0);
        stats->AddGrowableCol(0);
        stats->AddGrowableCol(1);
        stats->AddGrowableCol(2);
        stats->Add(create_stat_chip(body, _L("Failures"), failures_out), 1, wxEXPAND);
        stats->Add(create_stat_chip(body, _L("Successes"), successes_out), 1, wxEXPAND);
        stats->Add(create_stat_chip(body, _L("Recent (30d)"), recent_out), 1, wxEXPAND);
        inner->Add(stats, 0, wxEXPAND);
    }

    if (score_out)
        *score_out = score;
    if (gauge_out)
        *gauge_out = gauge;
    if (headline_out)
        *headline_out = headline;
    return frame;
}

namespace {

wxPanel* create_step_number_badge(wxWindow* parent, int step_num)
{
    const int size = parent->FromDIP(26);
    auto* badge = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(size, size));
    badge->SetBackgroundColour(Theme::primary());
    auto* sz = new wxBoxSizer(wxVERTICAL);
    auto* num = new wxStaticText(badge, wxID_ANY, wxString::Format("%d", step_num),
                                 wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
    wxFont nf = Label::Body_13;
    nf.SetWeight(wxFONTWEIGHT_BOLD);
    num->SetFont(nf);
    num->SetForegroundColour(StateColor::darkModeColorFor(*wxWHITE));
    sz->AddStretchSpacer(1);
    sz->Add(num, 0, wxALIGN_CENTER);
    sz->AddStretchSpacer(1);
    badge->SetSizer(sz);
    badge->SetMinSize(wxSize(size, size));
    badge->SetMaxSize(wxSize(size, size));
    return badge;
}

wxPanel* create_step_connector(wxWindow* parent)
{
    const int h = parent->FromDIP(12);
    auto* line = new wxPanel(parent, wxID_ANY, wxDefaultPosition, parent->FromDIP(wxSize(2, h)));
    line->SetBackgroundColour(Theme::border());
    line->SetMinSize(parent->FromDIP(wxSize(2, h)));
    line->SetMaxSize(parent->FromDIP(wxSize(2, h)));
    return line;
}

} // namespace

wxPanel* create_step_row_inline(wxWindow* parent, int step_num, const wxString& title,
                                const wxString& description, Button** action_out,
                                const wxString& action_label, bool show_connector)
{
    auto* outer = new wxPanel(parent, wxID_ANY);
    outer->SetBackgroundColour(Theme::background());
    auto* outer_sz = new wxBoxSizer(wxVERTICAL);

    auto* row = new wxPanel(outer, wxID_ANY);
    row->SetBackgroundColour(Theme::background());

    const int col_gap = parent->FromDIP(10);
    auto* row_sz = new wxBoxSizer(wxHORIZONTAL);

    auto* badge_col = new wxBoxSizer(wxVERTICAL);
    badge_col->Add(create_step_number_badge(row, step_num), 0, wxALIGN_CENTER_HORIZONTAL);
    if (show_connector)
        badge_col->Add(create_step_connector(row), 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, parent->FromDIP(3));
    row_sz->Add(badge_col, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, col_gap);

    auto* text_col = new wxBoxSizer(wxVERTICAL);
    auto* t = new wxStaticText(row, wxID_ANY, title);
    style_section_title(t);
    text_col->Add(t, 0, wxEXPAND);
    if (!description.IsEmpty()) {
        auto* d = new wxStaticText(row, wxID_ANY, description, wxDefaultPosition, wxDefaultSize,
                                   wxST_NO_AUTORESIZE);
        style_body_text(d, true);
        wrap_static_text(d, row, 0);
        text_col->Add(d, 0, wxEXPAND | wxTOP, parent->FromDIP(3));
    }
    row_sz->Add(text_col, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL | wxRIGHT, col_gap);

    auto* btn = new Button(row, action_label);
    style_dialog_button(btn, false);
    size_action_button(row, btn, 34);
    const int btn_w = parent->FromDIP(96);
    wxSize btn_min = btn->GetMinSize();
    btn->SetMinSize(wxSize(btn_w, btn_min.y));
    row_sz->Add(btn, 0, wxALIGN_CENTER_VERTICAL);

    row->SetSizer(row_sz);
    const int row_h = parent->FromDIP(description.IsEmpty() ? 54 : 62);
    row->SetMinSize(wxSize(-1, row_h));
    outer_sz->Add(row, 0, wxEXPAND);

    auto* rule = new wxPanel(outer, wxID_ANY, wxDefaultPosition, wxSize(-1, std::max(1, parent->FromDIP(1))));
    rule->SetBackgroundColour(Theme::border());
    outer_sz->Add(rule, 0, wxEXPAND | wxTOP, parent->FromDIP(6));

    outer->SetSizer(outer_sz);
    if (action_out)
        *action_out = btn;
    return outer;
}

wxPanel* create_workflow_stack(wxWindow* parent,
                               const std::vector<std::tuple<int, wxString, wxString, wxString>>& steps,
                               std::vector<Button*>& actions_out)
{
    auto* stack = new wxPanel(parent, wxID_ANY);
    stack->SetBackgroundColour(Theme::background());
    auto* sz = new wxBoxSizer(wxVERTICAL);
    actions_out.clear();
    actions_out.reserve(steps.size());

    for (size_t i = 0; i < steps.size(); ++i) {
        const auto& [num, title, desc, label] = steps[i];
        Button* btn = nullptr;
        const bool connector = i + 1 < steps.size();
        auto* row = create_step_row_inline(stack, num, title, desc, &btn, label, connector);
        if (i + 1 == steps.size()) {
            wxSizer* outer_sz = row->GetSizer();
            if (outer_sz && outer_sz->GetChildren().GetCount() > 1) {
                wxSizerItem* rule_item = outer_sz->GetItem(outer_sz->GetChildren().GetCount() - 1);
                if (rule_item && rule_item->GetWindow())
                    rule_item->GetWindow()->Hide();
            }
        }
        const int row_pad = parent->FromDIP(i + 1 < steps.size() ? 2 : 0);
        sz->Add(row, 0, wxEXPAND | wxTOP | wxBOTTOM, row_pad);
        if (btn)
            actions_out.push_back(btn);
    }
    stack->SetSizer(sz);
    return stack;
}

void add_tool_button_rows(wxWindow* parent, wxBoxSizer* target,
                          const std::vector<Button*>& buttons, int columns,
                          bool expand_buttons)
{
    if (!parent || !target || buttons.empty())
        return;
    const int cols = std::max(1, columns);
    const int gap = parent->FromDIP(6);
    for (size_t i = 0; i < buttons.size(); i += size_t(cols)) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        for (int c = 0; c < cols && i + size_t(c) < buttons.size(); ++c) {
            Button* btn = buttons[i + size_t(c)];
            if (!btn)
                continue;
            if (expand_buttons)
                row->Add(btn, 1, wxEXPAND | (c > 0 ? wxLEFT : 0), c > 0 ? gap : 0);
            else
                row->Add(btn, 0, wxALIGN_CENTER_VERTICAL | (c > 0 ? wxLEFT : 0), c > 0 ? gap : 0);
        }
        target->Add(row, 0, wxEXPAND | wxTOP, gap);
    }
}

void size_compact_toolbar_button(wxWindow* parent, Button* btn)
{
    if (!parent || !btn)
        return;
    wxClientDC dc(btn);
    dc.SetFont(btn->GetFont());
    wxSize te;
    dc.GetTextExtent(btn->GetLabel(), &te.x, &te.y);
    const int h = te.y + parent->FromDIP(14);
    const int w = te.x + parent->FromDIP(24);
    btn->SetMinSize(wxSize(w, h));
    btn->SetMaxSize(wxSize(w, h));
}

void style_section_title(wxStaticText* label)
{
    if (!label)
        return;
    label->SetFont(Label::Head_14);
    label->SetForegroundColour(Theme::text());
}

void style_body_text(wxStaticText* label, bool muted)
{
    if (!label)
        return;
    label->SetFont(Label::Body_13);
    label->SetForegroundColour(muted ? Theme::text_muted() : Theme::text());
}

void style_primary_button(Button* btn)
{
    if (!btn)
        return;
    btn->SetBackgroundColor(orca_primary_btn_bg());
    btn->SetTextColor(orca_primary_btn_text());
    btn->SetFont(Label::Body_14);
}

void style_secondary_button(Button* btn)
{
    style_dialog_button(btn, false);
}

void style_dialog_button(Button* btn, bool primary)
{
    if (!btn)
        return;
    if (primary) {
        style_primary_button(btn);
        btn->SetStyle(ButtonStyle::Confirm, ButtonType::Choice);
    } else {
        btn->SetStyle(ButtonStyle::Regular, ButtonType::Choice);
        btn->SetFont(Label::Body_14);
    }
}

void style_orca_checkbox(CheckBox* cb)
{
    if (!cb)
        return;
}

void style_orca_combobox(ComboBox* combo)
{
    if (!combo)
        return;
    combo->SetFont(Label::Body_14);
    combo->GetDropDown().SetFont(Label::Body_14);
    combo->GetDropDown().SetUseContentWidth(true);
}

void style_orca_list(wxListCtrl* list)
{
    if (!list)
        return;
    list->SetBackgroundColour(Theme::background());
    list->SetFont(Label::Body_13);
}

wxPanel* create_orca_list_column_header(wxWindow* parent,
                                      const std::vector<std::pair<wxString, int>>& columns_dip)
{
    auto* wrap = new wxPanel(parent, wxID_ANY);
    wrap->SetBackgroundColour(Theme::background());
    auto* col = new wxBoxSizer(wxVERTICAL);

    auto* row_panel = new wxPanel(wrap, wxID_ANY);
    row_panel->SetBackgroundColour(Theme::background());
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    const int pad_x = parent->FromDIP(8);
    const int pad_y = parent->FromDIP(6);

    for (const auto& col_def : columns_dip) {
        auto* cell = new wxPanel(row_panel, wxID_ANY);
        cell->SetBackgroundColour(Theme::background());
        cell->SetMinSize(parent->FromDIP(wxSize(col_def.second, -1)));
        auto* txt = new wxStaticText(cell, wxID_ANY, col_def.first);
        txt->SetFont(Label::Body_13);
        txt->SetForegroundColour(Theme::text_muted());
        auto* cell_sz = new wxBoxSizer(wxHORIZONTAL);
        cell_sz->Add(txt, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, pad_x);
        cell->SetSizer(cell_sz);
        row->Add(cell, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, pad_y);
    }

    row_panel->SetSizer(row);
    col->Add(row_panel, 0, wxEXPAND);
    auto* line = new wxStaticLine(wrap);
    line->SetForegroundColour(Theme::border());
    col->Add(line, 0, wxEXPAND);
    wrap->SetSizer(col);
    return wrap;
}

void style_orca_notebook(wxNotebook* notebook)
{
    if (!notebook)
        return;
    notebook->SetBackgroundColour(Theme::background());
    notebook->SetFont(Label::Body_13);
}

wxSizer* add_settings_checkbox_row(wxWindow* parent, wxSizer* target, const wxString& title,
                                   CheckBox** out, const wxString& tooltip)
{
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    const wxString tip = tooltip.IsEmpty() ? title : tooltip;
    const wxSize title_sz = parent->FromDIP(wxSize(280, -1));

    auto* lbl = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, title_sz, wxST_NO_AUTORESIZE);
    lbl->SetForegroundColour(Theme::text());
    lbl->SetFont(Label::Body_14);
    lbl->SetToolTip(tip);
    lbl->Wrap(title_sz.x);

    auto* cb = new CheckBox(parent);
    cb->SetToolTip(tip);
    style_orca_checkbox(cb);

    row->Add(lbl, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, parent->FromDIP(3));
    row->Add(cb, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, parent->FromDIP(5));
    target->Add(row, 0, wxEXPAND | wxBOTTOM, parent->FromDIP(4));
    if (out)
        *out = cb;
    return row;
}

wxSizer* add_settings_combobox_row(wxWindow* parent, wxSizer* target, const wxString& title,
                                   ComboBox** out, const std::vector<wxString>& choices,
                                   const wxString& tooltip)
{
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    const wxString tip = tooltip.IsEmpty() ? title : tooltip;

    auto* lbl = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition,
                                 parent->FromDIP(wxSize(280, -1)), wxST_NO_AUTORESIZE);
    lbl->SetForegroundColour(Theme::text());
    lbl->SetFont(Label::Body_14);
    lbl->SetToolTip(tip);
    lbl->Wrap(parent->FromDIP(280));

    auto* combo = new ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                               parent->FromDIP(wxSize(200, -1)), 0, nullptr, wxCB_READONLY);
    for (const wxString& item : choices)
        combo->Append(item);
    style_orca_combobox(combo);
    combo->SetToolTip(tip);

    row->Add(lbl, 0, wxALIGN_CENTER_VERTICAL);
    row->Add(combo, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, parent->FromDIP(8));
    target->Add(row, 0, wxEXPAND | wxBOTTOM, parent->FromDIP(8));
    if (out)
        *out = combo;
    return row;
}

void style_prepare_toolbar_button(Button* btn, bool confirm)
{
    if (!btn)
        return;
    btn->SetStyle(confirm ? ButtonStyle::Confirm : ButtonStyle::Regular, ButtonType::Compact);
    btn->SetPaddingSize(btn->FromDIP(wxSize(6, 2)));
    btn->SetCornerRadius(btn->FromDIP(6));
    btn->SetFont(Label::Body_10);
}

void size_prepare_strip_button(wxWindow* parent, Button* btn)
{
    if (!parent || !btn)
        return;
    wxClientDC dc(btn);
    dc.SetFont(btn->GetFont());
    wxSize te;
    dc.GetTextExtent(btn->GetLabel(), &te.x, &te.y);
    const int h = te.y + parent->FromDIP(8);
    const int w = te.x + parent->FromDIP(16);
    btn->SetMinSize(wxSize(w, h));
    btn->SetMaxSize(wxSize(w, h));
}

void style_accent_button(Button* btn, const wxColour& bg)
{
    if (!btn)
        return;
    btn->SetBackgroundColor(bg);
    btn->SetTextColor(StateColor(std::make_pair(*wxWHITE, (int) StateColor::Normal)));
    btn->SetFont(Label::Body_14);
}

wxPanel* create_step_row(wxWindow* parent, int step_num, const wxString& title,
                         const wxString& description, Button** action_out,
                         const wxString& action_label)
{
    auto* row = new wxPanel(parent, wxID_ANY);
    row->SetBackgroundColour(Theme::background());
    auto* row_sz = new wxBoxSizer(wxVERTICAL);

    auto* top = new wxBoxSizer(wxHORIZONTAL);
    auto* badge = new wxStaticText(row, wxID_ANY, wxString::Format("%d", step_num));
    wxFont bf = Label::Head_14;
    bf.SetWeight(wxFONTWEIGHT_BOLD);
    badge->SetFont(bf);
    badge->SetForegroundColour(Theme::primary());

    auto* text_col = new wxBoxSizer(wxVERTICAL);
    auto* t = new wxStaticText(row, wxID_ANY, title);
    style_section_title(t);
    text_col->Add(t, 0, wxEXPAND);
    if (!description.IsEmpty()) {
        auto* d = new wxStaticText(row, wxID_ANY, description);
        style_body_text(d, true);
        wrap_static_text(d, parent, 440);
        text_col->Add(d, 0, wxEXPAND | wxTOP, parent->FromDIP(2));
    }

    top->Add(badge, 0, wxALIGN_TOP | wxRIGHT, parent->FromDIP(10));
    top->Add(text_col, 1, wxEXPAND);
    row_sz->Add(top, 0, wxEXPAND);

    auto* btn = new Button(row, action_label);
    style_secondary_button(btn);
    size_action_button(row, btn);
    auto* btn_row = new wxBoxSizer(wxHORIZONTAL);
    btn_row->AddStretchSpacer();
    btn_row->Add(btn, 0);
    row_sz->Add(btn_row, 0, wxEXPAND | wxTOP, parent->FromDIP(8));

    row->SetSizer(row_sz);
    if (action_out)
        *action_out = btn;
    return row;
}

wxScrolledWindow* create_scroll_body(wxWindow* parent, wxBoxSizer** body_out)
{
    auto* scroll = new wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    scroll->SetScrollRate(0, parent->FromDIP(10));
    scroll->SetBackgroundColour(Theme::background());
    scroll->SetMinSize(parent->FromDIP(wxSize(360, 240)));
    auto* body = new wxBoxSizer(wxVERTICAL);
    scroll->SetSizer(body);
    if (body_out)
        *body_out = body;
    return scroll;
}

int content_wrap_width_px(wxWindow* ref, int fallback_dip)
{
    if (!ref)
        return 560;
    wxWindow* w = ref;
    int best = 0;
    while (w) {
        const int cw = w->GetClientSize().GetWidth();
        if (cw > best)
            best = cw;
        w = w->GetParent();
    }
    if (best > 80)
        return std::max(ref->FromDIP(280), best - ref->FromDIP(28));
    return ref->FromDIP(fallback_dip);
}

int scroll_content_wrap_width_px(wxScrolledWindow* scroll, int margin_dip)
{
    if (!scroll)
        return 560;
    int w = scroll->GetClientSize().GetWidth();
    if (w < 120) {
        wxWindow* p = scroll->GetParent();
        while (p && w < 120) {
            w = p->GetClientSize().GetWidth();
            p = p->GetParent();
        }
    }
    return std::max(scroll->FromDIP(260), w - scroll->FromDIP(margin_dip));
}

void wrap_static_text(wxStaticText* label, wxWindow* ref, int width_dip)
{
    if (!label || !ref)
        return;
    const int px = width_dip > 0 ? ref->FromDIP(width_dip) : content_wrap_width_px(ref, 560);
    label->Wrap(std::max(ref->FromDIP(200), px));
}

void wrap_static_text_in_scroll(wxStaticText* label, wxScrolledWindow* scroll, int margin_dip)
{
    if (!label || !scroll)
        return;
    label->Wrap(scroll_content_wrap_width_px(scroll, margin_dip));
}

void relayout_scroll_wrapped_texts(wxScrolledWindow* scroll,
                                   const std::vector<wxStaticText*>& labels,
                                   int margin_dip)
{
    if (!scroll)
        return;
    const int wrap_px = scroll_content_wrap_width_px(scroll, margin_dip);
    const int chip_px = std::max(scroll->FromDIP(72),
                                 (wrap_px - scroll->FromDIP(24)) / 3);
    for (wxStaticText* label : labels) {
        if (!label)
            continue;
        if (label->GetWindowStyleFlag() & wxALIGN_CENTER_HORIZONTAL)
            label->Wrap(chip_px);
        else
            label->Wrap(wrap_px);
    }
}

void size_checkbox_to_label(wxWindow* parent, wxCheckBox* cb)
{
    if (!parent || !cb)
        return;
    wxClientDC dc(cb);
    dc.SetFont(cb->GetFont());
    wxSize te;
    dc.GetTextExtent(cb->GetLabel(), &te.x, &te.y);
    cb->SetMinSize(wxSize(te.x + parent->FromDIP(32), te.y + parent->FromDIP(4)));
}

void size_action_button(wxWindow* parent, Button* btn, int min_height_dip)
{
    if (!parent || !btn)
        return;
    wxClientDC dc(btn);
    dc.SetFont(btn->GetFont());
    wxSize te;
    dc.GetTextExtent(btn->GetLabel(), &te.x, &te.y);
    const int h = std::max(parent->FromDIP(min_height_dip), te.y + parent->FromDIP(18));
    const int w = te.x + parent->FromDIP(52);
    btn->SetMinSize(wxSize(std::max(w, parent->FromDIP(88)), h));
    btn->SetMaxSize(wxSize(0, 0));
}

void finalize_modal_dialog(wxWindow* dialog, const wxSize& min_dip, const wxSize& default_dip,
                           wxScrolledWindow* scroll, int scroll_min_height_dip)
{
    if (!dialog)
        return;
    const wxSize min_px = dialog->FromDIP(min_dip);
    dialog->SetMinSize(min_px);
    if (scroll && scroll_min_height_dip > 0)
        scroll->SetMinSize(dialog->FromDIP(wxSize(-1, scroll_min_height_dip)));
    dialog->Layout();
    wxSize want_px = dialog->FromDIP(default_dip);
    want_px.x = std::max(want_px.x, min_px.x);
    want_px.y = std::max(want_px.y, min_px.y);
    dialog->SetClientSize(want_px);
    dialog->Layout();
    if (scroll) {
        scroll->FitInside();
        scroll->Layout();
    }
}

wxPanel* create_readiness_meter(wxWindow* parent, int score_percent, const wxString& headline,
                                ProgressBar** gauge_out)
{
    auto* card = new wxPanel(parent, wxID_ANY);
    card->SetBackgroundColour(Theme::background());
    auto* sz = new wxBoxSizer(wxVERTICAL);
    const int m = parent->FromDIP(12);
    sz->AddSpacer(m);

    auto* title = new wxStaticText(card, wxID_ANY, _L("Print readiness"));
    style_section_title(title);
    sz->Add(title, 0, wxLEFT | wxRIGHT, m);

    const int gauge_h = parent->FromDIP(20);
    wxSize gauge_size(-1, gauge_h);
    auto* gauge = new ProgressBar(card, wxID_ANY, 100, wxDefaultPosition, gauge_size);
    gauge->SetMinSize(gauge_size);
    gauge->SetRadius(gauge_h / 2.4);
    const int clamped = std::max(0, std::min(100, score_percent));
    apply_gauge_score(gauge, clamped);
    sz->Add(gauge, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, parent->FromDIP(8));

    auto* val = new wxStaticText(card, wxID_ANY, wxString::Format(_L("%d%% — %s"), clamped, headline));
    val->SetForegroundColour(Theme::text_muted());
    wxFont rf = parent->GetFont();
    rf.SetWeight(wxFONTWEIGHT_BOLD);
    val->SetFont(rf);
    wrap_static_text(val, parent, 480);
    sz->Add(val, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, parent->FromDIP(4));

    sz->AddSpacer(m);
    card->SetSizer(sz);
    if (gauge_out)
        *gauge_out = gauge;
    return card;
}

wxPanel* add_insight_list(wxPanel* card, wxBoxSizer* card_sz,
                          const std::vector<BambuSmartPrint::PrintInsight>& insights,
                          size_t max_items)
{
    if (insights.empty())
        return nullptr;

    auto* list = new wxBoxSizer(wxVERTICAL);
    size_t n = 0;
    for (const auto& ins : insights) {
        if (n >= max_items)
            break;
        wxColour dot = Theme::text_muted();
        switch (ins.severity) {
        case BambuSmartPrint::RiskSeverity::High:   dot = Theme::danger(); break;
        case BambuSmartPrint::RiskSeverity::Medium: dot = Theme::warning(); break;
        case BambuSmartPrint::RiskSeverity::Low:    dot = Theme::primary(); break;
        default: break;
        }
        wxString sev_label;
        switch (ins.severity) {
        case BambuSmartPrint::RiskSeverity::High:   sev_label = _L("High"); break;
        case BambuSmartPrint::RiskSeverity::Medium: sev_label = _L("Med"); break;
        case BambuSmartPrint::RiskSeverity::Low:    sev_label = _L("Low"); break;
        default: break;
        }

        auto* row = new wxBoxSizer(wxHORIZONTAL);
        if (!sev_label.IsEmpty()) {
            auto* pill = new wxStaticText(card, wxID_ANY, sev_label);
            pill->SetFont(Label::Body_12);
            pill->SetForegroundColour(dot);
            wxFont pf = pill->GetFont();
            pf.SetWeight(wxFONTWEIGHT_BOLD);
            pill->SetFont(pf);
            pill->SetMinSize(card->FromDIP(wxSize(36, -1)));
            row->Add(pill, 0, wxALIGN_TOP | wxRIGHT, card->FromDIP(8));
        } else {
            auto* bullet = new wxStaticText(card, wxID_ANY, wxString::FromUTF8("•"));
            bullet->SetForegroundColour(dot);
            row->Add(bullet, 0, wxTOP, card->FromDIP(2));
        }
        auto* text_col = new wxBoxSizer(wxVERTICAL);
        auto* lbl = new wxStaticText(card, wxID_ANY, wxString::FromUTF8(ins.label));
        style_section_title(lbl);
        wxFont lf = lbl->GetFont();
        lf.SetPointSize(std::max(9, lf.GetPointSize() - 1));
        lbl->SetFont(lf);
        text_col->Add(lbl, 0, wxEXPAND);
        auto* txt = new wxStaticText(card, wxID_ANY, wxString::FromUTF8(ins.detail),
                                     wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
        style_body_text(txt, true);
        wrap_static_text(txt, card, 460);
        text_col->Add(txt, 0, wxEXPAND | wxTOP, card->FromDIP(2));
        row->Add(text_col, 1, wxEXPAND);
        list->Add(row, 0, wxEXPAND | wxBOTTOM, card->FromDIP(8));
        ++n;
    }
    card_sz->Add(list, 0, wxEXPAND);
    return card;
}

void add_card_section_title(wxWindow* card, wxBoxSizer* card_sz, const wxString& title,
                            const wxString& hint)
{
    auto* title_col = new wxBoxSizer(wxVERTICAL);
    auto* t = new wxStaticText(card, wxID_ANY, title);
    style_section_title(t);
    title_col->Add(t, 0, wxEXPAND);
    if (!hint.IsEmpty()) {
        auto* h = new wxStaticText(card, wxID_ANY, hint, wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
        h->SetFont(Label::Body_12);
        h->SetForegroundColour(Theme::text_muted());
        wrap_static_text(h, card, 0);
        title_col->Add(h, 0, wxEXPAND | wxTOP, card->FromDIP(2));
    }
    card_sz->Add(title_col, 0, wxEXPAND | wxBOTTOM, card->FromDIP(10));
}

namespace {

class ModalAutoDefaultHandler : public wxEvtHandler
{
public:
    ModalAutoDefaultHandler(wxDialog* dlg, int default_rc, int timeout_ms, Button* countdown_btn,
                            wxString countdown_base)
        : m_dlg(dlg)
        , m_default(default_rc)
        , m_countdown_btn(countdown_btn)
        , m_countdown_base(std::move(countdown_base))
        , m_deadline(std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms))
    {
        m_timer.SetOwner(this);
        Bind(wxEVT_TIMER, &ModalAutoDefaultHandler::on_timer, this);
        m_timer.Start(250);

        dlg->Bind(wxEVT_BUTTON, &ModalAutoDefaultHandler::on_user_action, this);
        dlg->Bind(wxEVT_CLOSE_WINDOW, &ModalAutoDefaultHandler::on_close, this);
        update_countdown_label();
    }

    void stop() { m_timer.Stop(); Unbind(wxEVT_TIMER, &ModalAutoDefaultHandler::on_timer, this); }

private:
    void on_timer(wxTimerEvent&)
    {
        if (m_user_acted || !m_dlg)
            return;
        const auto now = std::chrono::steady_clock::now();
        if (now >= m_deadline) {
            m_timer.Stop();
            m_dlg->EndModal(m_default);
            return;
        }
        update_countdown_label();
    }

    void on_user_action(wxCommandEvent&) { m_user_acted = true; stop(); }
    void on_close(wxCloseEvent& e)
    {
        m_user_acted = true;
        stop();
        e.Skip();
    }

    void update_countdown_label()
    {
        if (!m_countdown_btn || m_countdown_base.empty())
            return;
        const auto now = std::chrono::steady_clock::now();
        const int secs = std::max(1, int(std::chrono::duration_cast<std::chrono::seconds>(m_deadline - now).count()) + 1);
        if (secs != m_last_secs) {
            m_last_secs = secs;
            m_countdown_btn->SetLabel(wxString::Format(_L("%s (%ds)"), m_countdown_base, secs));
            m_countdown_btn->Refresh();
        }
    }

    wxDialog*           m_dlg{ nullptr };
    int                 m_default{ wxID_CANCEL };
    Button*             m_countdown_btn{ nullptr };
    wxString            m_countdown_base;
    wxTimer             m_timer;
    std::chrono::steady_clock::time_point m_deadline;
    bool                m_user_acted{ false };
    int                 m_last_secs{ -1 };
};

Button* find_primary_footer_button(wxWindow* root)
{
    if (!root)
        return nullptr;
    std::vector<wxWindow*> pending{ root };
    Button* fallback = nullptr;
    while (!pending.empty()) {
        wxWindow* w = pending.back();
        pending.pop_back();
        if (auto* btn = dynamic_cast<Button*>(w)) {
            if (!fallback)
                fallback = btn;
            if (btn->GetLabel().Contains(_L("Apply")) || btn->GetLabel().Contains(_L("Approve"))
                || btn->GetLabel().Contains(_L("Got it")) || btn->GetLabel().Contains(_L("OK")))
                return btn;
        }
        const wxWindowList& kids = w->GetChildren();
        for (wxWindowList::const_iterator it = kids.begin(); it != kids.end(); ++it)
            pending.push_back(*it);
    }
    return fallback;
}

} // namespace

int show_modal_with_auto_default(wxDialog* dlg, int default_modal_result, int timeout_ms)
{
    if (!dlg)
        return wxID_CANCEL;
    Button* primary = find_primary_footer_button(dlg);
    wxString base_label;
    if (primary)
        base_label = primary->GetLabel();
    ModalAutoDefaultHandler handler(dlg, default_modal_result, timeout_ms, primary, base_label);
    return dlg->ShowModal();
}

int show_timed_message_box(wxWindow* parent, const wxString& message, const wxString& caption,
                           long style, int default_button, int timeout_ms)
{
    wxMessageDialog dlg(parent, message, caption, style);
    const bool default_yes = (default_button == wxYES);
    if ((style & wxYES_NO) == wxYES_NO) {
        const wxString suffix = wxString::Format(_L(" (%ds)"), timeout_ms / 1000);
        if (default_yes)
            dlg.SetYesNoLabels(_L("Yes") + suffix, _L("No"));
        else
            dlg.SetYesNoLabels(_L("Yes"), _L("No") + suffix);
    }

    struct TimedMessageHandler : public wxEvtHandler {
        wxMessageDialog* dialog;
        int default_btn;
        std::chrono::steady_clock::time_point deadline;
        wxTimer timer;
        bool user_acted{ false };

        TimedMessageHandler(wxMessageDialog* d, int def, int ms)
            : dialog(d), default_btn(def), deadline(std::chrono::steady_clock::now() + std::chrono::milliseconds(ms))
        {
            timer.SetOwner(this);
            Bind(wxEVT_TIMER, &TimedMessageHandler::on_timer, this);
            timer.Start(250);
            d->Bind(wxEVT_BUTTON, &TimedMessageHandler::on_user, this);
            d->Bind(wxEVT_CLOSE_WINDOW, &TimedMessageHandler::on_close, this);
        }
        void on_timer(wxTimerEvent&)
        {
            if (user_acted || !dialog)
                return;
            if (std::chrono::steady_clock::now() >= deadline) {
                timer.Stop();
                dialog->EndModal(default_btn);
            }
        }
        void on_user(wxCommandEvent&) { user_acted = true; timer.Stop(); }
        void on_close(wxCloseEvent& e) { user_acted = true; timer.Stop(); e.Skip(); }
    } handler(&dlg, default_button, timeout_ms);

    return dlg.ShowModal();
}

} // namespace SlicePilotUi

}} // namespace

#ifndef slic3r_BambuSmartPrintUi_hpp_
#define slic3r_BambuSmartPrintUi_hpp_

#include "../GUI_Utils.hpp"
#include "../wxExtensions.hpp"
#include "libslic3r/BambuSmartPrint/BambuSmartPrintTypes.hpp"
#include <wx/bitmap.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/scrolwin.h>
#include <tuple>
#include <vector>

#include "BambuSmartPrintService.hpp"

class Button;
class CheckBox;
class ComboBox;
class ProgressBar;

namespace Slic3r { namespace GUI {

class BambuSmartPrintPanel;

namespace SlicePilotUi {

/** Auto-confirm Smart Print choice dialogs after this delay (export/save dialogs excluded). */
constexpr int SMART_PRINT_DIALOG_AUTO_MS = 10000;

enum class BannerKind { Info, Warning, Success };

/** Orca-aligned palette (matches Preferences / sidebar). */
struct Theme
{
    static wxColour background();
    static wxColour surface();
    static wxColour surface_alt();
    static wxColour footer();
    static wxColour border();
    static wxColour primary();
    static wxColour primary_soft();
    static wxColour text();
    static wxColour text_muted();
    static wxColour success();
    static wxColour warning();
    static wxColour danger();
};

void apply_dialog_chrome(wxWindow* dialog, const wxString& title = {});

/** Panel / tab page background (matches Monitor / Project tabs). */
void apply_panel_chrome(wxPanel* panel);

/** Prepare-view strip background (sidebar-adjacent toolbar tone). */
wxColour prepare_strip_background();

/** Horizontal inset: Preferences tab vs Smart Print page / dialog body. */
int content_side_margin_dip(wxWindow* w, bool preferences_style);

/** Apply muted grey fill to a horizontal progress bar (macOS-safe; not wxGauge). */
void apply_gauge_score(ProgressBar* gauge, int score_percent);

/** Standard modal footer; *actions receives the horizontal button row. */
wxPanel* create_modal_footer(wxWindow* parent, wxBoxSizer** actions_out = nullptr);

/** Section title row (StaticLine) — no logo, no colored banner. */
wxPanel* create_header(wxWindow* parent, const wxString& title, const wxString& subtitle,
                       bool compact = false);

/** Returns bordered frame; *inner is the content sizer; *body is the inner panel for child windows. */
wxPanel* create_card(wxWindow* parent, wxBoxSizer** inner, wxPanel** body = nullptr, int outer_margin = 0,
                     bool flat = false);

/** One-line status for the Prepare view strip (no duplicate score / no special dashes). */
wxString format_prepare_bar_status(int score_percent, const std::string& tier_headline, bool has_model);
wxString format_prepare_bar_status(const BambuSmartPrint::ReadinessReport& ready, bool has_model);
wxString format_prepare_bar_status(const BambuSmartPrint::ReadinessReport& ready, bool has_model,
                                   const wxString& slice_estimate);
wxString format_slice_estimate_summary(const std::string& print_time, double filament_g);
wxString one_click_phase_status_text(BambuSmartPrintService::OneClickPhase phase);

wxPanel* create_flat_section(wxWindow* parent, const wxString& title, wxBoxSizer** inner_out);

void size_compact_toolbar_button(wxWindow* parent, Button* btn);

/** Extra-thin buttons for the Prepare view Smart Print strip. */
void size_prepare_strip_button(wxWindow* parent, Button* btn);

wxPanel* card_body_panel(wxPanel* card_frame);

wxPanel* create_empty_state(wxWindow* parent, const wxString& title, const wxString& detail);

/** Muted in-card placeholder (no teal banner). */
wxPanel* create_empty_state_subtle(wxWindow* parent, const wxString& title, const wxString& detail);

wxPanel* create_banner(wxWindow* parent, wxStaticText** text_out, BannerKind kind);

wxPanel* create_stat_chip(wxWindow* parent, const wxString& label, wxStaticText** value_out,
                          wxStaticText** label_out = nullptr);

/** Hero block: large readiness score, gauge, headline, and three stat chips. */
wxPanel* create_readiness_hero(wxWindow* parent, bool compact,
                               wxStaticText** score_out, ProgressBar** gauge_out,
                               wxStaticText** headline_out,
                               wxStaticText** failures_out, wxStaticText** successes_out,
                               wxStaticText** recent_out,
                               bool show_stat_chips = true);

wxPanel* create_step_row_inline(wxWindow* parent, int step_num, const wxString& title,
                                const wxString& description, Button** action_out,
                                const wxString& action_label, bool show_connector = false);

/** Vertical stack of workflow steps with optional connectors between rows. */
wxPanel* create_workflow_stack(wxWindow* parent,
                               const std::vector<std::tuple<int, wxString, wxString, wxString>>& steps,
                               std::vector<Button*>& actions_out);

void add_tool_button_rows(wxWindow* parent, wxBoxSizer* target,
                          const std::vector<Button*>& buttons, int columns = 2,
                          bool expand_buttons = true);

void style_section_title(wxStaticText* label);

void style_body_text(wxStaticText* label, bool muted = false);

void style_primary_button(Button* btn);

void style_secondary_button(Button* btn);

/** Modal / panel actions — Choice buttons match Preferences and device dialogs. */
void style_dialog_button(Button* btn, bool primary = false);

void style_orca_checkbox(CheckBox* cb);

void style_orca_combobox(ComboBox* combo);

void style_orca_list(wxListCtrl* list);

/** Column header row matching dialog background (avoids black native wxListCtrl header on macOS). */
wxPanel* create_orca_list_column_header(wxWindow* parent,
                                        const std::vector<std::pair<wxString, int>>& columns_dip);

void style_orca_notebook(wxNotebook* notebook);

/** Orca Preferences row: title then checkbox (adjacent, not right-aligned). */
wxSizer* add_settings_checkbox_row(wxWindow* parent, wxSizer* target, const wxString& title,
                                   CheckBox** out, const wxString& tooltip = {});

/** Orca Preferences row: title and readonly combo on one line. */
wxSizer* add_settings_combobox_row(wxWindow* parent, wxSizer* target, const wxString& title,
                                   ComboBox** out, const std::vector<wxString>& choices,
                                   const wxString& tooltip = {});

/** Compact toolbar buttons on the Prepare 3D view (matches Orca parameter/window buttons). */
void style_prepare_toolbar_button(Button* btn, bool confirm = false);

void style_accent_button(Button* btn, const wxColour& bg);

wxPanel* create_step_row(wxWindow* parent, int step_num, const wxString& title,
                         const wxString& description, Button** action_out,
                         const wxString& action_label);

wxScrolledWindow* create_scroll_body(wxWindow* parent, wxBoxSizer** body_out);

void add_card_section_title(wxWindow* card, wxBoxSizer* card_sz, const wxString& title,
                            const wxString& hint = {});

void size_action_button(wxWindow* parent, Button* btn, int min_height_dip = 36);

/** Usable width in pixels for wxStaticText::Wrap(). */
int content_wrap_width_px(wxWindow* ref, int fallback_dip = 560);

/** Wrap width inside a scrolled modal body (viewport minus card padding). */
int scroll_content_wrap_width_px(wxScrolledWindow* scroll, int margin_dip = 48);

void wrap_static_text(wxStaticText* label, wxWindow* ref, int width_dip = 0);

void wrap_static_text_in_scroll(wxStaticText* label, wxScrolledWindow* scroll, int margin_dip = 48);

void relayout_scroll_wrapped_texts(wxScrolledWindow* scroll,
                                   const std::vector<wxStaticText*>& labels,
                                   int margin_dip = 48);

void size_checkbox_to_label(wxWindow* parent, wxCheckBox* cb);

void finalize_modal_dialog(wxWindow* dialog, const wxSize& min_dip, const wxSize& default_dip,
                           wxScrolledWindow* scroll = nullptr, int scroll_min_height_dip = 240);

wxColour gauge_colour_for_score(int score_percent);

wxPanel* create_readiness_meter(wxWindow* parent, int score_percent, const wxString& headline,
                                ProgressBar** gauge_out = nullptr);

wxPanel* add_insight_list(wxPanel* card, wxBoxSizer* card_sz,
                          const std::vector<BambuSmartPrint::PrintInsight>& insights,
                          size_t max_items = 5);

/**
 * Show a modal dialog; if the user does not click within @p timeout_ms, end with @p default_modal_result.
 * Closing the window (X) is not treated as a timeout — it uses the dialog's cancel path.
 */
int show_modal_with_auto_default(wxDialog* dlg, int default_modal_result,
                                 int timeout_ms = SMART_PRINT_DIALOG_AUTO_MS);

/** Yes/No message with the same auto-default behavior (default is wxYES or wxNO). */
int show_timed_message_box(wxWindow* parent, const wxString& message, const wxString& caption,
                           long style, int default_button, int timeout_ms = SMART_PRINT_DIALOG_AUTO_MS);

} // namespace SlicePilotUi

}} // namespace

#endif

#ifndef slic3r_BambuSmartPrintPanel_hpp_
#define slic3r_BambuSmartPrintPanel_hpp_

#include <wx/panel.h>

class wxStaticText;
class ProgressBar;
class Button;
class CheckBox;
class ComboBox;

namespace Slic3r { namespace GUI {

class BambuSmartPrintPanel : public wxPanel
{
public:
    enum class LayoutStyle { Full, Preferences };

    explicit BambuSmartPrintPanel(wxWindow* parent, LayoutStyle style = LayoutStyle::Full);
    ~BambuSmartPrintPanel();

    void refresh_stats();
    void refresh_account_status();
    void refresh_model_snapshot();
    void refresh_printer_learning();
    void refresh_all();

private:
    void refresh_action_state();
    void refresh_scope_notice();
    void refresh_network_status();
    void refresh_tip();
    void refit_for_preferences();
    void update_text_layout();

    LayoutStyle m_style{ LayoutStyle::Full };
    bool          m_refreshing{ false };

    wxPanel*      m_scope_banner{ nullptr };
    wxStaticText* m_scope_notice{ nullptr };
    wxStaticText* m_network_plugin_status{ nullptr };
    wxStaticText* m_network_printer_status{ nullptr };
    wxStaticText* m_network_account_status{ nullptr };
    wxStaticText* m_account_status{ nullptr };
    wxStaticText* m_account_badge{ nullptr };
    wxStaticText* m_tip_text{ nullptr };
    wxStaticText* m_stat_failures{ nullptr };
    wxStaticText* m_stat_successes{ nullptr };
    wxStaticText* m_stat_recent{ nullptr };
    wxStaticText* m_hero_score{ nullptr };
    wxStaticText* m_hero_headline{ nullptr };
    ProgressBar*  m_hero_gauge{ nullptr };
    wxPanel*      m_filament_banner{ nullptr };
    wxStaticText* m_filament_notice{ nullptr };
    wxPanel*      m_insights_card{ nullptr };
    wxPanel*      m_insights_body{ nullptr };
    wxBoxSizer*   m_insights_inner{ nullptr };
    CheckBox*     m_enable_cb{ nullptr };
    ComboBox*     m_auto_load_choice{ nullptr };
    CheckBox*     m_learning_auto_cb{ nullptr };
    CheckBox*     m_safe_mode_cb{ nullptr };
    Button*       m_rollback_btn{ nullptr };
    Button*       m_scan_btn{ nullptr };
    Button*       m_load_profile_btn{ nullptr };
    Button*       m_login_btn{ nullptr };
    Button*       m_logout_btn{ nullptr };
    Button*       m_analyze_btn{ nullptr };
    Button*       m_smart_slice_btn{ nullptr };
    Button*       m_print_btn{ nullptr };
    Button*       m_analyze_all_btn{ nullptr };
    Button*       m_quick_apply_btn{ nullptr };
    Button*       m_open_details_btn{ nullptr };
    Button*       m_export_btn{ nullptr };
    wxStaticText* m_printer_learning{ nullptr };
    Button*       m_history_btn{ nullptr };
    Button*       m_privacy_btn{ nullptr };
};

}} // namespace

#endif

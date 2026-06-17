#ifndef slic3r_BambuSmartPrintService_hpp_
#define slic3r_BambuSmartPrintService_hpp_

#include <deque>
#include <string>
#include <vector>
#include <wx/string.h>
#include <wx/weakref.h>
#include "libslic3r/BambuSmartPrint/BambuSmartPrintTypes.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {
class Model;
class DynamicPrintConfig;
class MachineObject;
class Print;

namespace GUI {

class Plater;
class BambuSmartPrintPanel;
class BambuSmartPrintPrepareBar;
struct SmartPrintWorkflowContent;

class BambuSmartPrintService
{
public:
    static BambuSmartPrintService& instance();

    static bool is_enabled();
    static void set_enabled(bool enabled);
    enum class AutoLoadMode : int {
        Off = 0,
        Notify = 1,
        FullDialog = 2, // Apply AI recommendations via Ollama on model load
    };

    static AutoLoadMode auto_load_mode();
    static void set_auto_load_mode(AutoLoadMode mode);
    static bool auto_on_model_load(); // true if Notify or FullDialog
    static void set_auto_on_model_load(bool enabled); // maps to Off / FullDialog

    static bool learning_auto_apply();
    static void set_learning_auto_apply(bool enabled);
    static bool safe_mode_enabled();
    static void set_safe_mode_enabled(bool enabled);
    static bool should_handle(const MachineObject& obj);

    static bool is_bbl_printer_active();
    static wxString bbl_printer_required_message();

    void initialize();
    void on_models_loaded(Plater* plater);
    void on_print_state_changed(MachineObject* obj, const std::string& old_state, const std::string& new_state);
    void on_slice_completed(Plater* plater, const Print* print, bool success);

    void analyze_current_plate(Plater* plater);
    void analyze_all_plates(Plater* plater);
    void quick_apply_current_plate(Plater* plater, wxWindow* parent = nullptr);
    void export_current_plate_report(Plater* plater, wxWindow* parent = nullptr);
    void run_smart_slice(Plater* plater);
    /** Load settings → slice → open Send print job (minimal Smart Print prompts). */
    void run_one_click_print(Plater* plater);
    /** Apply queued failure fixes, then run one-click Print. */
    void run_reprint_with_failure_fixes(Plater* plater);
    bool has_reprintable_failure() const;
    wxString pending_failure_summary() const;
    wxString pending_failure_action_line() const;
    static bool try_fix_filament_mismatch(Plater* plater);
    bool one_click_print_active() const { return m_one_click_active; }

    enum class OneClickPhase : int {
        None = 0,
        Analyzing,
        Applying,
        Slicing,
        Exporting,
    };
    OneClickPhase one_click_phase() const { return m_one_click_phase; }
    size_t        pending_failure_count() const { return m_pending_failures.size(); }
    bool          has_storage_save_error() const { return m_storage_save_error_notified; }

    static void prompt_enable_smart_print(wxWindow* parent);
    static bool try_activate_bbl_printer_profile(Plater* plater);
    static void on_first_guide_completed();
    void        show_pending_failure_notification(Plater* plater);
    void        flush_pending_failure_dialog();
    void show_history_dialog(wxWindow* parent);
    void show_privacy_dialog(wxWindow* parent);
    void schedule_auto_workflow_after_load(Plater* plater);
    void open_full_workflow_for_current_plate(Plater* plater);

    bool is_bambu_account_logged_in() const;
    wxString bambu_account_status_text() const;
    void prompt_bambu_login(wxWindow* parent = nullptr);
    void prompt_bambu_logout();

    void register_preferences_panel(BambuSmartPrintPanel* panel);
    void unregister_preferences_panel(BambuSmartPrintPanel* panel);
    void register_main_panel(BambuSmartPrintPanel* panel);
    void unregister_main_panel(BambuSmartPrintPanel* panel);
    void register_prepare_bar(BambuSmartPrintPrepareBar* bar);
    void unregister_prepare_bar(BambuSmartPrintPrepareBar* bar);
    void refresh_all_panels();
    void on_app_preset_context_changed();

    void show_settings_compare(const DynamicPrintConfig& before, const DynamicPrintConfig& after,
                               const std::string& title = "",
                               const std::vector<BambuSmartPrint::SettingChange>* change_reasons = nullptr);
    void apply_config_to_plater(Plater* plater, const DynamicPrintConfig& before,
                                const DynamicPrintConfig& proposed,
                                bool trigger_reslice, bool show_compare = true);
    bool apply_config_with_workflow(Plater* plater, const DynamicPrintConfig& before,
                                    const DynamicPrintConfig& proposed,
                                    bool trigger_reslice,
                                    const std::vector<BambuSmartPrint::SettingChange>* change_reasons = nullptr);
    bool restore_last_baseline(Plater* plater);
    bool rollback_last_apply(Plater* plater);
    void scan_bambu_printers_on_network();
    bool load_printer_profile_for_selected_device(Plater* plater);
    bool show_settings_compare_approval(const DynamicPrintConfig& before, const DynamicPrintConfig& after,
                                        const std::string& title,
                                        const std::vector<BambuSmartPrint::SettingChange>* change_reasons = nullptr);

    const DynamicPrintConfig& last_baseline_config() const { return m_last_baseline; }
    const DynamicPrintConfig& last_applied_config() const { return m_last_applied; }
    const BambuSmartPrint::ModelAnalysis& last_mesh_analysis() const { return m_last_mesh_analysis; }
    const BambuSmartPrint::SliceAnalysis& last_slice_analysis() const { return m_last_slice_analysis; }
    const BambuSmartPrint::ReadinessReport& last_readiness_report() const { return m_last_readiness; }
    const BambuSmartPrint::AutoSettingsResult& last_auto_result() const { return m_last_auto_result; }
    const BambuSmartPrint::SuccessPrediction& last_prediction() const { return m_last_prediction; }
    const std::string& last_estimated_print_time() const { return m_last_estimated_print_time; }
    double last_estimated_filament_g() const { return m_last_estimated_filament_g; }
    wxString last_slice_estimate_text() const;
    /** Recompute mesh/readiness cache for the current plate (no UI refresh). */
    void update_plate_assessment_data(Plater* plater);
    void refresh_plate_snapshot(Plater* plater);
    void refresh_post_slice_assessment(Plater* plater);

private:
    BambuSmartPrintService() = default;

    bool show_workflow_dialog(Plater* plater, const SmartPrintWorkflowContent& content,
                              const DynamicPrintConfig& before, const DynamicPrintConfig& proposed,
                              const std::string& compare_title, const std::string& filament_name,
                              const std::vector<BambuSmartPrint::SettingChange>* change_reasons = nullptr);

    void run_auto_workflow(Plater* plater);
    void auto_prepare_safe_on_load(Plater* plater);
    void notify_model_load_summary(Plater* plater);
    void handle_print_failed(MachineObject* obj);
    void handle_print_cancelled(MachineObject* obj);
    void handle_print_success(MachineObject* obj);
    void notify_storage_errors();
    void notify_storage_save_error(const std::string& detail);
    void check_smart_print_persistence();
    void set_one_click_phase(OneClickPhase phase);
    void maybe_notify_slice_analysis(Plater* plater);
    void enqueue_failure_workflow(MachineObject* obj);
    void capture_active_print_config(MachineObject* obj);
    void clear_active_print_config();
    DynamicPrintConfig config_for_print_record(MachineObject* obj) const;

    DynamicPrintConfig m_last_baseline;
    DynamicPrintConfig m_last_applied;
    BambuSmartPrint::ModelAnalysis   m_last_mesh_analysis;
    BambuSmartPrint::SliceAnalysis   m_last_slice_analysis;
    BambuSmartPrint::ReadinessReport      m_last_readiness;
    BambuSmartPrint::AutoSettingsResult   m_last_auto_result;
    BambuSmartPrint::SuccessPrediction    m_last_prediction;
    bool m_workflow_running{ false };
    bool m_pending_smart_slice_followup{ false };
    int  m_post_slice_apply_rounds{ 0 };
    bool m_one_click_active{ false };
    bool m_one_click_pending_send{ false };
    OneClickPhase m_one_click_phase{ OneClickPhase::None };
    bool m_silent_workflow{ false };
    bool m_storage_save_error_notified{ false };
    std::string m_last_finish_printer_id;
    int64_t     m_last_finish_ms{ 0 };
    wxWeakRef<BambuSmartPrintPanel> m_preferences_panel;
    wxWeakRef<BambuSmartPrintPanel> m_main_panel;
    wxWeakRef<BambuSmartPrintPrepareBar> m_prepare_bar;

    struct PendingFailureDialog {
        BambuSmartPrint::PrintFailureRecord record;
        BambuSmartPrint::AutoSettingsResult fixes;
    };
    std::deque<PendingFailureDialog> m_pending_failures;

    DynamicPrintConfig m_active_print_config;
    std::string        m_active_print_printer_id;
    std::string        m_active_print_job_id;
    std::string        m_active_print_config_hash;
    int                m_active_print_plate_index{ -1 };
    bool               m_active_print_config_verified{ false };
    bool               m_has_active_print_config{ false };
    std::string        m_last_estimated_print_time;
    double             m_last_estimated_filament_g{ 0.0 };
};

} // namespace GUI
} // namespace Slic3r

#endif

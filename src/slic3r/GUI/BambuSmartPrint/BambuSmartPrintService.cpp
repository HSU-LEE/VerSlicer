#include "BambuSmartPrintService.hpp"
#include "BambuSmartPrintPanel.hpp"
#include "BambuSmartPrintPrepareBar.hpp"
#include "FirstPrintExperience.hpp"
#include "PrintReadinessGate.hpp"
#include "SlicePilotNetworkSetup.hpp"
#include "SlicePilotOnboardingCoordinator.hpp"
#include "SlicePilotOnboardingFunnel.hpp"
#include "SlicePilotSetupHub.hpp"
#include "SlicePilotSimpleLayout.hpp"
#include "libslic3r/BambuSmartPrint/BambuErrorCatalog.hpp"
#include "BambuSmartPrintCompareDialog.hpp"
#include "BambuSmartPrintWorkflowDialog.hpp"
#include "BambuSmartPrintHistoryDialog.hpp"
#include "BambuSmartPrintPrivacyDialog.hpp"
#include "BambuSmartPrintBatchDialog.hpp"

#include "../GUI_App.hpp"
#include "../MainFrame.hpp" // EVT_UPDATE_MACHINE_LIST
#include "../Plater.hpp"
#include "../Tab.hpp"
#include "../PartPlate.hpp"
#include "../DeviceManager.hpp"
#include "../DeviceCore/DevManager.h"
#include "../DeviceCore/DevHMS.h"
#include "../DeviceCore/DevBed.h"
#include "../DeviceCore/DevExtruderSystem.h"
#include "../DeviceCore/DevDefs.h"
#include "../I18N.hpp"
#include "../GUI.hpp"

#include "slic3r/Utils/ICloudServiceAgent.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"

#include "libslic3r/BambuSmartPrint/AutoSettingsEngine.hpp"
#include "libslic3r/BambuSmartPrint/MeshGeometryAnalyzer.hpp"
#include "libslic3r/BambuSmartPrint/MeshAnalysisCache.hpp"
#include "libslic3r/BambuSmartPrint/ConfigSnapshot.hpp"
#include "libslic3r/BambuSmartPrint/FailureDatabase.hpp"
#include "libslic3r/BambuSmartPrint/FailureAnalyzer.hpp"
#include "libslic3r/BambuSmartPrint/SettingsOptimizer.hpp"
#include "libslic3r/BambuSmartPrint/SuccessPredictor.hpp"
#include "libslic3r/BambuSmartPrint/PrintReadinessEngine.hpp"
#include "libslic3r/BambuSmartPrint/PlateBatchPlanner.hpp"
#include "libslic3r/BambuSmartPrint/SmartPrintReportExporter.hpp"
#include "libslic3r/BambuSmartPrint/PrinterLearningStore.hpp"
#include "libslic3r/BambuSmartPrint/SliceGeometryAnalyzer.hpp"
#include "libslic3r/BambuSmartPrint/BambuSmartPrintPaths.hpp"
#include "libslic3r/BambuSmartPrint/SafeModeGuard.hpp"
#include "libslic3r/BambuSmartPrint/ConfigVersionStack.hpp"
#include "libslic3r/BambuSmartPrint/FailureLogBundle.hpp"
#include "libslic3r/BambuSmartPrint/OrcaProfileMapper.hpp"
#include "SmartPrintOrchestrator.hpp"
#include "BambuSmartPrintUi.hpp"
#include <nlohmann/json.hpp>
#include "libslic3r/Print.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/SlicePilot/SlicePilotRestrictions.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/SlicePilot/SlicePilotRestrictions.hpp"

#include <wx/msgdlg.h>
#include <wx/filedlg.h>
#include <algorithm>
#include <chrono>
#include <atomic>
#include <boost/log/trivial.hpp>
#include "../NotificationManager.hpp"

namespace Slic3r { namespace GUI {

namespace {

bool is_print_object_config_key(const std::string& key)
{
    static const DynamicPrintConfig object_keys = [] {
        DynamicPrintConfig cfg;
        cfg.apply(PrintObjectConfig::defaults());
        return cfg;
    }();
    return object_keys.has(key);
}

struct PreparedPlateWorkflow {
    DynamicPrintConfig              base;
    DynamicPrintConfig              proposed;
    std::string                     printer_id;
    std::string                     filament_name;
    BambuSmartPrint::AutoSettingsResult auto_result;
    BambuSmartPrint::SuccessPrediction  prediction;
    BambuSmartPrint::ModelAnalysis      mesh;
    size_t                          change_count{ 0 };
};

bool prepare_plate_workflow(Plater* plater, PresetBundle* bundle, PreparedPlateWorkflow& out)
{
    if (!plater || !bundle)
        return false;

    out.base = bundle->full_config();
    if (PartPlate* plate = plater->get_partplate_list().get_curr_plate())
        out.base.apply(*plate->config());

    out.printer_id = "local";
    if (wxGetApp().getDeviceManager()) {
        if (MachineObject* sel = wxGetApp().getDeviceManager()->get_selected_machine())
            out.printer_id = sel->get_dev_id();
    }

    auto& learning_store = BambuSmartPrint::PrinterLearningStore::instance();
    const BambuSmartPrint::PrinterLearningProfile learning = learning_store.get_profile(out.printer_id);

    std::vector<ModelObject*> plate_objects;
    PartPlate* plate = plater->get_partplate_list().get_curr_plate();
    if (plate)
        plate_objects = plate->get_objects_on_this_plate();
    if (plate_objects.empty())
        return false;

    out.proposed = out.base;
    learning_store.apply_learning_to_config(out.printer_id, out.proposed);

    out.mesh = BambuSmartPrint::MeshGeometryAnalyzer::analyze_objects(plate_objects, out.base);
    const BambuSmartPrint::SliceAnalysis* slice_ptr = nullptr;
    const BambuSmartPrint::SliceAnalysis& slice_analysis =
        BambuSmartPrintService::instance().last_slice_analysis();
    if (slice_analysis.valid)
        slice_ptr = &slice_analysis;
    out.auto_result = BambuSmartPrint::AutoSettingsEngine::suggest_settings_for_objects(
        plate_objects, out.proposed, &learning, slice_ptr);
    out.proposed = out.auto_result.config_delta;
    out.filament_name = BambuSmartPrint::AutoSettingsEngine::suggest_filament_preset_name(*bundle, out.mesh);
    out.prediction = BambuSmartPrint::SuccessPredictor::predict(
        out.printer_id, out.mesh, out.proposed, learning, slice_ptr);
    out.change_count = BambuSmartPrint::ConfigSnapshot::diff(out.base, out.proposed).size();
    return true;
}

static BambuSmartPrint::ReadinessReport readiness_from_prep(const PreparedPlateWorkflow& prep,
                                                            const BambuSmartPrint::SliceAnalysis* slice)
{
    const BambuSmartPrint::PrinterLearningProfile learning =
        BambuSmartPrint::PrinterLearningStore::instance().get_profile(prep.printer_id);
    return BambuSmartPrint::PrintReadinessEngine::evaluate(
        prep.mesh, prep.proposed, learning, prep.prediction, slice, prep.change_count);
}

SmartPrintWorkflowContent workflow_content_from(const PreparedPlateWorkflow& prep,
                                                const BambuSmartPrint::ReadinessReport& readiness)
{
    SmartPrintWorkflowContent content;
    content.summary            = prep.auto_result.summary;
    content.suggested_material = prep.mesh.suggested_material;
    content.prediction_summary = prep.prediction.summary;
    content.readiness_headline = readiness.headline;
    content.active_filament    = readiness.active_filament_hint;
    content.filament_mismatch  = readiness.filament_mismatch;
    content.success_rate       = prep.prediction.success_rate;
    content.complexity_score   = prep.mesh.complexity_score;
    content.change_count       = prep.change_count;
    content.risk_factors       = prep.prediction.risk_factors;
    content.insights           = readiness.insights;
    content.show_success_gauge = true;
    content.is_failure_workflow = false;

    for (const auto& ch : prep.auto_result.changes)
        content.change_preview.push_back(ch.key + ": " + ch.reason);
    return content;
}

static bool is_active_print_state(const std::string& state)
{
    return state == "RUNNING" || state == "PAUSE" || state == "PREPARE" || state == "SLICING";
}

static bool is_cancel_terminal_state(const std::string& state)
{
    return state == "CANCEL" || state == "CANCELLED";
}

static bool is_running_print_state(const std::string& state)
{
    return state == "RUNNING" || state == "PREPARE" || state == "SLICING";
}

static DynamicPrintConfig capture_current_plate_config()
{
    DynamicPrintConfig cfg;
    if (wxGetApp().preset_bundle)
        cfg = wxGetApp().preset_bundle->full_config();
    if (Plater* plater = wxGetApp().plater()) {
        if (PartPlate* plate = plater->get_partplate_list().get_curr_plate())
            cfg.apply(*plate->config());
    }
    return cfg;
}

static wxWindow* smart_print_dialog_parent(wxWindow* preferred)
{
    if (preferred)
        return preferred;
    if (Plater* plater = wxGetApp().plater())
        return static_cast<wxWindow*>(plater);
    if (MainFrame* frame = wxGetApp().mainframe)
        return static_cast<wxWindow*>(frame);
    return nullptr;
}

static std::string failure_category_advice(BambuSmartPrint::FailureCategory category)
{
    switch (category) {
    case BambuSmartPrint::FailureCategory::Adhesion:
        return "Bed adhesion issue — check leveling, clean the plate, and use brim";
    case BambuSmartPrint::FailureCategory::Filament:
        return "Filament issue — verify AMS slot mapping and spool feed";
    case BambuSmartPrint::FailureCategory::Temperature:
        return "Temperature issue — confirm filament type matches the loaded spool";
    case BambuSmartPrint::FailureCategory::Mechanical:
        return "Mechanical issue — check for obstructions and re-run calibration";
    case BambuSmartPrint::FailureCategory::Gcode:
        return "G-code issue — re-slice with Smart Print suggested settings";
    case BambuSmartPrint::FailureCategory::Network:
        return "Connection issue — verify LAN access and printer online status";
    default:
        return "Review the printer HMS panel, then apply suggested fixes below";
    }
}

static SmartPrintWorkflowContent failure_workflow_content(
    const BambuSmartPrint::PrintFailureRecord& record,
    const BambuSmartPrint::AutoSettingsResult& fixes)
{
    SmartPrintWorkflowContent content;
    content.summary              = fixes.summary;
    content.diagnosis_title      = record.diagnosis.title;
    content.prediction_summary   = record.diagnosis.description;
    content.diagnosis_confidence = record.diagnosis.confidence;
    content.show_success_gauge   = false;
    content.is_failure_workflow  = true;
    content.change_count = BambuSmartPrint::ConfigSnapshot::diff(
        record.config_snapshot, fixes.config_delta).size();
    content.risk_factors.push_back(failure_category_advice(record.diagnosis.category));
    if (record.diagnosis.category == BambuSmartPrint::FailureCategory::Filament)
        content.risk_factors.push_back("Check AMS slot mapping and spool availability");
    content.diagnosis_uncertain = record.diagnosis.category == BambuSmartPrint::FailureCategory::Unknown
        || record.diagnosis.confidence < 0.7f;
    for (const BambuSmartPrint::SettingChange& fix : record.diagnosis.recommended_fixes) {
        if (!fix.key.empty())
            content.risk_factors.push_back("Suggested: " + fix.key + (fix.reason.empty() ? "" : " — " + fix.reason));
    }
    if (content.diagnosis_uncertain) {
        content.risk_factors.insert(content.risk_factors.begin(),
            "Error code not in catalog — suggestions are general; check the printer HMS panel");
        for (const std::string& code : record.hms_codes) {
            if (!code.empty())
                content.risk_factors.push_back("HMS: " + code);
        }
        if (record.mc_print_error_code != 0)
            content.risk_factors.push_back(
                "MC code: " + std::to_string(record.mc_print_error_code)
                + " (use Privacy & data → Add MC error code to improve future diagnosis)");
    }
    if (!record.config_snapshot_verified)
        content.risk_factors.insert(content.risk_factors.begin(),
            "Settings snapshot may not match job start — verify plate settings before applying fixes");
    for (const BambuSmartPrint::SettingChange& ch : fixes.changes)
        content.change_preview.push_back(ch.key + ": " + ch.reason);
    return content;
}

} // namespace

void BambuSmartPrintService::update_plate_assessment_data(Plater* plater)
{
    if (!plater || !wxGetApp().preset_bundle)
        return;

    try {
        PreparedPlateWorkflow prep;
        if (!prepare_plate_workflow(plater, wxGetApp().preset_bundle, prep))
            return;

        m_last_mesh_analysis = prep.mesh;
        m_last_baseline      = prep.base;
        m_last_applied       = prep.proposed;
        m_last_auto_result   = prep.auto_result;
        m_last_prediction    = prep.prediction;

        const BambuSmartPrint::PrinterLearningProfile learning =
            BambuSmartPrint::PrinterLearningStore::instance().get_profile(prep.printer_id);
        const BambuSmartPrint::SliceAnalysis* slice_ptr =
            m_last_slice_analysis.valid ? &m_last_slice_analysis : nullptr;
        m_last_readiness = BambuSmartPrint::PrintReadinessEngine::evaluate(
            prep.mesh, prep.proposed, learning, prep.prediction, slice_ptr, prep.change_count);
        SlicePilotOnboardingFunnel::record_smart_analysis_done();
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint update_plate_assessment_data: " << ex.what();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint update_plate_assessment_data: unknown error";
    }
}

void BambuSmartPrintService::refresh_post_slice_assessment(Plater* plater)
{
    update_plate_assessment_data(plater);
    refresh_all_panels();
}

void BambuSmartPrintService::refresh_plate_snapshot(Plater* plater)
{
    update_plate_assessment_data(plater);
}

static const char* kEnabledKey = "bambu_smart_print_enabled";
static const char* kAutoOnLoadKey = "bambu_smart_print_auto_on_load";
static const char* kAutoLoadModeKey = "bambu_smart_print_auto_load_mode";
static const char* kLearningAutoApplyKey = "bambu_smart_print_learning_auto_apply";
static const char* kSafeModeKey = "bambu_smart_print_safe_mode";

BambuSmartPrintService& BambuSmartPrintService::instance()
{
    static BambuSmartPrintService s;
    return s;
}

bool BambuSmartPrintService::is_enabled()
{
    if (!wxGetApp().app_config)
        return false;
    if (!wxGetApp().app_config->has(kEnabledKey))
        return true;
    return wxGetApp().app_config->get_bool(kEnabledKey);
}

void BambuSmartPrintService::set_enabled(bool enabled)
{
    if (wxGetApp().app_config)
        wxGetApp().app_config->set_bool(kEnabledKey, enabled);
}

BambuSmartPrintService::AutoLoadMode BambuSmartPrintService::auto_load_mode()
{
    if (!wxGetApp().app_config)
        return AutoLoadMode::Notify;
    if (wxGetApp().app_config->has(kAutoLoadModeKey)) {
        try {
            const int v = std::stoi(wxGetApp().app_config->get(kAutoLoadModeKey));
            if (v >= int(AutoLoadMode::Off) && v <= int(AutoLoadMode::FullDialog))
                return static_cast<AutoLoadMode>(v);
        } catch (...) {}
    }
    if (wxGetApp().app_config->has(kAutoOnLoadKey))
        return wxGetApp().app_config->get_bool(kAutoOnLoadKey) ? AutoLoadMode::FullDialog : AutoLoadMode::Off;
    return AutoLoadMode::Notify;
}

void BambuSmartPrintService::set_auto_load_mode(AutoLoadMode mode)
{
    if (!wxGetApp().app_config)
        return;
    wxGetApp().app_config->set(kAutoLoadModeKey, std::to_string(int(mode)));
    wxGetApp().app_config->set_bool(kAutoOnLoadKey, mode != AutoLoadMode::Off);
}

bool BambuSmartPrintService::auto_on_model_load()
{
    return auto_load_mode() != AutoLoadMode::Off;
}

void BambuSmartPrintService::set_auto_on_model_load(bool enabled)
{
    set_auto_load_mode(enabled ? AutoLoadMode::FullDialog : AutoLoadMode::Off);
}

bool BambuSmartPrintService::learning_auto_apply()
{
    if (!wxGetApp().app_config)
        return true;
    if (!wxGetApp().app_config->has(kLearningAutoApplyKey))
        return true;
    return wxGetApp().app_config->get_bool(kLearningAutoApplyKey);
}

void BambuSmartPrintService::set_learning_auto_apply(bool enabled)
{
    if (wxGetApp().app_config)
        wxGetApp().app_config->set_bool(kLearningAutoApplyKey, enabled);
}

bool BambuSmartPrintService::safe_mode_enabled()
{
    return BambuSmartPrint::SafeModeGuard::is_enabled();
}

void BambuSmartPrintService::set_safe_mode_enabled(bool enabled)
{
    BambuSmartPrint::SafeModeGuard::set_enabled(enabled);
    if (wxGetApp().app_config)
        wxGetApp().app_config->set_bool(kSafeModeKey, enabled);
}

bool BambuSmartPrintService::should_handle(const MachineObject& obj)
{
    if (!is_enabled())
        return false;
    if (!is_bbl_printer_active())
        return false;
    return !obj.get_dev_id().empty();
}

bool BambuSmartPrintService::is_bbl_printer_active()
{
    return wxGetApp().preset_bundle && SlicePilot::is_active_printer_bbl(*wxGetApp().preset_bundle);
}

wxString BambuSmartPrintService::bbl_printer_required_message()
{
    return _L("Smart Print works only with Bambu Lab printer profiles.\n\n"
              "Select a Bambu Lab printer under the printer preset menu. "
              "Other vendor profiles (Prusa, Creality, etc.) are not supported.");
}

void BambuSmartPrintService::notify_storage_save_error(const std::string& detail)
{
    if (m_storage_save_error_notified || detail.empty())
        return;
    m_storage_save_error_notified = true;
    wxGetApp().CallAfter([detail]() {
        wxWindow* parent = wxGetApp().plater();
        wxString msg = wxString::Format(
            _L("Smart Print could not save local data:\n\n%s\n\n"
               "Check disk space and folder permissions. New history may not persist until this is fixed."),
            wxString::FromUTF8(detail));
        show_error(parent, msg);
    });
}

void BambuSmartPrintService::check_smart_print_persistence()
{
    const std::string& db_err = BambuSmartPrint::FailureDatabase::instance().last_save_error();
    if (!db_err.empty()) {
        notify_storage_save_error(db_err);
        return;
    }
    const std::string& learn_err = BambuSmartPrint::PrinterLearningStore::instance().last_save_error();
    if (!learn_err.empty()) {
        notify_storage_save_error(learn_err);
        return;
    }
    m_storage_save_error_notified = false;
}

void BambuSmartPrintService::notify_storage_errors()
{
    wxString msg;
    bool any = false;

    for (const std::string& e : BambuSmartPrint::FailureDatabase::instance().load_error_messages()) {
        msg << "• " << wxString::FromUTF8(e) << "\n";
        any = true;
    }
    if (BambuSmartPrint::PrinterLearningStore::instance().had_load_error()) {
        msg << "• " << _L("Printer learning data could not be loaded (backup created).") << "\n";
        any = true;
    }
    if (!any)
        return;

    wxString full = _L("Smart Print had trouble loading saved data:\n\n");
    full << msg << "\n" << _L("New sessions may start with empty history for affected files.");
    wxMessageBox(full, wxString::Format(_L("%s Smart Print"), SLIC3R_APP_FULL_NAME),
                 wxOK | wxICON_WARNING, wxGetApp().plater());
}

void BambuSmartPrintService::initialize()
{
    if (wxGetApp().app_config) {
        FirstPrintExperience::initialize_defaults(wxGetApp().app_config);
        SlicePilotSimpleLayout::initialize_defaults(wxGetApp().app_config);
        if (!wxGetApp().app_config->has(kEnabledKey))
            wxGetApp().app_config->set_bool(kEnabledKey, true);
        if (!wxGetApp().app_config->has(kAutoLoadModeKey))
            wxGetApp().app_config->set(kAutoLoadModeKey, std::to_string(int(AutoLoadMode::FullDialog)));
        if (!wxGetApp().app_config->has(kLearningAutoApplyKey))
            wxGetApp().app_config->set_bool(kLearningAutoApplyKey, true);
        if (!wxGetApp().app_config->has(kSafeModeKey))
            wxGetApp().app_config->set_bool(kSafeModeKey, true);
        BambuSmartPrint::SafeModeGuard::set_enabled(wxGetApp().app_config->get_bool(kSafeModeKey));
        const wxString lang = wxGetApp().current_language_code().BeforeFirst('_');
        BambuSmartPrint::BambuErrorCatalog::set_prefer_korean_ui(lang == "ko");
    }

    std::vector<std::string> errors;
    BambuSmartPrint::FailureDatabase::instance().load(&errors);
    BambuSmartPrint::PrinterLearningStore::instance().load(&errors);
    BambuSmartPrint::ConfigVersionStack::instance().load();

    if (BambuSmartPrint::FailureDatabase::instance().had_load_error()
        || BambuSmartPrint::PrinterLearningStore::instance().had_load_error())
        wxGetApp().CallAfter([this]() { notify_storage_errors(); });

    wxGetApp().CallAfter([this]() { scan_bambu_printers_on_network(); });
    SlicePilotOnboardingCoordinator::schedule_post_init();
}

bool BambuSmartPrintService::try_fix_filament_mismatch(Plater* plater)
{
    return PrintReadinessGate::try_fix_filament_mismatch(plater);
}

void BambuSmartPrintService::register_preferences_panel(BambuSmartPrintPanel* panel)
{
    m_preferences_panel = panel;
}

void BambuSmartPrintService::unregister_preferences_panel(BambuSmartPrintPanel* panel)
{
    if (m_preferences_panel == panel)
        m_preferences_panel = nullptr;
}

void BambuSmartPrintService::register_main_panel(BambuSmartPrintPanel* panel)
{
    m_main_panel = panel;
}

void BambuSmartPrintService::unregister_main_panel(BambuSmartPrintPanel* panel)
{
    if (m_main_panel == panel)
        m_main_panel = nullptr;
}

void BambuSmartPrintService::register_prepare_bar(BambuSmartPrintPrepareBar* bar)
{
    m_prepare_bar = bar;
}

void BambuSmartPrintService::unregister_prepare_bar(BambuSmartPrintPrepareBar* bar)
{
    if (m_prepare_bar == bar)
        m_prepare_bar = nullptr;
}

void BambuSmartPrintService::refresh_all_panels()
{
    try {
        if (m_preferences_panel)
            m_preferences_panel->refresh_all();
        if (m_main_panel)
            m_main_panel->refresh_all();
        if (m_prepare_bar)
            m_prepare_bar->refresh();
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint refresh_all_panels: " << ex.what();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint refresh_all_panels: unknown error";
    }
}

void BambuSmartPrintService::set_one_click_phase(OneClickPhase phase)
{
    m_one_click_phase = phase;
    if (m_prepare_bar)
        m_prepare_bar->refresh();
}

void BambuSmartPrintService::prompt_enable_smart_print(wxWindow* parent)
{
    wxMessageDialog dlg(parent,
        _L("Smart Print is turned off.\n\nEnable it to analyze models, suggest settings, and use one-click Print from the bar above the build plate."),
        _L("Enable Smart Print"),
        wxYES_NO | wxICON_INFORMATION);
    dlg.SetYesNoLabels(_L("Open Smart Print"), _L("Not now"));
    if (dlg.ShowModal() == wxID_YES)
        wxGetApp().open_smart_print();
}

bool BambuSmartPrintService::try_activate_bbl_printer_profile(Plater* plater)
{
    if (!plater || !wxGetApp().preset_bundle)
        return false;
    if (is_bbl_printer_active())
        return true;
    PresetBundle* bundle = wxGetApp().preset_bundle;
    for (const Preset& preset : bundle->printers) {
        if (!SlicePilot::is_bbl_printer_preset(preset, bundle))
            continue;
        if (!preset.is_visible || !preset.is_compatible)
            continue;
        if (bundle->printers.select_preset_by_name(preset.name, true)) {
            plater->on_config_change(bundle->full_config(false));
            instance().refresh_all_panels();
            return true;
        }
    }
    return false;
}


void BambuSmartPrintService::on_first_guide_completed()
{
    SlicePilotOnboardingCoordinator::on_guide_completed();
}

void BambuSmartPrintService::show_pending_failure_notification(Plater* plater)
{
    if (!plater || m_pending_failures.empty())
        return;
    NotificationManager* nm = plater->get_notification_manager();
    if (!nm)
        return;
    const size_t n = m_pending_failures.size();
    wxString msg = n == 1
        ? _L("Print failed — press Reprint on the Smart Print bar to apply fixes.")
        : wxString::Format(_L("%zu print failures queued — Smart Print will show diagnosis when the current dialog closes."),
                           n);
    nm->push_notification(NotificationType::CustomNotification,
        NotificationManager::NotificationLevel::WarningNotificationLevel,
        std::string(msg.utf8_str()));
}

void BambuSmartPrintService::on_app_preset_context_changed()
{
    refresh_all_panels();
}

void BambuSmartPrintService::show_settings_compare(const DynamicPrintConfig& before,
                                                   const DynamicPrintConfig& after,
                                                   const std::string& title,
                                                   const std::vector<BambuSmartPrint::SettingChange>* change_reasons)
{
    wxWindow* parent = wxGetApp().plater();
    try {
        BambuSmartPrintCompareDialog dlg(parent, before, after, title, change_reasons);
        SlicePilotUi::show_modal_with_auto_default(&dlg, wxID_OK);
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint compare dialog: " << ex.what();
        show_error(parent,
            wxString::Format(_L("Could not show settings preview:\n\n%s"), wxString::FromUTF8(ex.what())));
    }
}

void BambuSmartPrintService::apply_config_to_plater(Plater* plater, const DynamicPrintConfig& before,
                                                    const DynamicPrintConfig& proposed,
                                                    bool trigger_reslice, bool show_compare)
{
    apply_config_with_workflow(plater, before, proposed, trigger_reslice, nullptr);
    if (show_compare && plater && wxGetApp().preset_bundle) {
        Preset& print_preset = wxGetApp().preset_bundle->prints.get_edited_preset();
        show_settings_compare(before, print_preset.config,
                              std::string(SLIC3R_APP_FULL_NAME) + " — applied settings");
    }
}

bool BambuSmartPrintService::apply_config_with_workflow(Plater* plater, const DynamicPrintConfig& before,
                                                        const DynamicPrintConfig& proposed,
                                                        bool trigger_reslice,
                                                        const std::vector<BambuSmartPrint::SettingChange>* change_reasons)
{
    if (!plater || !wxGetApp().preset_bundle) return false;
    PartPlate* plate = plater->get_partplate_list().get_curr_plate();
    if (!plate) return false;

    const BambuSmartPrint::SafeModeResult safe = BambuSmartPrint::SafeModeGuard::apply(before, proposed);
    DynamicPrintConfig effective = safe.config;
    if (safe.had_blocks && !m_silent_workflow) {
        wxString warn = _L("Safe mode blocked some risky changes:");
        for (const auto& b : safe.blocked_changes) {
            warn << "\n• " << wxString::FromUTF8(b.key);
        }
        show_info(plater, warn, _L("Smart Print — Safe mode"));
    }

    std::string printer_id = "local";
    if (wxGetApp().getDeviceManager()) {
        if (MachineObject* sel = wxGetApp().getDeviceManager()->get_selected_machine())
            printer_id = sel->get_dev_id();
    }
    BambuSmartPrint::ConfigVersionStack::instance().push("before_apply", printer_id,
        plater->get_partplate_list().get_curr_plate_index(), before);
    m_last_baseline = before;

    const std::vector<BambuSmartPrint::SettingChange> changes =
        BambuSmartPrint::ConfigSnapshot::diff(before, effective);
    if (changes.empty())
        return false;

    DynamicPrintConfig* plate_cfg = plate->config();
    DynamicPrintConfig plate_before = *plate_cfg;
    Preset& print_preset = wxGetApp().preset_bundle->prints.get_edited_preset();
    DynamicPrintConfig print_before = print_preset.config;

    bool plate_changed = false;
    bool print_changed = false;
    bool objects_changed = false;
    std::vector<size_t> changed_object_idxs;
    std::vector<std::string> skipped_keys;
    for (const BambuSmartPrint::SettingChange& ch : changes) {
        try {
            if (plate_cfg->has(ch.key)) {
                plate_cfg->set_deserialize_strict(ch.key, ch.new_value);
                plate_changed = true;
            } else if (print_preset.config.has(ch.key)) {
                print_preset.config.set_deserialize_strict(ch.key, ch.new_value);
                print_changed = true;
            } else if (is_print_object_config_key(ch.key)) {
                const std::vector<ModelObject*> plate_objects = plate->get_objects_on_this_plate();
                for (size_t idx = 0; idx < plater->model().objects.size(); ++idx) {
                    ModelObject* obj = plater->model().objects[idx];
                    if (!obj)
                        continue;
                    if (std::find(plate_objects.begin(), plate_objects.end(), obj) == plate_objects.end())
                        continue;
                    DynamicPrintConfig obj_cfg = obj->config.get();
                    obj_cfg.set_deserialize_strict(ch.key, ch.new_value);
                    obj->config.assign_config(obj_cfg);
                    changed_object_idxs.push_back(idx);
                    objects_changed = true;
                }
            } else {
                skipped_keys.push_back(ch.key);
            }
        } catch (const std::exception& ex) {
            BOOST_LOG_TRIVIAL(warning) << "BambuSmartPrint: skipped setting " << ch.key << ": " << ex.what();
            skipped_keys.push_back(ch.key);
        }
    }
    if (!skipped_keys.empty() && !m_silent_workflow) {
        wxString keys;
        for (size_t i = 0; i < skipped_keys.size() && i < 5; ++i) {
            if (i > 0) keys << ", ";
            keys << wxString::FromUTF8(skipped_keys[i]);
        }
        if (skipped_keys.size() > 5)
            keys << wxString::Format(_L(" … +%zu more"), skipped_keys.size() - 5);
        BOOST_LOG_TRIVIAL(info) << "BambuSmartPrint: skipped settings (not in current preset or plate): "
                                << keys.ToUTF8().data();
    }

    if (print_changed) {
        if (Tab* print_tab = wxGetApp().get_tab(Preset::TYPE_PRINT)) {
            print_tab->update_dirty();
            print_tab->reload_config();
        }
    }

    if (objects_changed)
        plater->changed_objects(changed_object_idxs);

    if (plate_changed || print_changed)
        plater->on_config_change(wxGetApp().preset_bundle->full_config(false));

    m_last_applied = effective;

    std::vector<std::string> applied_keys;
    applied_keys.reserve(changes.size());
    for (const BambuSmartPrint::SettingChange& ch : changes)
        if (!ch.key.empty())
            applied_keys.push_back(ch.key);

    if (MachineObject* sel = wxGetApp().getDeviceManager()
            ? wxGetApp().getDeviceManager()->get_selected_machine() : nullptr)
        BambuSmartPrint::PrinterLearningStore::instance().record_applied_suggestion(sel->get_dev_id(), applied_keys);
    else
        BambuSmartPrint::PrinterLearningStore::instance().record_applied_suggestion("local", applied_keys);

    if (trigger_reslice)
        plater->reslice();
    else
        plater->update();

    (void) change_reasons;
    return true;
}

bool BambuSmartPrintService::restore_last_baseline(Plater* plater)
{
    if (!plater || !wxGetApp().preset_bundle) return false;
    DynamicPrintConfig target = m_last_baseline;
    if (target.empty()) {
        BambuSmartPrint::ConfigVersionEntry entry;
        if (!BambuSmartPrint::ConfigVersionStack::instance().peek(&entry))
            return false;
        target = entry.config;
    }
    const DynamicPrintConfig current = SmartPrintOrchestrator::full_plate_config(plater, wxGetApp().preset_bundle);
    return apply_config_with_workflow(plater, current, target, true, nullptr);
}

bool BambuSmartPrintService::rollback_last_apply(Plater* plater)
{
    if (!plater) return false;
    BambuSmartPrint::ConfigVersionEntry entry;
    DynamicPrintConfig restored;
    if (!BambuSmartPrint::ConfigVersionStack::instance().restore_previous(&restored, &entry))
        return false;
    const DynamicPrintConfig current = SmartPrintOrchestrator::full_plate_config(plater, wxGetApp().preset_bundle);
    const bool ok = apply_config_with_workflow(plater, current, restored, true, nullptr);
    if (ok)
        show_info(plater, _L("Settings rolled back to the previous saved version."), _L("Smart Print"));
    else
        show_error(plater, _L("Rollback failed — no version in stack."));
    return ok;
}

void BambuSmartPrintService::scan_bambu_printers_on_network()
{
    if (NetworkAgent* agent = wxGetApp().getAgent()) {
        agent->start_discovery(true, false);
        auto* evt = new wxCommandEvent(EVT_UPDATE_MACHINE_LIST);
        wxQueueEvent(&wxGetApp(), evt);
    }
}

bool BambuSmartPrintService::load_printer_profile_for_selected_device(Plater* plater)
{
    if (!plater || !wxGetApp().preset_bundle || !wxGetApp().getDeviceManager())
        return false;
    MachineObject* obj = wxGetApp().getDeviceManager()->get_selected_machine();
    if (!obj) return false;
    const std::string preset = BambuSmartPrint::OrcaProfileMapper::suggest_printer_preset_name(
        *wxGetApp().preset_bundle, obj->printer_type);
    if (preset.empty()) return false;
    if (!BambuSmartPrint::OrcaProfileMapper::apply_printer_preset(*wxGetApp().preset_bundle, preset))
        return false;
    plater->on_config_change(wxGetApp().preset_bundle->full_config(false));
    show_info(plater, wxString::Format(_L("Loaded printer profile: %s"), wxString::FromUTF8(preset)),
              _L("Smart Print"));
    return true;
}

bool BambuSmartPrintService::show_settings_compare_approval(
    const DynamicPrintConfig& before, const DynamicPrintConfig& after,
    const std::string& title,
    const std::vector<BambuSmartPrint::SettingChange>* change_reasons)
{
    wxWindow* parent = smart_print_dialog_parent(wxGetApp().plater());
    if (!parent) return false;
    try {
        BambuSmartPrintCompareDialog dlg(parent, before, after, title, change_reasons, true);
        return SlicePilotUi::show_modal_with_auto_default(&dlg, wxID_OK) == wxID_OK;
    } catch (...) {
        return false;
    }
}

bool BambuSmartPrintService::show_workflow_dialog(Plater* plater, const SmartPrintWorkflowContent& content,
                                                  const DynamicPrintConfig& before, const DynamicPrintConfig& proposed,
                                                  const std::string& compare_title, const std::string& filament_name,
                                                  const std::vector<BambuSmartPrint::SettingChange>* change_reasons)
{
    wxWindow* dlg_parent = smart_print_dialog_parent(plater ? static_cast<wxWindow*>(plater) : nullptr);
    if (!dlg_parent) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint workflow: no parent window";
        return false;
    }
    try {
        BambuSmartPrintWorkflowDialog dlg(dlg_parent, content);
        const int rc = SlicePilotUi::show_modal_with_auto_default(&dlg, wxID_OK);
        if (rc != wxID_OK)
            return false;

        if (!dlg.preview_requested() && !dlg.apply_requested() && content.change_count > 0)
            dlg.confirm_auto_apply();

        if (dlg.preview_requested()) {
            show_settings_compare(before, proposed, compare_title.empty()
                ? std::string(SLIC3R_APP_FULL_NAME) + " — proposed changes"
                : compare_title, change_reasons);
            return false;
        }

        if (BambuSmartPrint::ConfigSnapshot::diff(before, proposed).empty())
            return true;

        if (!dlg.apply_requested())
            return false;

        if (!filament_name.empty() && wxGetApp().preset_bundle) {
            PresetBundle* bundle = wxGetApp().preset_bundle;
            if (bundle->filaments.find_preset(filament_name) != nullptr) {
                bundle->filaments.select_preset_by_name(filament_name, true);
                plater->on_filament_change(0);
            }
        }

        if (content.is_failure_workflow && !show_settings_compare_approval(before, proposed, compare_title, change_reasons))
            return false;
        apply_config_with_workflow(plater, before, proposed, true, change_reasons);
        return true;
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint workflow: " << ex.what();
        show_error(dlg_parent,
            wxString::Format(_L("Smart Print could not complete this action:\n\n%s"), wxString::FromUTF8(ex.what())));
        return false;
    }
}

void BambuSmartPrintService::analyze_current_plate(Plater* plater)
{
    wxWindow* parent = smart_print_dialog_parent(plater ? static_cast<wxWindow*>(plater) : nullptr);
    if (!plater) {
        show_error(parent, _L("No project is open."));
        return;
    }
    if (!is_enabled()) {
        prompt_enable_smart_print(parent);
        return;
    }
    if (!is_bbl_printer_active()) {
        wxMessageDialog dlg(parent, bbl_printer_required_message(), _L("Bambu Lab printer required"),
                          wxYES_NO | wxICON_WARNING);
        dlg.SetYesNoLabels(_L("Switch to Bambu profile"), _L("Cancel"));
        if (dlg.ShowModal() == wxID_YES)
            try_activate_bbl_printer_profile(plater);
        if (!is_bbl_printer_active())
            return;
    }
    if (plater->model().objects.empty()) {
        show_error(parent, _L("Load a model on the plate before running analysis."));
        return;
    }
    try {
        run_auto_workflow(plater);
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint analyze: " << ex.what();
        show_error(parent,
            wxString::Format(_L("Smart Print analysis failed:\n\n%s"), wxString::FromUTF8(ex.what())));
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint analyze: unknown error";
        show_error(parent, _L("Smart Print analysis failed due to an unexpected error."));
    }
}

void BambuSmartPrintService::analyze_all_plates(Plater* plater)
{
    wxWindow* parent = smart_print_dialog_parent(plater ? static_cast<wxWindow*>(plater) : nullptr);
    try {
        if (!plater) {
            show_error(parent, _L("No project is open."));
            return;
        }
        if (!is_enabled()) {
            show_error(parent, _L("Enable Smart Print in Preferences first."));
            return;
        }
        if (!is_bbl_printer_active()) {
            show_error(parent, bbl_printer_required_message());
            return;
        }
        if (plater->model().objects.empty()) {
            show_error(parent, _L("Load a model before running analysis."));
            return;
        }

        PartPlateList& list = plater->get_partplate_list();
        const int plate_count = list.get_plate_count();
        const int original    = list.get_curr_plate_index();

        std::string printer_id = "local";
        if (wxGetApp().getDeviceManager()) {
            if (MachineObject* sel = wxGetApp().getDeviceManager()->get_selected_machine())
                printer_id = sel->get_dev_id();
        }

        const int curr_plate = list.get_curr_plate_index();
        const BambuSmartPrint::SliceAnalysis* slice_ptr =
            m_last_slice_analysis.valid ? &m_last_slice_analysis : nullptr;

        const BambuSmartPrint::PlateBatchSummary batch = BambuSmartPrint::PlateBatchPlanner::analyze_all_plates(
            plate_count, [&](int i) -> BambuSmartPrint::PlateWorkflowInput {
                BambuSmartPrint::PlateWorkflowInput input;
                input.printer_id  = printer_id;
                input.plate_index = i;
                input.slice       = (slice_ptr && i == curr_plate) ? slice_ptr : nullptr;
                PartPlate* plate = list.get_plate(i);
                if (!plate || plate->empty())
                    return input;
                list.select_plate(i);
                input.objects = plate->get_objects_on_this_plate();
                if (wxGetApp().preset_bundle) {
                    input.base_config = wxGetApp().preset_bundle->full_config();
                    input.base_config.apply(*plate->config());
                }
                return input;
            });

        if (original >= 0 && original < plate_count)
            list.select_plate(original);

        if (batch.plates_with_models == 0) {
            show_error(parent, _L("No plates with models to analyze."));
            return;
        }

        BambuSmartPrintBatchDialog dlg(parent, batch);
        const int batch_rc = SlicePilotUi::show_modal_with_auto_default(&dlg, wxID_OK);
        if (!dlg.open_plate_requested() && batch_rc == wxID_OK)
            dlg.confirm_auto_open();
        if (batch_rc != wxID_OK || !dlg.open_plate_requested())
            return;

        const int pick = dlg.selected_plate_index();
        if (pick < 0 || pick >= plate_count)
            return;

        list.select_plate(pick);
        run_auto_workflow(plater);
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint analyze all plates: " << ex.what();
        show_error(parent,
            wxString::Format(_L("Smart Print could not analyze all plates:\n\n%s"), wxString::FromUTF8(ex.what())));
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint analyze all plates: unknown error";
        show_error(parent, _L("Smart Print could not analyze all plates due to an unexpected error."));
    }
}

void BambuSmartPrintService::quick_apply_current_plate(Plater* plater, wxWindow* parent)
{
    wxWindow* dlg_parent = smart_print_dialog_parent(parent ? parent : (plater ? static_cast<wxWindow*>(plater) : nullptr));
    if (!plater) {
        show_error(dlg_parent, _L("No project is open."));
        return;
    }
    if (!is_enabled() || !is_bbl_printer_active()) {
        show_error(dlg_parent, !is_enabled() ? _L("Enable Smart Print first.") : bbl_printer_required_message());
        return;
    }
    if (plater->model().objects.empty()) {
        show_error(dlg_parent, _L("Load a model on the plate first."));
        return;
    }

    try {
        PreparedPlateWorkflow prep;
        if (!prepare_plate_workflow(plater, wxGetApp().preset_bundle, prep))
            return;

        refresh_plate_snapshot(plater);

        if (prep.change_count == 0) {
            wxMessageBox(_L("No setting changes to apply — current plate already looks good."),
                         wxString::Format(_L("%s Smart Print"), SLIC3R_APP_FULL_NAME),
                         wxOK | wxICON_INFORMATION, dlg_parent);
            return;
        }

        if (!show_settings_compare_approval(prep.base, prep.proposed,
                std::string(SLIC3R_APP_FULL_NAME) + " — quick apply", &prep.auto_result.changes))
            return;

        apply_config_with_workflow(plater, prep.base, prep.proposed, true, &prep.auto_result.changes);
        m_last_applied = prep.proposed;
    } catch (const std::exception& ex) {
        show_error(dlg_parent,
            wxString::Format(_L("Quick apply failed:\n\n%s"), wxString::FromUTF8(ex.what())));
    }
}

void BambuSmartPrintService::export_current_plate_report(Plater* plater, wxWindow* parent)
{
    wxWindow* dlg_parent = smart_print_dialog_parent(parent ? parent : (plater ? static_cast<wxWindow*>(plater) : nullptr));
    if (!plater) {
        show_error(dlg_parent, _L("No project is open."));
        return;
    }

    refresh_plate_snapshot(plater);
    if (m_last_mesh_analysis.volume_mm3 <= 0.0) {
        show_error(dlg_parent, _L("Load a model on the plate before exporting a report."));
        return;
    }

    const int plate_idx = plater->get_partplate_list().get_curr_plate_index();
    wxString default_name = wxString::Format("slicepilot_plate%d_report.json", plate_idx + 1);
    wxString default_dir  = wxString::FromUTF8(BambuSmartPrint::smart_print_data_dir());

    wxFileDialog dlg(dlg_parent, _L("Export Smart Print report"), default_dir, default_name,
                     _L("JSON files (*.json)|*.json"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK)
        return;

    std::string err;
    const BambuSmartPrint::SliceAnalysis* slice_ptr =
        m_last_slice_analysis.valid ? &m_last_slice_analysis : nullptr;
    const bool ok = BambuSmartPrint::SmartPrintReportExporter::write_report_file(
        dlg.GetPath().utf8_string(), m_last_mesh_analysis, m_last_readiness,
        m_last_auto_result, m_last_prediction, plate_idx, nullptr, &err, slice_ptr);

    if (ok) {
        wxMessageBox(wxString::Format(_L("Report saved to:\n%s"), dlg.GetPath()),
                     wxString::Format(_L("%s Smart Print"), SLIC3R_APP_FULL_NAME),
                     wxOK | wxICON_INFORMATION, dlg_parent);
    } else {
        show_error(dlg_parent, wxString::Format(_L("Could not save report:\n%s"),
            err.empty() ? _L("Unknown error") : wxString::FromUTF8(err)));
    }
}

wxString BambuSmartPrintService::last_slice_estimate_text() const
{
    return SlicePilotUi::format_slice_estimate_summary(m_last_estimated_print_time, m_last_estimated_filament_g);
}

bool BambuSmartPrintService::has_reprintable_failure() const
{
    return !m_pending_failures.empty();
}

wxString BambuSmartPrintService::pending_failure_summary() const
{
    if (m_pending_failures.empty())
        return {};
    const auto& pending = m_pending_failures.front();
    wxString title = pending.record.diagnosis.title.empty()
        ? _L("Print failed")
        : wxString::FromUTF8(pending.record.diagnosis.title);
    if (pending.fixes.changes.empty())
        return title;
    return wxString::Format(_L("%s — %zu fix(es) ready"), title, pending.fixes.changes.size());
}

wxString BambuSmartPrintService::pending_failure_action_line() const
{
    if (m_pending_failures.empty())
        return {};
    const auto& pending = m_pending_failures.front();
    if (!pending.record.diagnosis.action_line.empty())
        return wxString::FromUTF8(pending.record.diagnosis.action_line);
    return pending_failure_summary();
}

void BambuSmartPrintService::run_reprint_with_failure_fixes(Plater* plater)
{
    wxWindow* parent = smart_print_dialog_parent(plater ? static_cast<wxWindow*>(plater) : nullptr);
    if (!plater || m_pending_failures.empty()) {
        show_error(parent, _L("No failure fixes are queued."));
        return;
    }
    if (m_workflow_running || m_one_click_active) {
        show_error(parent, _L("Smart Print is already running. Wait for the current step to finish."));
        return;
    }

    SlicePilotOnboardingFunnel::record_first_reprint();
    PendingFailureDialog pending = std::move(m_pending_failures.front());
    m_pending_failures.pop_front();
    refresh_all_panels();

    DynamicPrintConfig working = pending.record.config_snapshot;
    if (learning_auto_apply()) {
        std::string printer_id = pending.record.printer_id;
        if (printer_id.empty())
            printer_id = "local";
        BambuSmartPrint::PrinterLearningStore::instance().apply_learning_to_config(printer_id, working);
    }

    if (!BambuSmartPrint::ConfigSnapshot::diff(pending.record.config_snapshot, pending.fixes.config_delta).empty()) {
        apply_config_with_workflow(plater, pending.record.config_snapshot, pending.fixes.config_delta,
                                   false, &pending.fixes.changes);
        m_last_applied = pending.fixes.config_delta;
    } else if (learning_auto_apply()
               && !BambuSmartPrint::ConfigSnapshot::diff(pending.record.config_snapshot, working).empty()) {
        apply_config_with_workflow(plater, pending.record.config_snapshot, working, false, nullptr);
        m_last_applied = working;
    }

    run_one_click_print(plater);
}

void BambuSmartPrintService::auto_prepare_safe_on_load(Plater* plater)
{
    if (!is_enabled() || !plater || !wxGetApp().preset_bundle)
        return;
    if (!SlicePilot::is_active_printer_bbl(*wxGetApp().preset_bundle))
        return;
    if (plater->model().objects.empty())
        return;

    try {
        PreparedPlateWorkflow prep;
        if (!prepare_plate_workflow(plater, wxGetApp().preset_bundle, prep))
            return;

        m_last_mesh_analysis = prep.mesh;
        m_last_baseline      = prep.base;
        m_last_readiness     = readiness_from_prep(prep,
            m_last_slice_analysis.valid ? &m_last_slice_analysis : nullptr);

        if (!prep.mesh.fits_bed)
            FirstPrintExperience::apply_bed_fit_fix(plater);

        if (prep.change_count == 0) {
            refresh_all_panels();
            return;
        }

        BambuSmartPrint::SafeModeResult safe =
            BambuSmartPrint::SafeModeGuard::apply(prep.base, prep.proposed);
        const size_t safe_changes = BambuSmartPrint::ConfigSnapshot::diff(prep.base, safe.config).size();
        if (safe_changes > 0)
            apply_config_with_workflow(plater, prep.base, safe.config, false, &prep.auto_result.changes);

        m_last_applied = safe.config;
        update_plate_assessment_data(plater);
        refresh_all_panels();
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint auto_prepare_safe_on_load: " << ex.what();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint auto_prepare_safe_on_load: unknown error";
    }
}

void BambuSmartPrintService::run_one_click_print(Plater* plater)
{
    wxWindow* parent = smart_print_dialog_parent(plater ? static_cast<wxWindow*>(plater) : nullptr);
    if (!plater) {
        show_error(parent, _L("No project is open."));
        return;
    }

    const PrintGateResult gate = PrintReadinessGate::run(plater, parent);
    if (gate == PrintGateResult::Cancelled)
        return;

    if (m_workflow_running || m_one_click_active) {
        show_error(parent, _L("Smart Print is already running. Wait for the current step to finish."));
        return;
    }
    if (plater->is_background_process_slicing()) {
        show_error(parent, _L("Wait for the current slice or export to finish before starting Smart Print."));
        return;
    }

    try {
        m_one_click_active           = true;
        m_one_click_pending_send     = (gate == PrintGateResult::Proceed);
        m_silent_workflow            = true;
        m_pending_smart_slice_followup = false;
        m_post_slice_apply_rounds    = 0;
        set_one_click_phase(OneClickPhase::Analyzing);

        PreparedPlateWorkflow prep;
        if (!prepare_plate_workflow(plater, wxGetApp().preset_bundle, prep)) {
            m_one_click_active = false;
            m_one_click_pending_send = false;
            m_silent_workflow = false;
            set_one_click_phase(OneClickPhase::None);
            show_error(parent, _L("Could not analyze the current plate."));
            return;
        }

        m_last_mesh_analysis = prep.mesh;
        m_last_baseline      = prep.base;
        m_last_readiness     = readiness_from_prep(prep,
            m_last_slice_analysis.valid ? &m_last_slice_analysis : nullptr);
        refresh_all_panels();

        if (m_last_readiness.tier == BambuSmartPrint::ReadinessTier::Risky) {
            const bool first_print_user = FirstPrintExperience::local_successful_prints() == 0;
            if (first_print_user) {
                set_one_click_phase(OneClickPhase::Applying);
                if (!prep.filament_name.empty() && wxGetApp().preset_bundle) {
                    PresetBundle* bundle = wxGetApp().preset_bundle;
                    if (bundle->filaments.find_preset(prep.filament_name) != nullptr) {
                        bundle->filaments.select_preset_by_name(prep.filament_name, true);
                        plater->on_filament_change(0);
                    }
                }
                apply_config_with_workflow(plater, prep.base, prep.proposed, false, &prep.auto_result.changes);
                m_last_applied = prep.proposed;
                if (plater->get_notification_manager()) {
                    plater->get_notification_manager()->push_notification(
                        NotificationType::CustomNotification,
                        NotificationManager::NotificationLevel::RegularNotificationLevel,
                        std::string(_L("Smart Print applied safe settings for your first print.").utf8_str()));
                }
            } else {
            m_silent_workflow = false;
            SmartPrintWorkflowContent content = workflow_content_from(prep, m_last_readiness);
            const bool approved = show_workflow_dialog(plater, content, prep.base, prep.proposed,
                std::string(SLIC3R_APP_FULL_NAME) + " — review before Print", prep.filament_name,
                &prep.auto_result.changes);
            if (!approved) {
                m_one_click_active = false;
                m_one_click_pending_send = false;
                m_silent_workflow = false;
                set_one_click_phase(OneClickPhase::None);
                return;
            }
            m_silent_workflow = true;
            if (!prepare_plate_workflow(plater, wxGetApp().preset_bundle, prep)) {
                m_one_click_active = false;
                m_one_click_pending_send = false;
                m_silent_workflow = false;
                set_one_click_phase(OneClickPhase::None);
                show_error(parent, _L("Could not refresh plate analysis after applying settings."));
                return;
            }
            m_last_applied = prep.proposed;
            }
        }

        if (prep.change_count > 0 && m_last_readiness.tier != BambuSmartPrint::ReadinessTier::Risky) {
            set_one_click_phase(OneClickPhase::Applying);
            if (!prep.filament_name.empty() && wxGetApp().preset_bundle) {
                PresetBundle* bundle = wxGetApp().preset_bundle;
                if (bundle->filaments.find_preset(prep.filament_name) != nullptr) {
                    bundle->filaments.select_preset_by_name(prep.filament_name, true);
                    plater->on_filament_change(0);
                }
            }
            apply_config_with_workflow(plater, prep.base, prep.proposed, false, &prep.auto_result.changes);
            m_last_applied = prep.proposed;
        }

        set_one_click_phase(OneClickPhase::Slicing);
        plater->reslice();
    } catch (const std::exception& ex) {
        m_one_click_active = false;
        m_one_click_pending_send = false;
        m_silent_workflow = false;
        set_one_click_phase(OneClickPhase::None);
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint one-click: " << ex.what();
        show_error(parent,
            wxString::Format(_L("Smart Print could not finish one-click Print:\n\n%s"), wxString::FromUTF8(ex.what())));
    } catch (...) {
        m_one_click_active = false;
        m_one_click_pending_send = false;
        m_silent_workflow = false;
        set_one_click_phase(OneClickPhase::None);
        show_error(parent, _L("Smart Print could not finish one-click Print due to an unexpected error."));
    }
}

void BambuSmartPrintService::run_smart_slice(Plater* plater)
{
    if (!plater || m_workflow_running) return;
    if (!is_enabled()) {
        prompt_enable_smart_print(plater);
        return;
    }
    if (!is_bbl_printer_active()) {
        wxMessageDialog dlg(plater, bbl_printer_required_message(), _L("Bambu Lab printer required"),
                          wxYES_NO | wxICON_WARNING);
        dlg.SetYesNoLabels(_L("Switch to Bambu profile"), _L("Cancel"));
        if (dlg.ShowModal() == wxID_YES)
            try_activate_bbl_printer_profile(plater);
        if (!is_bbl_printer_active())
            return;
    }
    if (plater->model().objects.empty()) {
        show_error(plater, _L("Load a model on the plate before using Smart slice."));
        return;
    }

    wxWindow* parent = smart_print_dialog_parent(plater);
    try {
        m_workflow_running = true;
        m_post_slice_apply_rounds = 0;
        struct Guard {
            bool& f;
            BambuSmartPrintService* svc;
            ~Guard()
            {
                f = false;
                if (svc)
                    svc->flush_pending_failure_dialog();
            }
        } guard{ m_workflow_running, this };

        PreparedPlateWorkflow prep;
        if (!prepare_plate_workflow(plater, wxGetApp().preset_bundle, prep))
            return;

        m_last_mesh_analysis = prep.mesh;
        m_last_baseline      = prep.base;
        m_last_readiness     = readiness_from_prep(prep,
            m_last_slice_analysis.valid ? &m_last_slice_analysis : nullptr);

        if (prep.change_count == 0) {
            if (SlicePilotUi::show_timed_message_box(parent,
                    _L("No setting changes are recommended. Slice with current settings?"),
                    wxString::Format(_L("%s — Smart slice"), SLIC3R_APP_FULL_NAME),
                    wxYES_NO | wxICON_QUESTION, wxYES) == wxYES)
                plater->reslice();
            return;
        }

        SmartPrintWorkflowContent content = workflow_content_from(prep, m_last_readiness);
        if (show_workflow_dialog(plater, content, prep.base, prep.proposed,
                                 std::string(SLIC3R_APP_FULL_NAME) + " — smart slice", prep.filament_name,
                                 &prep.auto_result.changes))
            m_pending_smart_slice_followup = true;
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint smart slice: " << ex.what();
        show_error(parent,
            wxString::Format(_L("Smart slice failed:\n\n%s"), wxString::FromUTF8(ex.what())));
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint smart slice: unknown error";
        show_error(parent, _L("Smart slice failed due to an unexpected error."));
    }
}

void BambuSmartPrintService::show_history_dialog(wxWindow* parent)
{
    wxWindow* dlg_parent = smart_print_dialog_parent(parent);
    if (!dlg_parent) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint history: no parent window";
        return;
    }
    try {
        BambuSmartPrintHistoryDialog dlg(dlg_parent);
        dlg.ShowModal();
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint history dialog: " << ex.what();
        show_error(dlg_parent,
            wxString::Format(_L("Could not open print history:\n\n%s"), wxString::FromUTF8(ex.what())));
    }
}

void BambuSmartPrintService::show_privacy_dialog(wxWindow* parent)
{
    wxWindow* dlg_parent = smart_print_dialog_parent(parent);
    if (!dlg_parent) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint privacy: no parent window";
        return;
    }
    try {
        BambuSmartPrintPrivacyDialog dlg(dlg_parent);
        dlg.ShowModal();
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint privacy dialog: " << ex.what();
        show_error(dlg_parent,
            wxString::Format(_L("Could not open privacy dialog:\n\n%s"), wxString::FromUTF8(ex.what())));
    }
}

bool BambuSmartPrintService::is_bambu_account_logged_in() const
{
    NetworkAgent* agent = wxGetApp().getAgent();
    return agent && agent->is_user_login(BBL_CLOUD_PROVIDER);
}

wxString BambuSmartPrintService::bambu_account_status_text() const
{
    if (wxGetApp().app_config && wxGetApp().app_config->get_stealth_mode())
        return _L("Cloud login disabled (stealth mode).");

    if (wxGetApp().app_config && !wxGetApp().app_config->has_cloud_provider(BBL_CLOUD_PROVIDER))
        return _L("Bambu Lab cloud is off — enable it under Preferences → Online.");

    if (wxGetApp().app_config && !wxGetApp().app_config->get_bool("installed_networking"))
        return _L("Bambu Network plug-in not installed.");

    if (!is_bambu_account_logged_in())
        return _L("Not signed in to Bambu Lab.");

    NetworkAgent* agent = wxGetApp().getAgent();
    std::string display = agent->get_user_nickname(BBL_CLOUD_PROVIDER);
    if (display.empty())
        display = agent->get_user_name(BBL_CLOUD_PROVIDER);
    if (display.empty())
        return _L("Signed in to Bambu Lab.");
    return wxString::Format(_L("Signed in as %s"), wxString::FromUTF8(display));
}

void BambuSmartPrintService::prompt_bambu_login(wxWindow* parent)
{
    wxWindow* dlg_parent = parent ? parent : wxGetApp().plater();

    if (wxGetApp().app_config && wxGetApp().app_config->get_stealth_mode()) {
        show_error(dlg_parent,
            _L("Stealth mode is enabled. Disable it in Preferences to use Bambu Lab cloud login."));
        return;
    }
    if (wxGetApp().app_config && !wxGetApp().app_config->has_cloud_provider(BBL_CLOUD_PROVIDER)) {
        show_error(dlg_parent,
            _L("Turn on “Enable Bambu Cloud” under Preferences → Online, then sign in again."));
        return;
    }
    if (!wxGetApp().getAgent()) {
        show_error(dlg_parent, _L("Network services are not available."));
        return;
    }
    if (!wxGetApp().app_config || !wxGetApp().app_config->get_bool("installed_networking")) {
        wxGetApp().ShowDownNetPluginDlg();
        return;
    }

    BOOST_LOG_TRIVIAL(info) << "BambuSmartPrint: opening Bambu Lab login";
    wxGetApp().ShowUserLogin(true, BBL_CLOUD_PROVIDER);
    if (is_bambu_account_logged_in()) {
        BOOST_LOG_TRIVIAL(info) << "BambuSmartPrint: Bambu Lab login succeeded, refreshing devices";
        wxGetApp().request_user_handle(1, BBL_CLOUD_PROVIDER);
        refresh_all_panels();
    }
}

void BambuSmartPrintService::prompt_bambu_logout()
{
    if (!is_bambu_account_logged_in())
        return;
    BOOST_LOG_TRIVIAL(info) << "BambuSmartPrint: Bambu Lab logout";
    wxGetApp().request_user_logout(BBL_CLOUD_PROVIDER);
}

void BambuSmartPrintService::run_auto_workflow(Plater* plater)
{
    if (!plater || m_workflow_running) return;
    if (!wxGetApp().preset_bundle || !SlicePilot::is_active_printer_bbl(*wxGetApp().preset_bundle)) return;
    if (plater->model().objects.empty()) return;

    wxWindow* parent = smart_print_dialog_parent(plater);
    try {
        m_workflow_running = true;
        struct Guard {
            bool& f;
            BambuSmartPrintService* svc;
            ~Guard()
            {
                f = false;
                if (svc)
                    svc->flush_pending_failure_dialog();
            }
        } guard{ m_workflow_running, this };

        PreparedPlateWorkflow prep;
        if (!prepare_plate_workflow(plater, wxGetApp().preset_bundle, prep))
            return;

        m_last_mesh_analysis = prep.mesh;
        m_last_baseline      = prep.base;
        m_last_readiness     = readiness_from_prep(prep,
            m_last_slice_analysis.valid ? &m_last_slice_analysis : nullptr);

        SmartPrintWorkflowContent content = workflow_content_from(prep, m_last_readiness);
        PartPlate* plate = plater->get_partplate_list().get_curr_plate();
        if (plate && !plate->is_slice_result_ready_for_print()) {
            content.risk_factors.insert(content.risk_factors.begin(),
                std::string(_L("Slice this plate for slice-aware analysis (unsupported islands, bridges).").utf8_str()));
        }

        show_workflow_dialog(plater, content, prep.base, prep.proposed,
                             std::string(SLIC3R_APP_FULL_NAME) + " — auto settings", prep.filament_name,
                             &prep.auto_result.changes);
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint auto workflow: " << ex.what();
        show_error(parent,
            wxString::Format(_L("Smart Print workflow failed:\n\n%s"), wxString::FromUTF8(ex.what())));
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint auto workflow: unknown error";
        show_error(parent, _L("Smart Print workflow failed due to an unexpected error."));
    }
}

void BambuSmartPrintService::schedule_auto_workflow_after_load(Plater* plater)
{
    if (!is_enabled() || !plater)
        return;
    if (auto_load_mode() == AutoLoadMode::Off)
        return;
    if (!wxGetApp().preset_bundle || !SlicePilot::is_active_printer_bbl(*wxGetApp().preset_bundle))
        return;

    wxGetApp().CallAfter([]() {
        Plater* plater = wxGetApp().plater();
        if (!plater || plater->model().objects.empty())
            return;
        try {
            BambuSmartPrintService::instance().on_models_loaded(plater);
        } catch (const std::exception& ex) {
            BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint on_models_loaded: " << ex.what();
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "BambuSmartPrint on_models_loaded: unknown error";
        }
    });
}

void BambuSmartPrintService::on_models_loaded(Plater* plater)
{
    if (!is_enabled() || !plater) return;
    if (!wxGetApp().preset_bundle || !SlicePilot::is_active_printer_bbl(*wxGetApp().preset_bundle)) return;
    if (plater->model().objects.empty()) return;

    BambuSmartPrint::MeshAnalysisCache::instance().clear();
    m_last_slice_analysis = BambuSmartPrint::SliceAnalysis{};

    auto_prepare_safe_on_load(plater);

    const AutoLoadMode mode = auto_load_mode();
    if (mode == AutoLoadMode::FullDialog)
        run_auto_workflow(plater);
    else if (mode == AutoLoadMode::Notify)
        notify_model_load_summary(plater);
}

void BambuSmartPrintService::notify_model_load_summary(Plater* plater)
{
    if (!plater || !wxGetApp().preset_bundle)
        return;

    PreparedPlateWorkflow prep;
    if (!prepare_plate_workflow(plater, wxGetApp().preset_bundle, prep))
        return;

    m_last_mesh_analysis = prep.mesh;
    m_last_baseline      = prep.base;
    m_last_readiness     = readiness_from_prep(prep,
        m_last_slice_analysis.valid ? &m_last_slice_analysis : nullptr);
    refresh_all_panels();

    const int rate = int(std::round(prep.prediction.success_rate));
    wxString msg = wxString::Format(
        _L("Smart Print: %d%% estimated ready — %zu suggested change(s). Open Smart Print for details."),
        rate, prep.change_count);

    if (NotificationManager* nm = plater->get_notification_manager()) {
        nm->push_notification(NotificationType::CustomNotification,
            NotificationManager::NotificationLevel::RegularNotificationLevel,
            std::string(msg.utf8_str()),
            std::string(_L("Open Smart Print").utf8_str()),
            [](wxEvtHandler*) -> bool {
                wxGetApp().open_smart_print();
                return true;
            });
    }
}

void BambuSmartPrintService::open_full_workflow_for_current_plate(Plater* plater)
{
    if (plater)
        run_auto_workflow(plater);
}

void BambuSmartPrintService::capture_active_print_config(MachineObject* obj)
{
    m_active_print_config = capture_current_plate_config();
    m_has_active_print_config = true;
    m_active_print_config_verified = true;
    m_active_print_config_hash = BambuSmartPrint::ConfigSnapshot::fingerprint(m_active_print_config);
    m_active_print_plate_index = -1;
    if (Plater* plater = wxGetApp().plater()) {
        if (PartPlate* plate = plater->get_partplate_list().get_curr_plate())
            m_active_print_plate_index = int(plate->get_index());
    }
    if (obj) {
        m_active_print_printer_id = obj->get_dev_id();
        m_active_print_job_id       = obj->job_id_;
    } else {
        m_active_print_printer_id.clear();
        m_active_print_job_id.clear();
    }
}

void BambuSmartPrintService::clear_active_print_config()
{
    m_has_active_print_config = false;
    m_active_print_config     = DynamicPrintConfig();
    m_active_print_printer_id.clear();
    m_active_print_job_id.clear();
    m_active_print_config_hash.clear();
    m_active_print_plate_index = -1;
    m_active_print_config_verified = false;
}

DynamicPrintConfig BambuSmartPrintService::config_for_print_record(MachineObject* obj) const
{
    if (m_has_active_print_config && obj && obj->get_dev_id() == m_active_print_printer_id) {
        if (m_active_print_job_id.empty() || obj->job_id_.empty() || obj->job_id_ == m_active_print_job_id)
            return m_active_print_config;
    }
    return capture_current_plate_config();
}

void BambuSmartPrintService::flush_pending_failure_dialog()
{
    if (m_pending_failures.empty() || m_workflow_running)
        return;
    Plater* plater = wxGetApp().plater();
    if (!plater)
        return;

    m_workflow_running = true;
    struct Guard { bool& f; ~Guard() { f = false; } } guard{ m_workflow_running };

    const PendingFailureDialog pending = std::move(m_pending_failures.front());
    m_pending_failures.pop_front();
    SmartPrintWorkflowContent content = failure_workflow_content(pending.record, pending.fixes);
    show_workflow_dialog(plater, content, pending.record.config_snapshot, pending.fixes.config_delta,
                         std::string(SLIC3R_APP_FULL_NAME) + " — failure fixes", "", &pending.fixes.changes);

    if (!m_pending_failures.empty())
        wxGetApp().CallAfter([this]() { flush_pending_failure_dialog(); });
}

void BambuSmartPrintService::enqueue_failure_workflow(MachineObject* obj)
{
    if (!obj || !should_handle(*obj)) return;

    BambuSmartPrint::PrintFailureRecord record;
    record.timestamp_utc_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    static std::atomic<uint32_t> s_failure_seq{ 0 };
    record.record_id = std::to_string(record.timestamp_utc_ms) + "-"
        + std::to_string(s_failure_seq.fetch_add(1, std::memory_order_relaxed) + 1);
    record.printer_id     = obj->get_dev_id();
    record.printer_name   = obj->get_dev_name();
    record.printer_model  = obj->printer_type;
    record.gcode_file     = obj->m_gcode_file;
    record.subtask_name   = obj->subtask_name;
    record.job_id         = obj->job_id_;
    record.mc_print_error_code = obj->mc_print_error_code;
    record.print_error  = obj->print_error;
    record.mc_print_stage = obj->mc_print_stage;
    record.mc_print_percent = obj->mc_print_percent;
    record.print_status = "FAILED";

    if (obj->GetBed())
        record.bed_temp = obj->GetBed()->GetBedTemp();
    if (auto* ext_sys = obj->GetExtderSystem())
        record.nozzle_temp = float(ext_sys->GetNozzleTempCurrent(MAIN_EXTRUDER_ID));
    record.chamber_temp = obj->chamber_temp;

    if (obj->GetHMS()) {
        for (const DevHMSItem& item : obj->GetHMS()->GetHMSItems())
            record.hms_codes.push_back(item.get_long_error_code());
    }

    record.config_snapshot = config_for_print_record(obj);
    record.config_snapshot_hash = BambuSmartPrint::ConfigSnapshot::fingerprint(record.config_snapshot);
    record.config_snapshot_verified = m_active_print_config_verified
        && obj->get_dev_id() == m_active_print_printer_id
        && (m_active_print_job_id.empty() || obj->job_id_ == m_active_print_job_id);
    record.plate_index = m_active_print_plate_index;

    record.diagnosis = BambuSmartPrint::FailureAnalyzer::analyze_record(record);
    if (!record.config_snapshot_verified) {
        record.diagnosis.likely_causes.insert(record.diagnosis.likely_causes.begin(),
            "Plate settings captured at print start may differ from failure-time snapshot");
    }

    if (obj) {
        nlohmann::json extra;
        extra["dev_ip"] = obj->get_dev_ip();
        extra["connection_type"] = obj->connection_type();
        extra["lan_mode"] = obj->is_lan_mode_printer();
        extra["print_status"] = obj->print_status;
        const auto bundle = BambuSmartPrint::FailureLogBundle::capture(record, &extra);
        if (bundle.success)
            record.failure_log_bundle_dir = bundle.bundle_dir;
    }

    BambuSmartPrint::FailureDatabase::instance().append(record);
    check_smart_print_persistence();
    BambuSmartPrint::PrinterLearningStore::instance().record_failure(
        record.printer_id, record.diagnosis.category, record.config_snapshot, learning_auto_apply());
    check_smart_print_persistence();

    const BambuSmartPrint::PrinterLearningProfile learning =
        BambuSmartPrint::PrinterLearningStore::instance().get_profile(record.printer_id);
    BambuSmartPrint::AutoSettingsResult fixes = BambuSmartPrint::SettingsOptimizer::optimize_from_diagnosis(
        record.config_snapshot, record.diagnosis, &learning, !record.config_snapshot_verified);

    PendingFailureDialog pending;
    pending.record = std::move(record);
    pending.fixes  = std::move(fixes);
    m_pending_failures.push_back(std::move(pending));
    SlicePilotOnboardingFunnel::record_first_failure_seen();

    Plater* plater = wxGetApp().plater();
    if (m_workflow_running) {
        if (plater)
            show_pending_failure_notification(plater);
        wxGetApp().CallAfter([this]() { flush_pending_failure_dialog(); });
        return;
    }

    if (!plater) {
        wxGetApp().CallAfter([this]() { flush_pending_failure_dialog(); });
        return;
    }

    flush_pending_failure_dialog();
}

void BambuSmartPrintService::handle_print_failed(MachineObject* obj)
{
    enqueue_failure_workflow(obj);
}

void BambuSmartPrintService::handle_print_cancelled(MachineObject* obj)
{
    if (!obj || !should_handle(*obj)) return;

    const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (!m_last_finish_printer_id.empty() && obj->get_dev_id() == m_last_finish_printer_id
        && now_ms - m_last_finish_ms < 120000)
        return;
    if (obj->mc_print_percent >= 95)
        return;

    BambuSmartPrint::PrintFailureRecord record;
    record.timestamp_utc_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    static std::atomic<uint32_t> s_cancel_seq{ 0 };
    record.record_id = std::to_string(record.timestamp_utc_ms) + "-c"
        + std::to_string(s_cancel_seq.fetch_add(1, std::memory_order_relaxed) + 1);
    record.printer_id    = obj->get_dev_id();
    record.printer_name  = obj->get_dev_name();
    record.printer_model = obj->printer_type;
    record.gcode_file    = obj->m_gcode_file;
    record.subtask_name  = obj->subtask_name;
    record.job_id        = obj->job_id_;
    record.mc_print_percent = obj->mc_print_percent;
    record.print_status  = "CANCELLED";

    record.config_snapshot = config_for_print_record(obj);

    record.diagnosis.category     = BambuSmartPrint::FailureCategory::UserCancelled;
    record.diagnosis.title        = "Print cancelled";
    record.diagnosis.description  = "The print was stopped before completion. No learning adjustments were applied.";
    record.diagnosis.confidence   = 1.f;

    BambuSmartPrint::FailureDatabase::instance().append(record);
    check_smart_print_persistence();
}

void BambuSmartPrintService::handle_print_success(MachineObject* obj)
{
    if (!obj || !should_handle(*obj)) return;

    DynamicPrintConfig cfg = config_for_print_record(obj);
    clear_active_print_config();

    BambuSmartPrint::FailureDatabase::instance().append_success(
        obj->get_dev_id(), obj->m_gcode_file, cfg, obj->get_dev_name(), obj->subtask_name);
    check_smart_print_persistence();
    BambuSmartPrint::PrinterLearningStore::instance().record_success(obj->get_dev_id());
    check_smart_print_persistence();

    m_last_finish_printer_id = obj->get_dev_id();
    m_last_finish_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    FirstPrintExperience::record_successful_print();
    SlicePilotOnboardingFunnel::record_first_send();
    SlicePilotSetupHub::refresh_all(wxGetApp().plater());
    refresh_all_panels();

    Plater* plater = wxGetApp().plater();
    if (plater && plater->get_notification_manager()) {
        wxString name = obj->get_dev_name().empty() ? wxString::FromUTF8(obj->get_dev_id())
                                                    : wxString::FromUTF8(obj->get_dev_name());
        plater->get_notification_manager()->push_notification(
            NotificationType::CustomNotification,
            NotificationManager::NotificationLevel::RegularNotificationLevel,
            std::string(wxString::Format(
                _L("Print finished successfully on %s — recorded for Smart Print learning."), name).utf8_str()));
    }
}

void BambuSmartPrintService::on_print_state_changed(MachineObject* obj,
                                                    const std::string& old_state,
                                                    const std::string& new_state)
{
    if (!obj || old_state == new_state) return;

    if (is_running_print_state(new_state) && !is_running_print_state(old_state) && !is_active_print_state(old_state))
        capture_active_print_config(obj);

    if (new_state == "FAILED" && old_state != "FAILED") {
        handle_print_failed(obj);
        clear_active_print_config();
    } else if (new_state == "FINISH" && old_state != "FINISH") {
        handle_print_success(obj);
    } else if (is_cancel_terminal_state(new_state) && !is_cancel_terminal_state(old_state)) {
        handle_print_cancelled(obj);
        clear_active_print_config();
    } else if (new_state == "IDLE" && is_running_print_state(old_state)
             && obj->mc_print_percent > 0 && obj->mc_print_percent < 100) {
        handle_print_cancelled(obj);
        clear_active_print_config();
    }
}

void BambuSmartPrintService::on_slice_completed(Plater* plater, const Print* print, bool success)
{
    if (!plater || !print) {
        if (!success && m_one_click_active) {
            m_one_click_active = false;
            m_one_click_pending_send = false;
            m_silent_workflow = false;
            set_one_click_phase(OneClickPhase::None);
            refresh_all_panels();
        }
        return;
    }
    if (!is_enabled()) {
        if (!success && m_one_click_active) {
            m_one_click_active = false;
            m_one_click_pending_send = false;
            m_silent_workflow = false;
            set_one_click_phase(OneClickPhase::None);
            refresh_all_panels();
        }
        return;
    }
    if (!success) {
        if (m_one_click_active) {
            m_one_click_active = false;
            m_one_click_pending_send = false;
            m_silent_workflow = false;
            set_one_click_phase(OneClickPhase::None);
            show_error(plater, _L("Smart Print: slicing failed after applying suggested settings. Check the notifications panel for details."));
            refresh_all_panels();
        }
        return;
    }
    SlicePilotOnboardingFunnel::record_first_slice();
    if (!wxGetApp().preset_bundle || !SlicePilot::is_active_printer_bbl(*wxGetApp().preset_bundle)) return;

    if (m_one_click_pending_send) {
        m_one_click_pending_send = false;
        m_one_click_active       = false;
        m_silent_workflow        = false;
        set_one_click_phase(OneClickPhase::Exporting);
        const int plate_index = plater->get_partplate_list().get_curr_plate_index();
        wxGetApp().CallAfter([this, plater, plate_index]() {
            if (!plater)
                return;
            plater->send_print_job_for_plate(plate_index, true);
            BambuSmartPrintService::instance().set_one_click_phase(OneClickPhase::None);
            BambuSmartPrintService::instance().refresh_all_panels();
        });
        refresh_all_panels();
        return;
    }

    if (m_one_click_active) {
        m_one_click_active = false;
        m_silent_workflow  = false;
        set_one_click_phase(OneClickPhase::None);
        show_info(plater,
            _L("Smart Print finished slicing. G-code is ready on this plate.\n\n"
               "Install the Bambu Network plug-in and bind a printer to send jobs."),
            _L("Smart Print"));
        refresh_all_panels();
    }

    DynamicPrintConfig cfg = wxGetApp().preset_bundle->full_config();
    PartPlate* plate = plater->get_partplate_list().get_curr_plate();
    if (plate)
        cfg.apply(*plate->config());

    m_last_slice_analysis = BambuSmartPrint::SliceGeometryAnalyzer::analyze(*print, cfg);

    const PrintStatistics& ps = print->print_statistics();
    m_last_estimated_print_time = ps.estimated_normal_print_time;
    m_last_estimated_filament_g = ps.total_weight;

    if (m_last_slice_analysis.valid)
        refresh_post_slice_assessment(plater);
    else
        refresh_all_panels();

    if (m_pending_smart_slice_followup) {
        m_pending_smart_slice_followup = false;
        wxGetApp().CallAfter([this, plater]() { maybe_notify_slice_analysis(plater); });
    }
}

void BambuSmartPrintService::maybe_notify_slice_analysis(Plater* plater)
{
    if (m_one_click_active)
        return;
    if (!m_last_slice_analysis.valid || !plater || !wxGetApp().preset_bundle)
        return;

    PreparedPlateWorkflow prep;
    if (!prepare_plate_workflow(plater, wxGetApp().preset_bundle, prep))
        return;

    m_last_readiness = readiness_from_prep(prep, &m_last_slice_analysis);
    SmartPrintWorkflowContent content = workflow_content_from(prep, m_last_readiness);
    content.is_smart_slice_result = true;
    content.show_success_gauge    = false;

    auto slice_risk_utf8 = [](const wxString& s) { return std::string(s.utf8_str()); };
    std::vector<std::string> slice_risks;
    slice_risks.push_back(slice_risk_utf8(wxString::Format(_L("Unsupported slice area: %.0f%%"),
        m_last_slice_analysis.overhang_area_ratio * 100.f)));
    if (m_last_slice_analysis.unsupported_islands_count > 0)
        slice_risks.push_back(slice_risk_utf8(wxString::Format(
            _L("Unsupported islands: %d"), m_last_slice_analysis.unsupported_islands_count)));
    if (m_last_slice_analysis.bridge_length_max_mm > 0.f)
        slice_risks.push_back(slice_risk_utf8(wxString::Format(
            _L("Longest bridge: %.1f mm"), m_last_slice_analysis.bridge_length_max_mm)));
    for (const std::string& note : m_last_slice_analysis.risk_notes) {
        if (!note.empty())
            slice_risks.push_back(note);
    }
    content.risk_factors.insert(content.risk_factors.begin(), slice_risks.begin(), slice_risks.end());

    if (content.change_count > 0) {
        content.summary = wxString(_L("Smart slice finished. Slice analysis suggests additional changes — "
                                      "preview and apply to re-slice, or dismiss to keep current settings."))
                              .utf8_string();
    } else {
        content.summary = wxString(_L("Smart slice finished. No further setting changes from slice analysis; "
                                      "review geometry risks below."))
                              .utf8_string();
    }

    bool applied = false;
    if (m_silent_workflow && prep.change_count > 0) {
        applied = apply_config_with_workflow(plater, prep.base, prep.proposed, true, &prep.auto_result.changes);
        m_last_applied     = prep.proposed;
        m_last_auto_result = prep.auto_result;
        m_last_prediction  = prep.prediction;
        m_last_readiness   = readiness_from_prep(prep, &m_last_slice_analysis);
        refresh_all_panels();
    } else {
        applied = show_workflow_dialog(plater, content, prep.base, prep.proposed,
            std::string(SLIC3R_APP_FULL_NAME) + " — smart slice results", prep.filament_name,
            &prep.auto_result.changes);
    }

    if (!applied) {
        m_last_mesh_analysis = prep.mesh;
        m_last_auto_result   = prep.auto_result;
        m_last_prediction    = prep.prediction;
        refresh_all_panels();
    }

    if (applied && content.change_count > 0 && m_post_slice_apply_rounds < 1) {
        ++m_post_slice_apply_rounds;
        m_pending_smart_slice_followup = true;
    }
}

}} // namespace

#include "AssistantActionWorkflow.hpp"
#include "AiCatalog.hpp"

#include "AssistantActionExecutor.hpp"
#include "AssistantExecutionPolicy.hpp"
#include "AssistantSettingDescriptions.hpp"
#include "AssistantTelemetry.hpp"

#include "../BambuSmartPrint/BambuSmartPrintService.hpp"
#include "../BambuSmartPrint/BambuSmartPrintUi.hpp"
#include "../BambuSmartPrint/BambuSmartPrintWorkflowDialog.hpp"
#include "AssistantToolRegistry.hpp"
#include "../AICoach/AIGuiOrchestrator.hpp"
#include "../I18N.hpp"
#include "../GUI_App.hpp"
#include "../Plater.hpp"

#include "libslic3r/BambuSmartPrint/ConfigSnapshot.hpp"
#include "libslic3r/BambuSmartPrint/ConfigVersionStack.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/libslic3r.h"

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <sstream>

namespace Slic3r { namespace GUI {

class Plater;

namespace {

wxWindow* dialog_parent(wxWindow* preferred)
{
    if (preferred)
        return preferred;
    return wxGetApp().GetTopWindow();
}

DynamicPrintConfig capture_current_config()
{
    if (auto* bundle = wxGetApp().preset_bundle)
        return bundle->full_config(false);
    return {};
}

static bool ui_prefers_korean()
{
    const wxString code = wxGetApp().current_language_code();
    wxString       lang = code.BeforeFirst('_').BeforeFirst('-').Lower();
    if (lang == wxString("ko"))
        return true;
    lang = wxGetApp().current_language_code_safe().BeforeFirst('_').Lower();
    return lang == wxString("ko");
}

static bool is_readonly_action(const std::string& type)
{
    return AssistantToolRegistry::category_for(type) == AssistantToolCategory::Readonly;
}

static bool is_workflow_only_action(const std::string& type)
{
    return type == "slice" || type == "ui_select_tab" || type == "menu_item" || type == "add_model"
        || type == "makerworld_search" || type == "makerworld_find_and_print" || type == "import_makerworld"
        || type == "open_smart_print" || type == "open_setup"
        || type == "export_gcode" || type == "rollback_apply"
        || type == "get_state" || type == "list_objects" || type == "select_object"
        || type == "add_plate" || type == "delete_plate" || type == "select_plate"
        || type == "open_calibration" || type == "run_smart_print" || type == "select_preset"
        // Gated by the user's one-time printer-control consent instead of a review dialog.
        || type == "printer_control";
}

static bool is_geometry_action(const std::string& type)
{
    return type == "rotate" || type == "translate" || type == "scale" || type == "clone_selection"
        || type == "arrange" || type == "arrange_objects" || type == "split_object" || type == "delete_selection";
}

std::string describe_action(const nlohmann::json& action, bool ko)
{
    if (!action.is_object() || !action.contains("type"))
        return "unknown";
    const std::string type = action.value("type", "");
    if (type == "set_config" && action.contains("options") && action["options"].is_object()) {
        std::ostringstream oss;
        const std::string preset = action.value("preset", "print");
        oss << preset << ": ";
        bool first = true;
        for (auto it = action["options"].begin(); it != action["options"].end(); ++it) {
            if (!first)
                oss << ", ";
            first = false;
            oss << it.key() << " = ";
            if (it.value().is_string())
                oss << it.value().get<std::string>();
            else
                oss << it.value().dump();
        }
        return oss.str();
    }
    if (type == "rotate") {
        return (boost::format(ai_catalog_text("rotate selection (deg %s, %s, %s)"))
                % action.value("x", 0.0) % action.value("y", 0.0) % action.value("z", 0.0)).str();
    }
    if (type == "translate") {
        return (boost::format(ai_catalog_text("move selection (%s, %s, %s) mm"))
                % action.value("x", 0.0) % action.value("y", 0.0) % action.value("z", 0.0)).str();
    }
    if (type == "scale") {
        return (boost::format(ai_catalog_text("scale selection (factor %s)")) % action.value("factor", 1.0)).str();
    }
    if (type == "clone_selection")
        return ai_catalog_text("copy (duplicate) selection");
    if (type == "arrange")
        return ai_catalog_text("auto-arrange objects on plate");
    if (type == "arrange_objects")
        return ai_catalog_text("arrange by object");
    if (type == "split_object")
        return ai_catalog_text("split to objects");
    if (type == "add_plate")
        return ai_catalog_text("add build plate");
    if (type == "slice")
        return action.value("scope", "plate") == "all" ? ai_catalog_text("slice all plates")
                                                        : ai_catalog_text("slice current plate");
    if (type == "ui_select_tab") {
        const std::string tab = action.value("tab", "");
        if (tab == "prepare") return ai_catalog_text("switch tab: Prepare");
        if (tab == "preview") return ai_catalog_text("switch tab: Preview");
        if (tab == "monitor") return ai_catalog_text("switch tab: Device");
        if (tab == "smart_print") return ai_catalog_text("switch tab: Smart Print");
        return "switch tab: " + tab;
    }
    if (type == "menu_item")
        return action.value("menu", "") + " → " + action.value("item", "");
    if (type == "add_model")
        return (boost::format(ai_catalog_text("import model: %s")) % action.value("path", "")).str();
    return type;
}

static std::string describe_config_change(const BambuSmartPrint::SettingChange& ch, bool ko)
{
    return AssistantSettingDescriptions::preview_line(ch, ko);
}

static std::string build_summary_text(const std::string& assistant_msg,
                                      const std::vector<BambuSmartPrint::SettingChange>& config_changes,
                                      size_t change_count, bool ko)
{
    if (!config_changes.empty())
        return AssistantSettingDescriptions::build_summary(config_changes, assistant_msg, ko);

    if (!assistant_msg.empty())
        return assistant_msg;

    if (change_count > 0) {
        return (boost::format(ai_catalog_text("VerSlicer AI proposes %s setting change(s).")) % change_count).str();
    }

    return ai_catalog_text("Review VerSlicer AI suggestions.");
}

static void prepend_ai_change_insights(SmartPrintWorkflowContent& content,
                                       const std::vector<BambuSmartPrint::SettingChange>& config_changes, bool ko)
{
    if (config_changes.empty())
        return;

    BambuSmartPrint::PrintInsight header;
    header.label    = ai_catalog_text("AI adjustments");
    header.detail   = ai_catalog_text("VerSlicer AI proposes the settings below — review before applying.");
    header.severity = BambuSmartPrint::RiskSeverity::Low;
    content.insights.insert(content.insights.begin(), header);

    for (const std::string& effect : AssistantSettingDescriptions::expected_effects(config_changes, ko)) {
        BambuSmartPrint::PrintInsight ins;
        ins.label     = ai_catalog_text("Expected effect");
        ins.detail    = effect;
        ins.severity  = BambuSmartPrint::RiskSeverity::Info;
        content.insights.insert(content.insights.begin() + 1, ins);
    }
}

std::vector<BambuSmartPrint::SettingChange> diff_with_ai_reasons(
    const DynamicPrintConfig& before, const DynamicPrintConfig& after);

DynamicPrintConfig simulate_proposed_config_inner(const DynamicPrintConfig& before, const nlohmann::json& root);

SmartPrintWorkflowContent build_workflow_content(const nlohmann::json& root)
{
    SmartPrintWorkflowContent content;
    content.show_success_gauge   = true;
    content.is_failure_workflow  = false;
    content.is_smart_slice_result = false;

    const bool ko = ui_prefers_korean();

    std::string assistant_msg;
    if (root.contains("message") && root["message"].is_string())
        assistant_msg = root["message"].get<std::string>();

    const DynamicPrintConfig before_cfg = capture_current_config();
    const DynamicPrintConfig after_cfg  = simulate_proposed_config_inner(before_cfg, root);
    const std::vector<BambuSmartPrint::SettingChange> config_changes =
        diff_with_ai_reasons(before_cfg, after_cfg);

    for (const auto& ch : config_changes)
        content.change_preview.push_back(describe_config_change(ch, ko));

    if (root.contains("actions") && root["actions"].is_array()) {
        for (const auto& action : root["actions"]) {
            if (!action.is_object())
                continue;
            const std::string type = action.value("type", "");
            if (type == "slice")
                content.is_smart_slice_result = true;
            if (type == "set_config" || is_workflow_only_action(type))
                continue;
            if (is_geometry_action(type))
                content.change_preview.push_back(describe_action(action, ko));
        }
    }

    content.change_count = content.change_preview.size();
    content.summary      = build_summary_text(assistant_msg, config_changes, content.change_count, ko);

    auto& svc = BambuSmartPrintService::instance();
    const BambuSmartPrint::ReadinessReport& readiness = svc.last_readiness_report();
    const BambuSmartPrint::SuccessPrediction& prediction = svc.last_prediction();
    const BambuSmartPrint::ModelAnalysis& mesh = svc.last_mesh_analysis();

    if (!readiness.headline.empty() || readiness.score > 0.f) {
        content.readiness_headline = readiness.headline;
        content.success_rate       = readiness.score > 0.f ? readiness.score : readiness.success_rate;
        content.insights           = readiness.insights;
        content.filament_mismatch  = readiness.filament_mismatch;
        content.active_filament    = readiness.active_filament_hint;
        if (!readiness.suggested_filament_hint.empty())
            content.suggested_material = readiness.suggested_filament_hint;
    } else if (prediction.success_rate > 0.f) {
        content.success_rate       = prediction.success_rate;
        content.prediction_summary = prediction.summary;
        content.risk_factors       = prediction.risk_factors;
    } else {
        content.success_rate       = 80.f;
        content.readiness_headline = ai_catalog_text("Review AI suggestions before applying");
    }

    if (!config_changes.empty()) {
        content.readiness_headline = ai_catalog_text("Review AI adjustments");

        std::vector<BambuSmartPrint::PrintInsight> filtered_insights;
        filtered_insights.reserve(content.insights.size());
        for (const BambuSmartPrint::PrintInsight& ins : content.insights) {
            if (boost::icontains(ins.detail, "adjustments are pending"))
                continue;
            filtered_insights.push_back(ins);
        }
        content.insights = std::move(filtered_insights);

        for (const std::string& effect : AssistantSettingDescriptions::expected_effects(config_changes, ko))
            content.risk_factors.insert(content.risk_factors.begin(), effect);
        prepend_ai_change_insights(content, config_changes, ko);
    }

    if (content.suggested_material.empty() && !mesh.suggested_material.empty())
        content.suggested_material = mesh.suggested_material;
    if (mesh.complexity_score > 0)
        content.complexity_score = mesh.complexity_score;

  if (wxGetApp().preset_bundle) {
        const std::string filament = wxGetApp().preset_bundle->filaments.get_edited_preset().name;
        if (!filament.empty() && content.suggested_material.empty())
            content.suggested_material = filament;
    }

    return content;
}

std::vector<BambuSmartPrint::SettingChange> diff_with_ai_reasons(
    const DynamicPrintConfig& before, const DynamicPrintConfig& after)
{
    std::vector<BambuSmartPrint::SettingChange> changes = BambuSmartPrint::ConfigSnapshot::diff(before, after);
    for (auto& ch : changes) {
        if (ch.reason.empty())
            ch.reason = AssistantSettingDescriptions::change_reason(ch, ui_prefers_korean());
    }
    return changes;
}

DynamicPrintConfig simulate_proposed_config_inner(const DynamicPrintConfig& before, const nlohmann::json& root)
{
    DynamicPrintConfig proposed = before;
    AssistantActionExecutor::apply_set_config_actions_to_config(proposed, root);
    return proposed;
}

void push_config_version_for_assistant_apply()
{
    if (!wxGetApp().preset_bundle)
        return;
    Plater* plater = wxGetApp().plater();
    if (!plater)
        return;
    const DynamicPrintConfig before = wxGetApp().preset_bundle->full_config(false);
    const int plate_idx             = plater->get_partplate_list().get_curr_plate_index();
    BambuSmartPrint::ConfigVersionStack::instance().push("ollama_apply", "local", plate_idx, before);
}

static bool root_has_set_config(const nlohmann::json& root)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return false;
    for (const auto& a : root["actions"]) {
        if (a.is_object() && a.value("type", "") == "set_config")
            return true;
    }
    return false;
}

static bool is_geometry_or_flow_only(const nlohmann::json& root)
{
    if (!root.contains("actions") || !root["actions"].is_array() || root["actions"].empty())
        return false;
    if (root_has_set_config(root))
        return false;
    return true;
}

static bool is_dangerous_action(const std::string& type)
{
    return AssistantToolRegistry::category_for(type) == AssistantToolCategory::Dangerous;
}

static bool is_inline_chat_apply(const nlohmann::json& root)
{
    if (root_has_set_config(root))
        return false;
    if (!root.contains("actions") || !root["actions"].is_array() || root["actions"].empty())
        return false;
    for (const auto& a : root["actions"]) {
        if (!a.is_object())
            continue;
        const std::string type = a.value("type", "");
        if (is_dangerous_action(type))
            return false;
        if (!is_readonly_action(type) && !is_geometry_action(type) && !is_workflow_only_action(type))
            return false;
    }
    return true;
}

} // namespace

AssistantWorkflowRun AssistantActionWorkflow::execute_with_policy(const nlohmann::json& root, wxWindow* parent,
                                                            AssistantExecutionPolicy policy)
{
    if (!has_executable_actions(root)) {
        AssistantWorkflowRun run;
        run.results = AssistantActionExecutor::execute(root);
        return run;
    }
    if (policy == AssistantExecutionPolicy::ConfirmAlways)
        return confirm_and_execute(root, parent);
    if (is_inline_chat_apply(root))
        return execute_inline(root, parent);
    return confirm_and_execute(root, parent);
}

bool AssistantActionWorkflow::has_executable_actions(const nlohmann::json& root)
{
    return root.contains("actions") && root["actions"].is_array() && !root["actions"].empty();
}

DynamicPrintConfig AssistantActionWorkflow::simulate_proposed_config(const DynamicPrintConfig& before,
                                                                const nlohmann::json& root)
{
    return simulate_proposed_config_inner(before, root);
}

AssistantWorkflowRun AssistantActionWorkflow::execute_inline(const nlohmann::json& root, wxWindow* parent)
{
    (void) parent;
    AssistantWorkflowRun run;
    if (Plater* plater = wxGetApp().plater())
        BambuSmartPrintService::instance().update_plate_assessment_data(plater);
    if (root_has_set_config(root))
        push_config_version_for_assistant_apply();
    run.results = AssistantActionExecutor::execute(root);
    AssistantTelemetry::workflow_finished(!run.results.empty(), false, false, static_cast<int>(run.results.size()));
    return run;
}

AssistantWorkflowRun AssistantActionWorkflow::confirm_and_execute(const nlohmann::json& root, wxWindow* parent)
{
    AssistantWorkflowRun run;
    if (!has_executable_actions(root)) {
        run.results = AssistantActionExecutor::execute(root);
        return run;
    }

    if (is_inline_chat_apply(root))
        return execute_inline(root, parent);

    // Without a window to review in, refuse rather than apply unreviewed changes.
    wxWindow* dlg_parent = dialog_parent(parent);
    if (!dlg_parent) {
        run.cancelled = true;
        AssistantTelemetry::workflow_finished(false, true, false, 0);
        return run;
    }

    Plater* plater = wxGetApp().plater();
    if (plater)
        BambuSmartPrintService::instance().update_plate_assessment_data(plater);

    SmartPrintWorkflowContent content = build_workflow_content(root);
    const DynamicPrintConfig before = capture_current_config();
    const DynamicPrintConfig after  = simulate_proposed_config_inner(before, root);
    const std::vector<BambuSmartPrint::SettingChange> change_reasons = diff_with_ai_reasons(before, after);

    AIGuiOrchestrator::instance().on_chat_apply_begin();
    try {
        BambuSmartPrintWorkflowDialog dlg(dlg_parent, content);
        const int rc = SlicePilotUi::show_modal_with_auto_default(&dlg, wxID_CANCEL);
        if (rc != wxID_OK) {
            run.cancelled = true;
            AIGuiOrchestrator::instance().on_chat_apply_end(false);
            AssistantTelemetry::workflow_finished(false, true, false, 0);
            return run;
        }

        if (!dlg.preview_requested() && !dlg.apply_requested() && content.change_count > 0)
            dlg.confirm_auto_apply();

        if (dlg.preview_requested()) {
            BambuSmartPrintService::instance().show_settings_compare(
                before, after,
                std::string(SLIC3R_APP_FULL_NAME) + " — AI proposed changes",
                change_reasons.empty() ? nullptr : &change_reasons);
            run.preview_only = true;
            AIGuiOrchestrator::instance().on_chat_apply_end(false);
            AssistantTelemetry::workflow_finished(false, false, true, 0);
            return run;
        }

        if (!dlg.apply_requested()) {
            run.cancelled = true;
            AIGuiOrchestrator::instance().on_chat_apply_end(false);
            AssistantTelemetry::workflow_finished(false, true, false, 0);
            return run;
        }

        if (root_has_set_config(root))
            push_config_version_for_assistant_apply();
        run.results = AssistantActionExecutor::execute(root);
        AIGuiOrchestrator::instance().on_chat_apply_end(true, root);
        AssistantTelemetry::workflow_finished(true, false, false, static_cast<int>(run.results.size()));
        return run;
    } catch (const std::exception&) {
        run.cancelled = true;
        AIGuiOrchestrator::instance().on_chat_apply_end(false);
        AssistantTelemetry::workflow_finished(false, true, false, 0);
        throw;
    }
}

}} // namespace

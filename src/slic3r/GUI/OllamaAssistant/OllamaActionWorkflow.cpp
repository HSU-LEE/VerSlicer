#include "OllamaActionWorkflow.hpp"

#include "../BambuSmartPrint/BambuSmartPrintService.hpp"
#include "../BambuSmartPrint/BambuSmartPrintUi.hpp"
#include "../BambuSmartPrint/BambuSmartPrintWorkflowDialog.hpp"
#include "../GUI_App.hpp"

#include "libslic3r/BambuSmartPrint/ConfigSnapshot.hpp"
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

std::string describe_action(const nlohmann::json& action)
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
        return (boost::format("rotate selection (deg %1%, %2%, %3%)")
                % action.value("x", 0.0) % action.value("y", 0.0) % action.value("z", 0.0))
            .str();
    }
    if (type == "translate") {
        return (boost::format("move selection (%1%, %2%, %3%) mm")
                % action.value("x", 0.0) % action.value("y", 0.0) % action.value("z", 0.0))
            .str();
    }
    if (type == "scale") {
        return (boost::format("scale selection (factor %1%)") % action.value("factor", 1.0)).str();
    }
    if (type == "clone_selection")
        return "copy (duplicate) selection";
    if (type == "arrange")
        return "auto-arrange objects on plate";
    if (type == "slice")
        return action.value("scope", "plate") == "all" ? "slice all plates" : "slice current plate";
    if (type == "ui_select_tab")
        return "switch tab: " + action.value("tab", "");
    if (type == "menu_item")
        return action.value("menu", "") + " → " + action.value("item", "");
    if (type == "add_model")
        return "import model: " + action.value("path", "");
    return type;
}

SmartPrintWorkflowContent build_workflow_content(const nlohmann::json& root)
{
    SmartPrintWorkflowContent content;
    content.show_success_gauge   = true;
    content.is_failure_workflow  = false;
    content.is_smart_slice_result = false;

    std::string assistant_msg;
    if (root.contains("message") && root["message"].is_string())
        assistant_msg = root["message"].get<std::string>();

    if (root.contains("actions") && root["actions"].is_array()) {
        for (const auto& action : root["actions"]) {
            if (!action.is_object())
                continue;
            const std::string type = action.value("type", "");
            if (type == "slice")
                content.is_smart_slice_result = true;
            content.change_preview.push_back(describe_action(action));
        }
    }
    content.change_count = content.change_preview.size();

    std::string summary_line = (boost::format("Verslicer AI: %1% planned adjustment(s)") % content.change_count).str();
    if (!assistant_msg.empty())
        summary_line += " — " + assistant_msg;
    content.summary = summary_line;

    auto& svc = BambuSmartPrintService::instance();
    const BambuSmartPrint::ReadinessReport& readiness = svc.last_readiness_report();
    const BambuSmartPrint::SuccessPrediction& prediction = svc.last_prediction();
    const BambuSmartPrint::ModelAnalysis& mesh = svc.last_mesh_analysis();

    if (!readiness.headline.empty() || readiness.score > 0.f) {
        content.readiness_headline = readiness.headline;
        content.success_rate       = readiness.success_rate > 0.f ? readiness.success_rate : readiness.score;
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
        content.readiness_headline = "Review AI suggestions before applying";
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
            ch.reason = "AI assistant suggestion";
    }
    return changes;
}

DynamicPrintConfig simulate_proposed_config(const DynamicPrintConfig& before, const nlohmann::json& root)
{
    DynamicPrintConfig proposed = before;
    if (!root.contains("actions") || !root["actions"].is_array())
        return proposed;

    ConfigSubstitutionContext ctxt{ ForwardCompatibilitySubstitutionRule::Disable };
    for (const auto& action : root["actions"]) {
        if (!action.is_object() || action.value("type", "") != "set_config")
            continue;
        if (!action.contains("options") || !action["options"].is_object())
            continue;
        for (auto it = action["options"].begin(); it != action["options"].end(); ++it) {
            std::string key = it.key();
            if (key == "support" || key == "support_material")
                key = "enable_support";
            if (!proposed.has(key))
                continue;
            std::string val;
            if (it.value().is_string())
                val = it.value().get<std::string>();
            else if (it.value().is_boolean())
                val = it.value().get<bool>() ? "1" : "0";
            else if (it.value().is_number_integer())
                val = std::to_string(it.value().get<long long>());
            else if (it.value().is_number_float())
                val = std::to_string(it.value().get<double>());
            else
                val = it.value().dump();
            proposed.set_deserialize_nothrow(key, val, ctxt, false);
        }
    }
    return proposed;
}

} // namespace

bool OllamaActionWorkflow::has_executable_actions(const nlohmann::json& root)
{
    return root.contains("actions") && root["actions"].is_array() && !root["actions"].empty();
}

OllamaWorkflowRun OllamaActionWorkflow::confirm_and_execute(const nlohmann::json& root, wxWindow* parent)
{
    OllamaWorkflowRun run;
    if (!has_executable_actions(root)) {
        run.results = OllamaActionExecutor::execute(root);
        return run;
    }

    wxWindow* dlg_parent = dialog_parent(parent);
    if (!dlg_parent) {
        run.results = OllamaActionExecutor::execute(root);
        return run;
    }

    Plater* plater = wxGetApp().plater();
    if (plater)
        BambuSmartPrintService::instance().update_plate_assessment_data(plater);

    const SmartPrintWorkflowContent content = build_workflow_content(root);
    const DynamicPrintConfig before = capture_current_config();
    const DynamicPrintConfig after  = simulate_proposed_config(before, root);
    const std::vector<BambuSmartPrint::SettingChange> change_reasons = diff_with_ai_reasons(before, after);

    try {
        BambuSmartPrintWorkflowDialog dlg(dlg_parent, content);
        const int rc = SlicePilotUi::show_modal_with_auto_default(&dlg, wxID_OK);
        if (rc != wxID_OK) {
            run.cancelled = true;
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
            return run;
        }

        if (!dlg.apply_requested()) {
            run.cancelled = true;
            return run;
        }

        run.results = OllamaActionExecutor::execute(root);
        return run;
    } catch (...) {
        run.results = OllamaActionExecutor::execute(root);
        return run;
    }
}

}} // namespace

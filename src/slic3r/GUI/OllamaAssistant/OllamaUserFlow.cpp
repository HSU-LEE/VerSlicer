#include "OllamaUserFlow.hpp"

#include "../AICoach/BeginnerJourney.hpp"
#include "../BambuSmartPrint/BambuSmartPrintService.hpp"
#include "../BambuConnect.hpp"
#include "../BambuSmartPrint/PrintReadinessGate.hpp"
#include "../BambuSmartPrint/SlicePilotSetupHub.hpp"
#include "libslic3r/BambuSmartPrint/PrintGoalSession.hpp"
#include "../GLToolbar.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../MainFrame.hpp"
#include "../MakerWorld/MakerWorldSearchService.hpp"
#include "../Plater.hpp"
#include "OllamaActionWorkflow.hpp"
#include "OllamaActionExecutor.hpp"

namespace Slic3r { namespace GUI {

namespace {

static const char* kStepNames[] = { "printer", "plugin", "connect", "model" };

SetupHubStep first_incomplete_setup_step()
{
    for (int i = 0; i < int(SetupHubStep::Count); ++i) {
        const auto step = static_cast<SetupHubStep>(i);
        if (!SlicePilotSetupHub::step_complete(step))
            return step;
    }
    return SetupHubStep::Count;
}

std::string active_tab_id()
{
    MainFrame* mf = wxGetApp().mainframe;
    if (!mf)
        return "unknown";
    if (mf->is_tab_selected(MainFrame::tp3DEditor))
        return "prepare";
    if (mf->is_tab_selected(MainFrame::tpPreview))
        return "preview";
    if (mf->is_tab_selected(MainFrame::tpMonitor))
        return "device";
    if (mf->is_tab_selected(MainFrame::tpSmartPrint))
        return "smart_print";
    if (mf->is_tab_selected(MainFrame::tpCalibration))
        return "calibration";
    if (mf->is_tab_selected(MainFrame::tpHome))
        return "home";
    return "other";
}

OllamaFlowDispatchResult run_flow_actions(const nlohmann::json& root)
{
    OllamaFlowDispatchResult out;
    out.handled = true;
    OllamaActionWorkflow::execute_inline(root, nullptr);
    return out;
}

void refresh_smart_print_from_goal_session(Plater* plater)
{
    BambuSmartPrintService::instance().refresh_from_goal_session(plater);
}

OllamaFlowDispatchResult open_setup_flow(Plater* plater)
{
    refresh_smart_print_from_goal_session(plater);
    wxGetApp().open_smart_print();
    if (const SetupHubStep step = first_incomplete_setup_step(); step != SetupHubStep::Count)
        SlicePilotSetupHub::activate_step(step, plater);
    SlicePilotSetupHub::refresh_all(plater);

    OllamaFlowDispatchResult out;
    out.handled = true;
    return out;
}

OllamaFlowDispatchResult send_print_flow(Plater* plater)
{
    OllamaFlowDispatchResult out;
    if (!plater) {
        out.handled = true;
        return out;
    }

    BambuSmartPrintService::instance().refresh_from_goal_session(plater);

    MainFrame* mf = wxGetApp().mainframe;
    if (PrintReadinessGate::run(plater, mf) != PrintGateResult::Proceed) {
        out.handled         = true;
        out.setup_blocked   = true;
        out.blocked_message = _u8L(
            "Complete printer setup before sending — I'll open Smart Print Setup so you can connect your printer.");
        open_setup_flow(plater);
        return out;
    }

    if (BambuConnect::preferred())
        BambuConnect::send_current_plate(plater);
    else
        plater->send_print_job_for_plate(plater->get_partplate_list().get_curr_plate_index(), true);
    BeginnerJourney::on_sent_or_exported();
    out.handled = true;
    return out;
}

} // namespace

nlohmann::json OllamaUserFlow::build_flow_context_json()
{
    nlohmann::json flow = nlohmann::json::object();
    flow["active_tab"]  = active_tab_id();

    nlohmann::json steps = nlohmann::json::object();
    for (int i = 0; i < int(SetupHubStep::Count); ++i) {
        const auto step = static_cast<SetupHubStep>(i);
        steps[kStepNames[i]] = SlicePilotSetupHub::step_complete(step);
    }
    flow["setup_steps"]     = steps;
    flow["setup_completed"] = SlicePilotSetupHub::completed_count();
    flow["setup_total"]     = int(SetupHubStep::Count);

    if (const SetupHubStep next = first_incomplete_setup_step(); next != SetupHubStep::Count)
        flow["setup_next"] = kStepNames[int(next)];

    Plater* plater = wxGetApp().plater();
    if (plater) {
        try {
            flow["has_model"] = !plater->model().objects.empty();
        } catch (...) {
            flow["has_model"] = false;
        }
    }

    const auto& readiness = BambuSmartPrintService::instance().last_readiness_report();
    const auto& session   = BambuSmartPrint::PrintGoalSession::instance();
    if (session.has_last_plan()) {
        const auto& plan = session.last_plan();
        if (plan.readiness.score > 0.f)
            flow["readiness_score"] = plan.readiness.score;
        if (!plan.readiness.headline.empty())
            flow["readiness_headline"] = plan.readiness.headline;
        if (!plan.goal.user_text.empty())
            flow["print_goal"] = plan.goal.user_text;
    } else if (readiness.score > 0.f) {
        flow["readiness_score"] = readiness.score;
        if (!readiness.headline.empty())
            flow["readiness_headline"] = readiness.headline;
    }
    if (!flow.contains("print_goal") && !session.goal().user_text.empty())
        flow["print_goal"] = session.goal().user_text;

    flow["network_plugin_ready"]  = PrintReadinessGate::network_plugin_ready();
    flow["printer_connected"]   = PrintReadinessGate::has_bound_printer();
    flow["smart_print_enabled"] = BambuSmartPrintService::instance().is_enabled();

    const int done = SlicePilotSetupHub::completed_count();
    if (done < int(SetupHubStep::Count)) {
        flow["suggested_next"] =
            "Complete Smart Print setup (printer → plugin → connect → model), then prepare, slice, preview. The user presses the Print button.";
    } else if (plater && flow.value("has_model", false)) {
        flow["suggested_next"] =
            "Tune on Prepare if needed → slice, check the preview, then the user presses the Print button.";
    } else {
        flow["suggested_next"] = "Load a model (local file or MakerWorld), then prepare and slice.";
    }

    return flow;
}

OllamaFlowDispatchResult OllamaUserFlow::dispatch_coach_action(const std::string& action_id, Plater* plater)
{
    if (action_id == "arrange") {
        nlohmann::json root;
        root["actions"] = nlohmann::json::array({ nlohmann::json{{"type", "arrange"}} });
        return run_flow_actions(root);
    }
    if (action_id == "preview_tab") {
        nlohmann::json root;
        root["actions"] = nlohmann::json::array({ nlohmann::json{{"type", "ui_select_tab"}, {"tab", "preview"}} });
        return run_flow_actions(root);
    }
    if (action_id == "send_print")
        return send_print_flow(plater);
    if (action_id == "export_gcode") {
        if (plater)
            wxPostEvent(plater, SimpleEvent(EVT_GLTOOLBAR_EXPORT_GCODE));
        BeginnerJourney::on_sent_or_exported();
        OllamaFlowDispatchResult out;
        out.handled = true;
        return out;
    }
    if (action_id == "open_settings" || action_id == "open_setup")
        return open_setup_flow(plater);
    if (action_id == "open_smart_print") {
        refresh_smart_print_from_goal_session(plater);
        wxGetApp().open_smart_print();
        OllamaFlowDispatchResult out;
        out.handled = true;
        return out;
    }
    if (action_id == "undo_apply" || action_id == "rollback_apply") {
        nlohmann::json root;
        root["actions"] = nlohmann::json::array({ nlohmann::json{{"type", "rollback_apply"}} });
        return run_flow_actions(root);
    }

    OllamaFlowDispatchResult out;
    out.handled = false;
    return out;
}

bool OllamaUserFlow::is_acquisition_print_request(const std::string& user_utf8, bool plate_has_model)
{
    (void) user_utf8;
    (void) plate_has_model;
    return false;
}

void OllamaUserFlow::prune_navigation_for_config_fixes(nlohmann::json& root, const std::string& user_request)
{
    (void) root;
    (void) user_request;
}

bool OllamaUserFlow::result_is_navigation_only(const OllamaActionResult& result)
{
    if (!result.success || !result.effective_change)
        return false;
    const std::string& m = result.message;
    return m.find("Opened Smart Print") != std::string::npos
        || m.find("Opened Smart Print setup") != std::string::npos
        || m.find("Switched tab to smart_print") != std::string::npos;
}

OllamaActionResult OllamaUserFlow::apply_flow_action(const nlohmann::json& action, Plater* plater)
{
    OllamaActionResult result;
    const std::string  type = action.value("type", "");

    if (type == "open_smart_print" || type == "run_smart_print") {
        refresh_smart_print_from_goal_session(plater);
        // The AI only opens Smart Print; one-click print (which ends in a send)
        // stays a button the user presses there.
        wxGetApp().open_smart_print();
        result.success          = true;
        result.effective_change = true;
        result.message          = "Opened Smart Print";
        return result;
    }
    if (type == "open_setup") {
        open_setup_flow(plater);
        result.success          = true;
        result.effective_change = true;
        result.message          = "Opened Smart Print setup";
        return result;
    }
    if (type == "export_gcode") {
        if (plater)
            wxPostEvent(plater, SimpleEvent(EVT_GLTOOLBAR_EXPORT_GCODE));
        BeginnerJourney::on_sent_or_exported();
        result.success          = true;
        result.effective_change = true;
        result.message          = "Exporting G-code";
        return result;
    }
    if (type == "rollback_apply") {
        if (!plater) {
            result.message = "Plater not available";
            return result;
        }
        if (BambuSmartPrintService::instance().rollback_last_apply(plater)) {
            refresh_smart_print_from_goal_session(plater);
            result.success          = true;
            result.effective_change = true;
            result.message          = "Reverted last AI settings change";
        } else {
            result.message = "Nothing to roll back";
        }
        return result;
    }

    result.message = "Unknown flow action: " + type;
    return result;
}

}} // namespace

#include "OllamaUserFlow.hpp"

#include "../AICoach/BeginnerJourney.hpp"
#include "../BambuSmartPrint/BambuSmartPrintService.hpp"
#include "../BambuSmartPrint/PrintReadinessGate.hpp"
#include "../BambuSmartPrint/SlicePilotSetupHub.hpp"
#include "libslic3r/BambuSmartPrint/PrintGoalSession.hpp"
#include "../GLToolbar.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../MainFrame.hpp"
#include "../Plater.hpp"
#include "OllamaActionWorkflow.hpp"
#include "OllamaActionExecutor.hpp"

#include <boost/algorithm/string.hpp>

namespace Slic3r { namespace GUI {

namespace {

static const char* kStepNames[] = { "printer", "plugin", "connect", "model" };

bool contains_ci(const std::string& hay, const char* needle)
{
    return boost::ifind_first(hay, needle);
}

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

bool actions_contain_type(const nlohmann::json& root, const char* type)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return false;
    for (const auto& a : root["actions"]) {
        if (a.is_object() && a.value("type", "") == type)
            return true;
    }
    return false;
}

bool user_wants_print_flow(const std::string& user)
{
    return user.find("프린트") != std::string::npos || user.find("출력") != std::string::npos
        || user.find("인쇄") != std::string::npos || user.find("보내") != std::string::npos
        || contains_ci(user, "print") || contains_ci(user, "send to printer")
        || contains_ci(user, "start print");
}

bool user_wants_setup_flow(const std::string& user)
{
    return user.find("설정") != std::string::npos || user.find("연결") != std::string::npos
        || user.find("플러그인") != std::string::npos || user.find("셋업") != std::string::npos
        || contains_ci(user, "setup") || contains_ci(user, "connect") || contains_ci(user, "plugin")
        || contains_ci(user, "bind") || user.find("바인") != std::string::npos;
}

bool user_wants_smart_print(const std::string& user)
{
    return user.find("스마트") != std::string::npos || contains_ci(user, "smart print");
}

bool user_wants_preview_flow(const std::string& user)
{
    return user.find("미리보기") != std::string::npos || user.find("프리뷰") != std::string::npos
        || contains_ci(user, "preview") || contains_ci(user, "g-code viewer");
}

bool user_wants_device_flow(const std::string& user)
{
    return user.find("디바이스") != std::string::npos || user.find("프린터 탭") != std::string::npos
        || contains_ci(user, "device tab") || contains_ci(user, "monitor");
}

bool user_wants_export_flow(const std::string& user)
{
    return user.find("내보내") != std::string::npos || user.find("익스포트") != std::string::npos
        || contains_ci(user, "export g") || contains_ci(user, "export gcode");
}

bool user_wants_slice_flow(const std::string& user)
{
    return user.find("슬라이스") != std::string::npos || contains_ci(user, "slice");
}

bool user_wants_undo_apply(const std::string& user)
{
    return user.find("되돌") != std::string::npos || user.find("취소") != std::string::npos
        || contains_ci(user, "undo") || contains_ci(user, "rollback");
}

bool user_wants_failure_help(const std::string& user)
{
    return user.find("실패") != std::string::npos || user.find("망") != std::string::npos
        || contains_ci(user, "failed") || contains_ci(user, "failure");
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

    wxPostEvent(plater, SimpleEvent(EVT_GLTOOLBAR_SEND_GCODE));
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
            "Complete Smart Print setup (printer → plugin → connect → model), then prepare, slice, preview, send.";
    } else if (plater && flow.value("has_model", false)) {
        flow["suggested_next"] =
            "Tune on Prepare if needed → slice → Preview → send to printer or export G-code via actions.";
    } else {
        flow["suggested_next"] = "Load a model (local file or MakerWorld), then prepare and slice.";
    }

    return flow;
}

std::string OllamaUserFlow::flow_prompt_block(bool korean)
{
    if (korean) {
        return R"FLOW(
## 사용자 흐름 (모든 기능을 actions로 연결)
context.user_flow: active_tab, setup_steps, setup_next, suggested_next.

표준 여정: 모델 → Prepare(튜닝) → slice → Preview → send_print / export_gcode.
설정 미완(printer/plugin/connect/model)이면 print/send 전에 open_setup 또는 open_smart_print.

| 사용자 의도 | actions |
| 프린트/출력/보내기 | setup 미완 → open_setup; 모델 없음 → open_setup(model) 또는 makerworld_search; else send_print (+ 필요 시 slice, ui_select_tab preview) |
| 설정/연결/플러그인 | open_setup |
| 스마트 프린트 | open_smart_print |
| 미리보기 | ui_select_tab preview (+ slice if needed) |
| 디바이스/프린터 탭 | ui_select_tab monitor |
| G-code 내보내기 | export_gcode |
| 출력 실패/망함 | open_smart_print (Failure Doctor) |
| AI 변경 되돌리기 | rollback_apply |

workflow actions (JSON):
- open_smart_print, open_setup, send_print, export_gcode, rollback_apply
- ui_select_tab: prepare|preview|monitor|smart_print|home
- slice: {"type":"slice","scope":"plate"}
메뉴/단축키를 말로 안내하지 말고 actions만 사용.
)FLOW";
    }
    return R"FLOW(
## User flow (connect every feature via actions)
Read context.user_flow: active_tab, setup_steps, setup_next, suggested_next.

Standard journey: model → Prepare (tune) → slice → Preview → send_print / export_gcode.
If setup incomplete, use open_setup or open_smart_print before send_print.

| User intent | actions |
| print / send | incomplete setup → open_setup; no model → open_setup or makerworld_search; else send_print (+ slice, ui_select_tab preview if needed) |
| setup / connect / plugin | open_setup |
| smart print | open_smart_print |
| preview | ui_select_tab preview (+ slice if needed) |
| device / printer tab | ui_select_tab monitor |
| export g-code | export_gcode |
| print failed | open_smart_print |
| undo AI changes | rollback_apply |

Workflow action types:
open_smart_print, open_setup, send_print, export_gcode, rollback_apply,
ui_select_tab (prepare|preview|monitor|smart_print|home), slice {"scope":"plate"}.
Never tell the user to use menus or shortcuts — emit actions.
)FLOW";
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

void OllamaUserFlow::ensure_flow_actions_from_user_text(nlohmann::json& /*root*/, const std::string& /*user_request*/)
{
    // Flow/tab actions come from the LLM response — not keyword injection.
}

void OllamaUserFlow::prune_navigation_for_config_fixes(nlohmann::json& root, const std::string& user_request)
{
    if (!actions_contain_type(root, "set_config") || user_wants_smart_print(user_request))
        return;
    if (!root.contains("actions") || !root["actions"].is_array())
        return;

    nlohmann::json kept = nlohmann::json::array();
    for (const auto& a : root["actions"]) {
        if (!a.is_object()) {
            kept.push_back(a);
            continue;
        }
        const std::string type = a.value("type", "");
        if (type == "open_smart_print")
            continue;
        if (type == "ui_select_tab" && a.value("tab", "") == "smart_print")
            continue;
        kept.push_back(a);
    }
    root["actions"] = std::move(kept);
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
        if (type == "run_smart_print" && plater && BambuSmartPrintService::is_enabled()
            && BambuSmartPrintService::is_bbl_printer_active()) {
            BambuSmartPrintService::instance().run_one_click_print(plater);
            result.success          = true;
            result.effective_change = true;
            result.message          = "Started Smart Print one-click workflow";
            return result;
        }
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
    if (type == "send_print") {
        const OllamaFlowDispatchResult dispatch = send_print_flow(plater);
        result.success = dispatch.handled;
        if (dispatch.setup_blocked) {
            result.message = dispatch.blocked_message;
            return result;
        }
        result.effective_change = true;
        result.message          = "Sending to printer";
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

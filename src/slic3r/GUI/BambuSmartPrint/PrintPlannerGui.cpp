#include "PrintPlannerGui.hpp"

#include "BambuSmartPrintService.hpp"
#include "BambuSmartPrintWorkflowDialog.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../MainFrame.hpp"
#include "../NotificationManager.hpp"
#include "../Plater.hpp"
#include "../OllamaAssistant/OllamaSettingDescriptions.hpp"

#include "../AICoach/AICoachTrustBuilder.hpp"
#include "../AICoach/AICoachTriggerPolicy.hpp"
#include "../AICoach/AICoachTypes.hpp"

#include "libslic3r/BambuSmartPrint/MeshAnalysisCache.hpp"
#include "libslic3r/SlicePilot/SlicePilotRestrictions.hpp"
#include "../DeviceCore/DevManager.h"

#include <boost/algorithm/string.hpp>

#include <set>

namespace Slic3r { namespace GUI {

namespace {

using namespace BambuSmartPrint;

nlohmann::json set_config_action(const std::string& key, const nlohmann::json& value)
{
    nlohmann::json options = nlohmann::json::object({{key, value}});
    if (key == "enable_support" && value.is_boolean() && value.get<bool>())
        options["support_type"] = "tree(auto)";
    return nlohmann::json{
        {"type", "set_config"},
        {"preset", "print"},
        {"options", std::move(options)},
    };
}

AICoachCard make_plan_card(AICoachTriggerId id, AICoachImportance imp, const std::string& body,
                           std::vector<AICoachButton> buttons, nlohmann::json apply_root = {},
                           std::vector<std::string> bullets = {})
{
    AICoachCard c;
    c.trigger         = id;
    c.importance      = imp;
    c.title           = "AI Coach";
    c.body            = body;
    c.bullets         = std::move(bullets);
    c.buttons         = std::move(buttons);
    c.apply_root      = std::move(apply_root);
    c.auto_dismiss_ms = imp == AICoachImportance::Critical ? 0 : 10000;
    AICoachTriggerPolicy::apply_defaults(c);
    return c;
}

void enrich_cards(std::vector<AICoachCard>& cards, Plater* plater)
{
    for (AICoachCard& c : cards) {
        if (!c.apply_root.empty())
            AICoachTrustBuilder::enrich_recommendation_card(c, plater);
    }
}

static bool smart_print_ui_korean()
{
    const wxString code = wxGetApp().current_language_code();
    wxString       lang = code.BeforeFirst('_').BeforeFirst('-').Lower();
    if (lang == wxString("ko"))
        return true;
    lang = wxGetApp().current_language_code_safe().BeforeFirst('_').Lower();
    return lang == wxString("ko");
}

static bool is_generic_workflow_summary(const std::string& summary)
{
    if (summary.empty())
        return true;
    static const char* patterns[] = {
        "SlicePilot tuned",
        "tree supports, no raft",
        "Applying settings matched",
        "Suggested settings for your print goal",
        "Estimated success rate:",
        "adjustments are pending",
        "모델이 잘 붙는지",
        "오버행 부분을 서포트",
    };
    for (const char* p : patterns) {
        if (summary.find(p) != std::string::npos)
            return true;
    }
    return false;
}

static std::string workflow_summary_from_changes(const std::vector<SettingChange>& changes,
                                                 const std::string& fallback_message)
{
    const bool ko = smart_print_ui_korean();
    std::string fallback = fallback_message;
    if (is_generic_workflow_summary(fallback))
        fallback.clear();
    if (!changes.empty())
        return OllamaSettingDescriptions::build_summary(changes, fallback, ko);

    if (!fallback.empty())
        return fallback;

    if (ko)
        return "추가 설정 변경은 없습니다. 아래 모델 인사이트를 확인해 주세요.";
    return "No setting changes are pending — review model insights below.";
}

} // namespace

PlateContext PrintPlannerGui::build_plate_context(Plater* plater)
{
    PlateContext ctx;
    if (!plater || !wxGetApp().preset_bundle)
        return ctx;

    BambuSmartPrintService::instance().update_plate_assessment_data(plater);
    const auto& svc = BambuSmartPrintService::instance();

    ctx.mesh            = svc.last_mesh_analysis();
    ctx.readiness       = svc.last_readiness_report();
    ctx.prediction      = svc.last_prediction();
    ctx.base_config     = svc.last_baseline_config();
    ctx.proposed_config = svc.last_applied_config();
    ctx.auto_result     = svc.last_auto_result();
    ctx.slice           = svc.last_slice_analysis();
    ctx.has_slice       = ctx.slice.valid;
    ctx.has_model       = !plater->model().objects.empty();
    ctx.change_count    = ctx.auto_result.changes.size();
    ctx.filament_name   = ctx.mesh.suggested_material;
    ctx.printer_id      = "local";
    if (wxGetApp().getDeviceManager()) {
        if (MachineObject* sel = wxGetApp().getDeviceManager()->get_selected_machine())
            ctx.printer_id = sel->get_dev_id();
    }
    return ctx;
}

ApplyPolicy PrintPlannerGui::default_apply_policy()
{
    const auto mode = BambuSmartPrintService::auto_load_mode();
    if (mode == BambuSmartPrintService::AutoLoadMode::FullDialog)
        return ApplyPolicy::ReviewDialog;
    if (BambuSmartPrintService::safe_mode_enabled())
        return ApplyPolicy::SilentSafe;
    return ApplyPolicy::Notify;
}

PrintPlan PrintPlannerGui::plan_for_user_text(Plater* plater, const std::string& user_text)
{
    PlateContext ctx = build_plate_context(plater);
    PrintGoalSession::instance().merge_goal(PrintPlanner::parse_goal(user_text));
    PrintPlan plan = PrintPlanner::replan(ctx, PrintGoalSession::instance());
    plan.apply_policy = default_apply_policy();
    PrintGoalSession::instance().set_last_plan(plan);
    apply_plan_to_service(plan);
    return plan;
}

PrintPlan PrintPlannerGui::plan_from_assistant(Plater* plater, const std::string& user_text,
                                               const nlohmann::json& assistant_root)
{
    PlateContext ctx = build_plate_context(plater);
    PrintGoalSession::instance().merge_goal(PrintPlanner::parse_goal(user_text));
    PrintPlan plan =
        PrintPlanner::plan_from_assistant(ctx, PrintGoalSession::instance().goal(), assistant_root);
    plan.apply_policy = default_apply_policy();
    PrintGoalSession::instance().set_last_plan(plan);
    apply_plan_to_service(plan);
    return plan;
}

void PrintPlannerGui::apply_plan_to_service(const PrintPlan& plan)
{
    BambuSmartPrintService::instance().sync_from_plan(plan);
}

void PrintPlannerGui::dispatch_model_loaded(Plater* plater)
{
    if (!plater || plater->model().objects.empty())
        return;
    if (!BambuSmartPrintService::is_enabled())
        return;
    if (!wxGetApp().preset_bundle || !SlicePilot::is_active_printer_bbl(*wxGetApp().preset_bundle))
        return;

    MeshAnalysisCache::instance().clear();

    PlateContext ctx = build_plate_context(plater);
    PrintPlan    plan = PrintPlanner::replan(ctx, PrintGoalSession::instance());
    plan.apply_policy = default_apply_policy();
    PrintGoalSession::instance().set_last_plan(plan);
    apply_plan_to_service(plan);

    const auto load_mode = BambuSmartPrintService::auto_load_mode();

    if (load_mode == BambuSmartPrintService::AutoLoadMode::Off)
        return;

    if (plan.change_count > 0) {
        BambuSmartPrintService::notify_readiness_suggestions(
            plater, int(std::round(plan.success_estimate)), plan.change_count);
    } else if (load_mode == BambuSmartPrintService::AutoLoadMode::Notify && plan.readiness.score > 0.f) {
        wxString msg = wxString::Format(
            _L("Smart Print: %d%% estimated ready. Open Smart Print for details."),
            int(std::round(plan.success_estimate)));
        if (NotificationManager* nm = plater->get_notification_manager()) {
            nm->push_notification(NotificationType::CustomNotification,
                NotificationManager::NotificationLevel::RegularNotificationLevel,
                std::string(msg.utf8_str()),
                std::string(_L("Open Smart Print").utf8_str()),
                [](wxEvtHandler*) -> bool {
                    if (MainFrame* mf = wxGetApp().mainframe)
                        mf->select_tab(MainFrame::tpSmartPrint);
                    return false;
                });
        }
    }

    if (plan.apply_policy == ApplyPolicy::SilentSafe && plan.has_actions()) {
        BambuSmartPrintService::instance().apply_config_to_plater(
            plater, plan.base_config, plan.proposed_config, false, false);
    }

    wxGetApp().CallAfter([]() { BambuSmartPrintService::instance().refresh_all_panels(); });
}

std::vector<AICoachCard> PrintPlannerGui::coach_cards_from_plan(Plater* plater, const PrintPlan& plan)
{
    std::vector<AICoachCard> out;
    if (!plater)
        return out;

    const nlohmann::json plan_root = plan.root;
    std::set<PrintRiskKind> seen;

    for (const PrintRisk& risk : plan.risks) {
        if (seen.count(risk.kind))
            continue;
        seen.insert(risk.kind);

        switch (risk.kind) {
        case PrintRiskKind::Overhang: {
            nlohmann::json root = nlohmann::json{
                {"message", _u8L("Steep overhangs were found on this model. Turning on supports can improve print success.")},
                {"actions", nlohmann::json::array({set_config_action("enable_support", true)})},
            };
            out.push_back(make_plan_card(
                AICoachTriggerId::OverhangSupport, AICoachImportance::Normal,
                _u8L("Steep overhangs were found on this model. Turning on supports can improve print success."),
                {
                    {_u8L("Turn on supports"), AICoachButtonRole::Primary, "apply_actions", root},
                    {_u8L("Not now"), AICoachButtonRole::Secondary, "dismiss", {}},
                },
                root));
            break;
        }
        case PrintRiskKind::TallNarrow:
        case PrintRiskKind::SmallFootprint: {
            nlohmann::json root = nlohmann::json{
                {"message", _u8L("A wider brim helps tall prints stay stable on the bed.")},
                {"actions", nlohmann::json::array({
                                set_config_action("brim_type", "outer_only"),
                                set_config_action("brim_width", 5),
                            })},
            };
            out.push_back(make_plan_card(
                AICoachTriggerId::ModelTallBrim, AICoachImportance::Normal,
                _u8L("This model is tall. A brim can help keep it stable on the bed."),
                {
                    {_u8L("Apply"), AICoachButtonRole::Primary, "apply_actions", root},
                    {_u8L("Not now"), AICoachButtonRole::Secondary, "dismiss", {}},
                },
                root));
            break;
        }
        case PrintRiskKind::Adhesion: {
            nlohmann::json root = plan.has_actions() ? plan_root : nlohmann::json{
                {"message", _u8L("Safer first-layer settings can reduce the risk of the print coming loose.")},
                {"actions", nlohmann::json::array({
                                set_config_action("brim_type", "outer_only"),
                                set_config_action("brim_width", 5),
                            })},
            };
            std::vector<std::string> bullets;
            if (!risk.detail.empty())
                bullets.push_back(risk.detail);
            out.push_back(make_plan_card(
                AICoachTriggerId::AdhesionRisk, AICoachImportance::Normal,
                _u8L("The first layer may not stick well with the current settings. I can suggest safer values."),
                {
                    {_u8L("Apply suggestions"), AICoachButtonRole::Primary, "apply_actions", root},
                    {_u8L("Not now"), AICoachButtonRole::Secondary, "dismiss", {}},
                },
                root, std::move(bullets)));
            break;
        }
        default:
            break;
        }
    }

    enrich_cards(out, plater);
    return out;
}

SmartPrintWorkflowContent PrintPlannerGui::workflow_content_from_plan(const PrintPlan& plan)
{
    SmartPrintWorkflowContent content;
    content.suggested_material  = plan.mesh.suggested_material;
    content.prediction_summary  = plan.prediction.summary;
    content.active_filament     = plan.readiness.active_filament_hint;
    content.filament_mismatch   = plan.readiness.filament_mismatch;
    content.complexity_score    = plan.mesh.complexity_score;
    content.change_count        = plan.change_count;
    content.insights            = plan.readiness.insights;
    content.show_success_gauge  = true;
    content.is_failure_workflow = false;

    const std::string fallback = plan.explanation.summary.empty() ? plan.message() : plan.explanation.summary;
    enrich_workflow_content(content, plan.auto_result.changes, plan.readiness, fallback);

    for (const PrintRisk& r : plan.risks) {
        if (!r.recommended_action_text.empty())
            content.risk_factors.push_back(r.label + " → " + r.recommended_action_text);
        else
            content.risk_factors.push_back(r.label + (r.detail.empty() ? "" : " — " + r.detail));
    }
    for (const std::string& rf : plan.prediction.risk_factors) {
        if (content.risk_factors.size() >= 8)
            break;
        content.risk_factors.push_back(rf);
    }
    if (!plan.explanation.tradeoff_note.empty())
        content.change_preview.insert(content.change_preview.begin(), plan.explanation.tradeoff_note);

    return content;
}

void PrintPlannerGui::enrich_workflow_content(SmartPrintWorkflowContent& content,
                                              const std::vector<SettingChange>& changes,
                                              const ReadinessReport& readiness,
                                              const std::string& fallback_message)
{
    content.summary = workflow_summary_from_changes(changes, fallback_message);
    content.readiness_headline = readiness.headline;
    content.success_rate       = readiness.score > 0.f ? readiness.score : content.success_rate;
    content.change_count       = changes.empty() ? content.change_count : changes.size();
    content.change_preview.clear();
    const bool ko = smart_print_ui_korean();
    for (const SettingChange& ch : changes) {
        if (content.change_preview.size() >= 8)
            break;
        content.change_preview.push_back(OllamaSettingDescriptions::preview_line(ch, ko));
    }
}

}} // namespace

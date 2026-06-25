#include "AIGuiOrchestrator.hpp"

#include "AICoachApplyDedup.hpp"
#include "AICoachController.hpp"
#include "AICoachRulesEngine.hpp"

#include "../OllamaAssistant/OllamaActionPipeline.hpp"
#include "../OllamaAssistant/OllamaAgentEventBus.hpp"
#include "../OllamaAssistant/OllamaModelLoadAdvisor.hpp"

#include "../I18N.hpp"

#include "../BambuSmartPrint/PrintPlannerGui.hpp"

#include "../GLCanvas3D.hpp"
#include "../DailyTips.hpp"
#include "../NotificationManager.hpp"
#include "../GUI_App.hpp"
#include "../Plater.hpp"

#include <wx/app.h>

namespace Slic3r { namespace GUI {

namespace {

bool ollama_assist_active()
{
    if (!wxGetApp().app_config)
        return false;
    const std::string mode = wxGetApp().app_config->get("ollama", "assistant_mode");
    return mode == "assist" || mode == "apply";
}

} // namespace

AIGuiOrchestrator& AIGuiOrchestrator::instance()
{
    static AIGuiOrchestrator s;
    return s;
}

void AIGuiOrchestrator::apply_event_suppressions(Plater* plater, AICoachSuppression mask)
{
    if (!plater || mask == AICoachSuppression::None)
        return;

    if (suppression_active(mask, AICoachSuppression::SliceProgressNotification)) {
        if (NotificationManager* nm = plater->get_notification_manager())
            nm->set_slicing_progress_hidden();
    }
    if (suppression_active(mask, AICoachSuppression::DailyTipsInSliceNotif))
        plater->get_dailytips()->close();
    if (suppression_active(mask, AICoachSuppression::GcodeLegendAutoShow)) {
        if (GLCanvas3D* preview = plater->get_preview_canvas3D())
            preview->get_gcode_viewer().show_legend(false);
    }
}

bool AIGuiOrchestrator::active_suppression(AICoachSuppression flag) const
{
    if (!AICoachController::is_enabled_for_current_mode())
        return false;
    if (!AICoachController::instance().overlay().has_card())
        return false;
    const AICoachCard& card = AICoachController::instance().overlay().card();
    return suppression_active(AICoachTriggerPolicy::get(card.trigger).suppresses_while_active, flag);
}

bool AIGuiOrchestrator::should_render_dailytips_window() const
{
    if (active_suppression(AICoachSuppression::DailyTipsInSliceNotif))
        return false;
    return true;
}

bool AIGuiOrchestrator::should_render_beginner_journey() const
{
    if (active_suppression(AICoachSuppression::BeginnerJourney))
        return false;
    return true;
}

bool AIGuiOrchestrator::should_enqueue_beginner_tour() const
{
    if (!AICoachController::is_enabled_for_current_mode())
        return true;
    if (active_suppression(AICoachSuppression::BeginnerTourEnqueue))
        return false;
    if (defers_coach_cards())
        return false;
    return true;
}

void AIGuiOrchestrator::on_model_loaded(Plater* plater)
{
    if (plater && ollama_assist_active())
        OllamaModelLoadAdvisor::schedule_after_model_load(plater);
    PrintPlannerGui::dispatch_model_loaded(plater);
}

void AIGuiOrchestrator::on_slice_completed(Plater* plater, const Print* print, bool success)
{
    if (plater) {
        nlohmann::json payload = {{"success", success}};
        OllamaAgentEventBus::instance().publish(OllamaAgentEventKind::SliceDone, std::move(payload));
    }
    if (!success && plater && AICoachController::is_enabled_for_current_mode()) {
        const bool ko = wxGetApp().current_language_code().StartsWith("ko");
        const std::string summary =
            ko ? "슬라이싱에 실패했습니다. 모델 배치, 서포트, 벽 두께를 확인해 보세요."
               : "Slicing failed. Check model placement, supports, and wall settings.";
        nlohmann::json apply_root = OllamaActionPipeline::build_symptom_fallback_root(
            ko ? "슬라이싱 실패 서포트 브림" : "slicing failed support brim", false);
        AICoachController::instance().on_print_failure_hint(plater, summary, apply_root);
    }
    if (!plater || !success || !AICoachController::is_enabled_for_current_mode()) {
        AICoachController::instance().on_slice_completed(plater, print, success);
        return;
    }

    apply_event_suppressions(plater, AICoachTriggerPolicy::get(AICoachTriggerId::SliceDoneSend).suppresses_on_event);
    AICoachController::instance().on_slice_completed(plater, print, success);
}

void AIGuiOrchestrator::on_print_failure_hint(Plater* plater, const std::string& summary,
                                              const nlohmann::json& apply_root)
{
    AICoachController::instance().on_print_failure_hint(plater, summary, apply_root);
}

void AIGuiOrchestrator::on_print_success(Plater* plater, const std::string& job_name)
{
    AICoachController::instance().on_print_success(plater, job_name);
}

void AIGuiOrchestrator::on_print_running(Plater* plater, int print_percent)
{
    if (!plater || print_percent < 0 || defers_coach_cards())
        return;
    if (!AICoachController::is_enabled_for_current_mode())
        return;
    AICoachController::instance().enqueue_cards(
        AICoachRulesEngine::evaluate_during_print(plater, print_percent));
}

void AIGuiOrchestrator::on_chat_apply_begin()
{
    m_chat_apply_in_progress = true;
}

void AIGuiOrchestrator::on_chat_apply_end(bool applied, const nlohmann::json& root, const char* dedup_source)
{
    m_chat_apply_in_progress = false;
    if (applied) {
        wxGetApp().set_show_gcode_window(false);
        if (!root.empty() && dedup_source && dedup_source[0] != '\0')
            AICoachApplyDedup::instance().record_applied_root(root, dedup_source);
    }
    flush_deferred_coach_cards();
}

void AIGuiOrchestrator::defer_coach_cards(std::vector<AICoachCard> cards)
{
    for (AICoachCard& c : cards) {
        if (c.importance == AICoachImportance::Critical)
            m_deferred_critical.insert(m_deferred_critical.begin(), std::move(c));
        else
            m_deferred_cards.push_back(std::move(c));
    }
}

void AIGuiOrchestrator::defer_coach_card(AICoachCard card)
{
    if (card.importance == AICoachImportance::Critical)
        m_deferred_critical.insert(m_deferred_critical.begin(), std::move(card));
    else
        m_deferred_cards.insert(m_deferred_cards.begin(), std::move(card));
}

void AIGuiOrchestrator::flush_deferred_coach_cards()
{
    if (m_deferred_critical.empty() && m_deferred_cards.empty())
        return;
    if (!m_deferred_critical.empty()) {
        AICoachController::instance().enqueue_cards(std::move(m_deferred_critical));
        m_deferred_critical.clear();
    }
    if (!m_deferred_cards.empty()) {
        AICoachController::instance().enqueue_cards(std::move(m_deferred_cards));
        m_deferred_cards.clear();
    }
}

void AIGuiOrchestrator::on_makerworld_search_begin()
{
    m_makerworld_search_in_progress = true;
}

void AIGuiOrchestrator::on_makerworld_search_end()
{
    m_makerworld_search_in_progress = false;
    flush_deferred_coach_cards();
}

}} // namespace

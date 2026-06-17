#include "AICoachController.hpp"

#include "AICoachApplyDedup.hpp"
#include "AICoachFinishingBuilder.hpp"
#include "AICoachRulesEngine.hpp"
#include "AICoachTrustBuilder.hpp"
#include "AICoachTriggerPolicy.hpp"
#include "AIGuiOrchestrator.hpp"
#include "BeginnerJourney.hpp"

#include "../BambuSmartPrint/BambuSmartPrintService.hpp"
#include "../BambuSmartPrint/PrintReadinessGate.hpp"
#include "../GLCanvas3D.hpp"
#include "../GUI_App.hpp"
#include "../MainFrame.hpp"
#include "../OllamaAssistant/OllamaActionExecutor.hpp"
#include "../OllamaAssistant/OllamaActionPipeline.hpp"
#include "../OllamaAssistant/OllamaActionValidator.hpp"
#include "../OllamaAssistant/OllamaActionWorkflow.hpp"
#include "../GLToolbar.hpp"
#include "../NotificationManager.hpp"
#include "../Plater.hpp"

#include "libslic3r/Config.hpp"
#include "libslic3r/Print.hpp"

#include <wx/event.h>
#include <wx/msgdlg.h>

namespace Slic3r { namespace GUI {

namespace {

constexpr const char* kEnabledKey     = "ai_coach_enabled";
constexpr const char* kBedHintsKey    = "ai_coach_show_bed_hints";

bool workflow_had_effective_change(const OllamaWorkflowRun& workflow)
{
    for (const auto& r : workflow.results) {
        if (r.success && r.effective_change)
            return true;
    }
    return false;
}

bool app_config_bool(const char* key, bool default_val)
{
    if (!wxGetApp().app_config)
        return default_val;
    if (!wxGetApp().app_config->has(key))
        return default_val;
    return wxGetApp().app_config->get_bool(key);
}

} // namespace

AICoachController& AICoachController::instance()
{
    static AICoachController s;
    return s;
}

bool AICoachController::is_enabled_for_current_mode()
{
    if (!wxGetApp().app_config)
        return true;

    const ConfigOptionMode mode = wxGetApp().get_mode();
    if (mode == comExpert) {
        if (!wxGetApp().app_config->has(kEnabledKey))
            return false;
        return app_config_bool(kEnabledKey, false);
    }
    if (mode == comAdvanced && !app_config_bool(kBedHintsKey, true)) {
        // Advanced still enabled; bed hints filtered in should_show_trigger
    }
    return true;
}

bool AICoachController::should_show_trigger(AICoachTriggerId id, AICoachImportance imp)
{
    if (!is_enabled_for_current_mode())
        return false;

    const ConfigOptionMode mode = wxGetApp().get_mode();
    if (mode == comSimple)
        return true;

    if (mode == comExpert && !app_config_bool(kEnabledKey, false))
        return false;

    if (mode == comAdvanced) {
        if (id == AICoachTriggerId::BedArrange)
            return app_config_bool(kBedHintsKey, true);
        if (imp == AICoachImportance::Low)
            return false;
        return id == AICoachTriggerId::AdhesionRisk || id == AICoachTriggerId::OverhangSupport
            || id == AICoachTriggerId::ModelTallBrim || id == AICoachTriggerId::SliceDoneSend
            || id == AICoachTriggerId::PrintFailure || id == AICoachTriggerId::FailureDoctor
            || id == AICoachTriggerId::PrintSuccessFinishing || id == AICoachTriggerId::PrintMonitor
            || id == AICoachTriggerId::PersonalTrainer || id == AICoachTriggerId::AppliedUndo;
    }

    return true;
}

bool AICoachController::was_recently_shown(AICoachTriggerId id) const
{
    if (!AICoachTriggerPolicy::uses_dedup(id))
        return false;
    const int dedup_ms = AICoachTriggerPolicy::get(id).dedup_ms;
    const auto it = m_last_shown_ms_by_trigger.find(id);
    if (it == m_last_shown_ms_by_trigger.end())
        return false;
    return GLCanvas3D::timestamp_now() - it->second < dedup_ms;
}

void AICoachController::mark_shown(AICoachTriggerId id)
{
    if (!AICoachTriggerPolicy::uses_dedup(id))
        return;
    m_last_shown_ms_by_trigger[id] = GLCanvas3D::timestamp_now();
}

void AICoachController::enqueue_cards(std::vector<AICoachCard> cards)
{
    if (AIGuiOrchestrator::instance().defers_coach_cards()) {
        AIGuiOrchestrator::instance().defer_coach_cards(std::move(cards));
        return;
    }
    for (AICoachCard& c : cards) {
        AICoachTriggerPolicy::apply_defaults(c);
        if (!should_show_trigger(c.trigger, c.importance))
            continue;
        if (AICoachApplyDedup::instance().card_is_redundant(c))
            continue;
        if (was_recently_shown(c.trigger))
            continue;
        if (c.importance == AICoachImportance::Critical)
            m_queue.insert(m_queue.begin(), std::move(c));
        else
            m_queue.push_back(std::move(c));
    }
    show_next_if_idle();
}

void AICoachController::enqueue_card(AICoachCard card)
{
    enqueue_cards({ std::move(card) });
}

void AICoachController::show_next_if_idle()
{
    if (m_overlay.has_card() || m_queue.empty())
        return;
    AICoachCard next = std::move(m_queue.front());
    m_queue.erase(m_queue.begin());
    mark_shown(next.trigger);
    m_overlay.show_card(std::move(next));
    if (Plater* p = wxGetApp().plater())
        if (GLCanvas3D* c = p->get_current_canvas3D())
            c->schedule_extra_frame(0);
}

void AICoachController::handle_button(Plater* plater, const AICoachButton& btn, const AICoachCard& card)
{
    if (btn.action_id == "dismiss") {
        m_overlay.dismiss();
        show_next_if_idle();
        return;
    }

    if (btn.action_id == "apply_actions" && !card.apply_root.empty()) {
        apply_with_confirmation(plater, card);
        return;
    }

    if (btn.action_id == "undo_apply") {
        BambuSmartPrintService::instance().rollback_last_apply(plater);
        m_overlay.dismiss();
        show_next_if_idle();
        return;
    }

    if (btn.action_id == "feedback_good" || btn.action_id == "feedback_ok" || btn.action_id == "feedback_bad") {
        if (wxGetApp().app_config)
            wxGetApp().app_config->set("ai_coach_last_feedback", btn.action_id);
        m_overlay.dismiss();
        if (btn.action_id == "feedback_bad" && plater)
            enqueue_cards(AICoachRulesEngine::evaluate_personal_trainer(plater));
        show_next_if_idle();
        return;
    }

    if (btn.action_id == "arrange") {
        nlohmann::json root;
        nlohmann::json arrange_action = {{"type", "arrange"}};
        root["actions"] = nlohmann::json::array({arrange_action});
        OllamaActionWorkflow::execute_inline(root, nullptr);
        m_overlay.dismiss();
        show_next_if_idle();
        return;
    }

    if (btn.action_id == "preview_tab" && wxGetApp().mainframe) {
        wxGetApp().mainframe->select_tab(MainFrame::tpPreview);
        m_overlay.dismiss();
        show_next_if_idle();
        return;
    }

    if (btn.action_id == "send_print" && plater) {
        if (PrintReadinessGate::run(plater, wxGetApp().mainframe) != PrintGateResult::Proceed) {
            AICoachCard err;
            err.trigger         = AICoachTriggerId::SendGateBlocked;
            err.importance      = AICoachImportance::Normal;
            err.body            = _u8L("Complete printer setup before sending — open Smart Print Setup to connect your printer.");
            err.auto_dismiss_ms = 12000;
            err.buttons         = {
                {_u8L("OK"), AICoachButtonRole::Primary, "dismiss", {}},
            };
            show_card(std::move(err));
        } else {
            m_overlay.dismiss();
            wxPostEvent(plater, SimpleEvent(EVT_GLTOOLBAR_SEND_GCODE));
            BeginnerJourney::on_sent_or_exported();
            show_next_if_idle();
        }
        return;
    }

    if (btn.action_id == "export_gcode" && plater) {
        wxPostEvent(plater, SimpleEvent(EVT_GLTOOLBAR_EXPORT_GCODE));
        BeginnerJourney::on_sent_or_exported();
        m_overlay.dismiss();
        show_next_if_idle();
        return;
    }

    if (btn.action_id == "open_settings" && wxGetApp().mainframe) {
        wxGetApp().save_mode(comSimple);
        wxGetApp().mainframe->select_tab(MainFrame::tp3DEditor);
        m_overlay.dismiss();
        show_next_if_idle();
        return;
    }
}

void AICoachController::on_model_loaded(Plater* plater)
{
    if (!plater || !is_enabled_for_current_mode())
        return;
    wxGetApp().CallAfter([plater]() {
        if (!wxGetApp().plater())
            return;
        AICoachController::instance().enqueue_cards(
            AICoachRulesEngine::evaluate_after_model_load(plater));
    });
}

void AICoachController::on_slice_completed(Plater* plater, const Print* print, bool success)
{
    if (!plater || !is_enabled_for_current_mode())
        return;

    enqueue_cards(AICoachRulesEngine::evaluate_after_slice(plater, print, success));
}

void AICoachController::apply_with_confirmation(Plater* plater, const AICoachCard& card)
{
    if (!plater || card.apply_root.empty())
        return;

    wxString confirm_body = wxString::FromUTF8(card.body);
    for (const std::string& line : card.sections) {
        confirm_body << "\n" << wxString::FromUTF8(line);
    }
    if (wxMessageBox(confirm_body, _L("Apply this AI Coach suggestion?"),
            wxYES_NO | wxICON_QUESTION, wxGetApp().GetTopWindow())
        != wxYES)
        return;

    apply_card_actions(plater, card);
}

void AICoachController::apply_card_actions(Plater* plater, const AICoachCard& card)
{
    if (!plater || card.apply_root.empty())
        return;

    nlohmann::json root = card.apply_root;
    OllamaPipelineOptions opt;
    opt.apply_mode         = true;
    opt.include_makerworld = false;
    opt.user_request       = card.body;
    OllamaActionPipeline::process_actions(root, opt);

    if (!root.contains("actions") || !root["actions"].is_array() || root["actions"].empty())
        return;

    AIGuiOrchestrator::instance().on_chat_apply_begin();
    const OllamaWorkflowRun workflow = OllamaActionWorkflow::execute_inline(root, nullptr);
    const bool applied               = workflow_had_effective_change(workflow);
    AIGuiOrchestrator::instance().on_chat_apply_end(applied, root, nullptr);

    if (!applied)
        return;

    AICoachApplyDedup::instance().record_applied_root(root, "ai_coach");
    m_overlay.dismiss();
    show_undo_card();
}

void AICoachController::show_undo_card()
{
    AICoachCard c;
    c.trigger         = AICoachTriggerId::AppliedUndo;
    c.kind            = AICoachCardKind::AppliedUndo;
    c.importance      = AICoachImportance::Normal;
    c.title           = _u8L("AI Coach");
    c.body            = _u8L("Changes applied. You can undo them from here.");
    c.auto_dismiss_ms = 15000;
    c.buttons         = {
        {_u8L("Undo"), AICoachButtonRole::Primary, "undo_apply", {}},
        {_u8L("OK"), AICoachButtonRole::Secondary, "dismiss", {}},
    };
    show_card(std::move(c));
}

void AICoachController::on_print_failure_hint(Plater* plater, const std::string& summary,
                                              const nlohmann::json& apply_root)
{
    if (!is_enabled_for_current_mode() || summary.empty())
        return;

    AICoachCard c;
    c.trigger    = AICoachTriggerId::FailureDoctor;
    c.importance = AICoachImportance::Critical;
    c.title      = _u8L("AI Coach");
    c.body       = summary;
    c.apply_root = apply_root;
    c.auto_dismiss_ms = 0;
    if (!apply_root.empty() && apply_root.contains("actions") && apply_root["actions"].is_array()
        && !apply_root["actions"].empty()) {
        if (AICoachTrustBuilder::enrich_recommendation_card(c, plater)) {
            if (c.buttons.empty()) {
                c.buttons = {
                    {_u8L("Apply suggestions"), AICoachButtonRole::Primary, "apply_actions", c.apply_root},
                    {_u8L("Not now"), AICoachButtonRole::Secondary, "dismiss", {}},
                };
            }
        } else {
            c.apply_root = nlohmann::json::object();
        }
    } else {
        c.buttons = {{_u8L("Close"), AICoachButtonRole::Primary, "dismiss", {}}};
    }
    if (!should_show_trigger(c.trigger, c.importance))
        return;
    AICoachTriggerPolicy::apply_defaults(c);
    show_card(std::move(c));
}

void AICoachController::on_print_success(Plater* plater, const std::string& job_name)
{
    if (!plater || !is_enabled_for_current_mode())
        return;
    AICoachCard c = AICoachFinishingBuilder::build_print_success_card(plater, job_name);
    if (!should_show_trigger(c.trigger, c.importance))
        return;
    AICoachTriggerPolicy::apply_defaults(c);
    enqueue_card(std::move(c));

    if (wxGetApp().app_config && wxGetApp().app_config->get("ai_coach_last_feedback") == "feedback_bad") {
        enqueue_cards(AICoachRulesEngine::evaluate_personal_trainer(plater));
    }
}

void AICoachController::show_card(AICoachCard card)
{
    if (AIGuiOrchestrator::instance().defers_coach_cards()
        && card.importance != AICoachImportance::Critical) {
        AIGuiOrchestrator::instance().defer_coach_card(std::move(card));
        return;
    }
    AICoachTriggerPolicy::apply_defaults(card);
    m_queue.clear();
    mark_shown(card.trigger);
    m_overlay.show_card(std::move(card));
    if (Plater* p = wxGetApp().plater())
        if (GLCanvas3D* c = p->get_current_canvas3D())
            c->schedule_extra_frame(0);
}

float AICoachController::reserved_bottom_height(float scale) const
{
    if (!is_enabled_for_current_mode())
        return 0.f;
    return m_overlay.reserved_bottom_height(scale);
}

void AICoachController::render(GLCanvas3D& canvas, float bottom_margin, float right_margin)
{
    if (!is_enabled_for_current_mode())
        return;
    m_overlay.render(canvas, bottom_margin, right_margin);
}

bool AICoachController::update(GLCanvas3D& canvas, bool paused, int64_t delta_ms)
{
    (void) canvas;
    if (!is_enabled_for_current_mode())
        return false;

    const bool need = m_overlay.update_state(paused, delta_ms);

    if (m_overlay.has_card()) {
        const int btn_idx = m_overlay.poll_clicked_button();
        if (btn_idx >= 0 && !m_button_dispatch_pending) {
            const AICoachCard& card = m_overlay.card();
            if (static_cast<size_t>(btn_idx) < card.buttons.size()) {
                const AICoachButton button = card.buttons[static_cast<size_t>(btn_idx)];
                const AICoachCard   card_copy = card;
                m_button_dispatch_pending = true;
                wxGetApp().CallAfter([button, card_copy]() {
                    AICoachController& self = AICoachController::instance();
                    self.m_button_dispatch_pending = false;
                    Plater* plater = wxGetApp().plater();
                    if (!plater)
                        return;
                    self.handle_button(plater, button, card_copy);
                });
            }
        }
    }

    if (!m_overlay.has_card())
        show_next_if_idle();

    return need || m_overlay.has_card();
}

}} // namespace

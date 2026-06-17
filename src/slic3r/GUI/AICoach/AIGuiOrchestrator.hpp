#ifndef slic3r_AIGuiOrchestrator_hpp_
#define slic3r_AIGuiOrchestrator_hpp_

#include "AICoachTriggerPolicy.hpp"
#include "AICoachTypes.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Slic3r {
class Print;

namespace GUI {

class Plater;

/** Coordinates Coach cards, notifications, modals, and passive UI (journey, tips, legend). */
class AIGuiOrchestrator
{
public:
    static AIGuiOrchestrator& instance();

    void on_model_loaded(Plater* plater);
    void on_slice_completed(Plater* plater, const Print* print, bool success);
    void on_print_failure_hint(Plater* plater, const std::string& summary,
                               const nlohmann::json& apply_root = nlohmann::json::object());
    void on_print_success(Plater* plater, const std::string& job_name);
    void on_print_running(Plater* plater, int print_percent);

    void on_chat_apply_begin();
    void on_chat_apply_end(bool applied, const nlohmann::json& root = nlohmann::json::object(),
                           const char* dedup_source = "ollama");

    void on_makerworld_search_begin();
    void on_makerworld_search_end();

    void defer_coach_cards(std::vector<AICoachCard> cards);
    void defer_coach_card(AICoachCard card);
    void flush_deferred_coach_cards();

    bool should_render_dailytips_window() const;
    bool should_render_beginner_journey() const;
    bool should_enqueue_beginner_tour() const;
    bool defers_coach_cards() const { return m_chat_apply_in_progress || m_makerworld_search_in_progress; }

    bool active_suppression(AICoachSuppression flag) const;
    void apply_event_suppressions(Plater* plater, AICoachSuppression mask);

private:
    AIGuiOrchestrator() = default;

    bool m_chat_apply_in_progress{ false };
    bool m_makerworld_search_in_progress{ false };
    std::vector<AICoachCard> m_deferred_cards;
    std::vector<AICoachCard> m_deferred_critical;
};

}} // namespace

#endif

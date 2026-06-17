#ifndef slic3r_AICoachController_hpp_
#define slic3r_AICoachController_hpp_

#include "AICoachOverlay.hpp"
#include "AICoachTypes.hpp"

#include <nlohmann/json.hpp>
#include <unordered_map>

namespace Slic3r {
class Print;

namespace GUI {

class Plater;

class AICoachController
{
public:
    static AICoachController& instance();

    static bool is_enabled_for_current_mode();
    static bool should_show_trigger(AICoachTriggerId id, AICoachImportance imp);

    void on_model_loaded(Plater* plater);
    void on_slice_completed(Plater* plater, const Print* print, bool success);
    void on_print_failure_hint(Plater* plater, const std::string& summary,
                               const nlohmann::json& apply_root = nlohmann::json::object());
    void on_print_success(Plater* plater, const std::string& job_name);

    void show_card(AICoachCard card);
    void enqueue_card(AICoachCard card);
    void enqueue_cards(std::vector<AICoachCard> cards);

    void render(GLCanvas3D& canvas, float bottom_margin, float right_margin);
    bool update(GLCanvas3D& canvas, bool paused, int64_t delta_ms);

    /** Extra bottom inset for notification stacking when a coach card is shown. */
    float reserved_bottom_height(float scale) const;

    AICoachOverlay& overlay() { return m_overlay; }

private:
    AICoachController() = default;

    void show_next_if_idle();
    void handle_button(Plater* plater, const AICoachButton& btn, const AICoachCard& card);
    void apply_with_confirmation(Plater* plater, const AICoachCard& card);
    void apply_card_actions(Plater* plater, const AICoachCard& card);
    void show_undo_card();
    bool was_recently_shown(AICoachTriggerId id) const;
    void mark_shown(AICoachTriggerId id);

    AICoachOverlay                              m_overlay;
    std::vector<AICoachCard>                    m_queue;
    std::unordered_map<AICoachTriggerId, int64_t> m_last_shown_ms_by_trigger;
    bool                                        m_button_dispatch_pending{ false };
};

}} // namespace

#endif

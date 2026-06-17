#ifndef slic3r_AICoachOverlay_hpp_
#define slic3r_AICoachOverlay_hpp_

#include "AICoachTypes.hpp"

namespace Slic3r { namespace GUI {

class GLCanvas3D;

class AICoachOverlay
{
public:
    void show_card(AICoachCard card);
    void dismiss();
    bool has_card() const { return m_has_card; }
    const AICoachCard& card() const { return m_card; }

    void render(GLCanvas3D& canvas, float bottom_margin, float right_margin);
    bool update_state(bool paused, int64_t delta_ms);
    bool needs_frame() const;

    /** Returns button index clicked this frame, or -1. */
    int poll_clicked_button();

    /** Height to keep clear above the canvas bottom for notifications (0 if no card). */
    float reserved_bottom_height(float scale) const;

private:
    void render_card(GLCanvas3D& canvas, float bottom_margin, float right_margin);

    AICoachCard m_card;
    bool        m_has_card{ false };
    int64_t     m_show_start_ms{ 0 };
    int64_t     m_next_render_ms{ 0 };
    float       m_anim_opacity{ 0.f };
    float       m_anim_offset_y{ 20.f };
    bool        m_dismissing{ false };
    int         m_clicked_button{ -1 };
    float       m_last_window_height{ 0.f };
};

}} // namespace

#endif

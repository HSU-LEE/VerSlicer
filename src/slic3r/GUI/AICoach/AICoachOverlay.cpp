#include "AICoachOverlay.hpp"

#include "../GLCanvas3D.hpp"
#include "../GUI_App.hpp"
#include "../GUI_Utils.hpp"
#include "../ImGuiWrapper.hpp"
#include "../I18N.hpp"

#include <algorithm>
#include <imgui/imgui.h>

namespace Slic3r { namespace GUI {

namespace {

constexpr float kCoachWidthMin   = 320.f;
constexpr float kCoachWidthMax   = 420.f;
constexpr float kAnimDurationMs  = 250.f;
constexpr float kRightGap              = 16.f;
constexpr float kNotificationGap       = 12.f;
constexpr float kDefaultReserveHeight  = 220.f;
/** ImGui mis-sizes windows when alpha is exactly 0 (see GLCanvas3D fade-in). */
constexpr float kMinImGuiAlpha   = 0.01f;

struct CoachPanelTheme {
    ImVec4 window_bg;
    ImVec4 border;
    ImVec4 text;
    ImVec4 accent;
    float  window_rounding;
    float  border_size;
    float  left_bar_width;
};

CoachPanelTheme coach_panel_theme(bool dark, float scale)
{
    CoachPanelTheme t;
    t.window_rounding = 4.f * scale;
    t.border_size     = t.window_rounding / 4.f;
    t.left_bar_width  = t.window_rounding * 2.f;
    t.accent          = ImGuiWrapper::to_ImVec4(decode_color_to_float_array("#FF8F4A"));
    if (dark) {
        t.window_bg = ImVec4(45 / 255.f, 45 / 255.f, 49 / 255.f, 1.f);
        t.border    = ImVec4(62 / 255.f, 62 / 255.f, 69 / 255.f, 1.f);
        t.text      = ImVec4(224 / 255.f, 224 / 255.f, 224 / 255.f, 1.f);
    } else {
        t.window_bg = ImVec4(1.f, 1.f, 1.f, 1.f);
        t.border    = ImVec4(214 / 255.f, 214 / 255.f, 220 / 255.f, 1.f);
        t.text      = ImVec4(50 / 255.f, 58 / 255.f, 61 / 255.f, 1.f);
    }
    return t;
}

bool is_section_heading(const std::string& line)
{
    if (line.empty() || (line.size() >= 3 && line.compare(0, 3, "☐") == 0))
        return false;
    if (line.find("→") != std::string::npos)
        return false;
    if (line.find(_u8L("Confidence:")) == 0)
        return true;
    if (line.find(':') != std::string::npos && line.find("->") == std::string::npos) {
        const size_t colon = line.find(':');
        return colon > 0 && colon < 28;
    }
    return line.find(_u8L("Step ")) == 0 || line.find(_u8L("Reason")) == 0
        || line.find(_u8L("What to expect")) == 0 || line.find(_u8L("What will change")) == 0
        || line.find(_u8L("Summary")) == 0
        || line.find(_u8L("Post-processing")) == 0 || line.find(_u8L("Print record")) == 0
        || line.find(_u8L("How did")) == 0;
}

void draw_left_accent_bar(const CoachPanelTheme& theme, float opacity)
{
    ImDrawList* const draw_list = ImGui::GetWindowDrawList();
    const ImVec2      win_pos   = ImGui::GetWindowPos();
    const ImVec2      win_size  = ImGui::GetWindowSize();
    const float       border    = ImGui::GetStyle().WindowBorderSize;
    const ImVec2      bar_min(win_pos.x + border, win_pos.y + border);
    const ImVec2      bar_max(bar_min.x + theme.left_bar_width, win_pos.y + win_size.y - border);
    const ImU32       clr       = ImGui::GetColorU32(ImVec4(theme.accent.x, theme.accent.y, theme.accent.z, opacity));
    draw_list->AddRectFilled(bar_min, bar_max, clr, theme.window_rounding, ImDrawFlags_RoundCornersLeft);
}

} // namespace

void AICoachOverlay::show_card(AICoachCard card)
{
    m_card          = std::move(card);
    m_has_card      = true;
    m_dismissing    = false;
    m_anim_opacity  = 0.f;
    m_anim_offset_y = 20.f;
    m_show_start_ms = GLCanvas3D::timestamp_now();
    m_next_render_ms = m_show_start_ms;
    m_clicked_button = -1;
}

void AICoachOverlay::dismiss()
{
    if (!m_has_card)
        return;
    m_dismissing    = true;
    m_show_start_ms = GLCanvas3D::timestamp_now();
}

bool AICoachOverlay::needs_frame() const
{
    if (!m_has_card)
        return false;
    if (m_anim_opacity < 1.f || m_dismissing)
        return true;
    return GLCanvas3D::timestamp_now() < m_next_render_ms;
}

bool AICoachOverlay::update_state(bool paused, int64_t delta_ms)
{
    if (!m_has_card || paused)
        return needs_frame();

    const int64_t now = GLCanvas3D::timestamp_now();
    const int64_t elapsed = now - m_show_start_ms;

    if (m_dismissing) {
        const float t = std::min(1.f, static_cast<float>(elapsed) / kAnimDurationMs);
        m_anim_opacity  = 1.f - t;
        m_anim_offset_y = 20.f * t;
        if (t >= 1.f) {
            m_has_card            = false;
            m_dismissing          = false;
            m_last_window_height  = 0.f;
            return false;
        }
        m_next_render_ms = now + 16;
        return true;
    }

    const float t_in = std::min(1.f, static_cast<float>(elapsed) / kAnimDurationMs);
    m_anim_opacity  = t_in;
    m_anim_offset_y = 20.f * (1.f - t_in);

    if (m_card.auto_dismiss_ms > 0 && m_card.importance != AICoachImportance::Critical) {
        const int64_t dismiss_at = m_show_start_ms + m_card.auto_dismiss_ms;
        if (now >= dismiss_at && m_anim_opacity >= 1.f)
            dismiss();
    }

    m_next_render_ms = now + 200;
    return needs_frame();
}

int AICoachOverlay::poll_clicked_button()
{
    const int b = m_clicked_button;
    m_clicked_button = -1;
    return b;
}

float AICoachOverlay::reserved_bottom_height(float scale) const
{
    if (!m_has_card)
        return 0.f;
    const float card_h = m_last_window_height > 0.f ? m_last_window_height : kDefaultReserveHeight * scale;
    return card_h + kRightGap + m_anim_offset_y + kNotificationGap;
}

void AICoachOverlay::render(GLCanvas3D& canvas, float bottom_margin, float right_margin)
{
    if (!m_has_card)
        return;
    render_card(canvas, bottom_margin, right_margin);
}

void AICoachOverlay::render_card(GLCanvas3D& canvas, float bottom_margin, float right_margin)
{
    ImGuiWrapper* imgui_ptr = wxGetApp().imgui();
    if (!imgui_ptr)
        return;
    ImGuiWrapper& imgui = *imgui_ptr;
    const Size cnv_size = canvas.get_canvas_size();
    const float scale   = canvas.get_scale();
    const float width   = std::min(kCoachWidthMax, std::max(kCoachWidthMin, 350.f * scale));

    const char* win_name = "AICoachCard";
    const bool  dark     = wxGetApp().app_config && wxGetApp().app_config->get("dark_color_mode") == "1";
    const CoachPanelTheme theme = coach_panel_theme(dark, scale);
    const float opacity  = std::max(m_anim_opacity, kMinImGuiAlpha);
    const float pad_left = theme.left_bar_width + 12.f;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, theme.window_rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad_left, 10.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, theme.border_size);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.f, 6.f));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, opacity);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.window_bg);
    ImGui::PushStyleColor(ImGuiCol_Border, theme.border);
    ImGui::PushStyleColor(ImGuiCol_Text, theme.text);

    ImGui::SetNextWindowSize(ImVec2(width, 0.f), ImGuiCond_Always);
    const float x = static_cast<float>(cnv_size.get_width()) - right_margin - kRightGap;
    const float y = static_cast<float>(cnv_size.get_height()) - bottom_margin - kRightGap - m_anim_offset_y;
    imgui.set_next_window_pos(x, y, ImGuiCond_Always, 1.f, 1.f);

    const int coach_flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize;
    if (imgui.begin(std::string(win_name), coach_flags)) {
        draw_left_accent_bar(theme, opacity);
        ImGui::PushStyleColor(ImGuiCol_Text, theme.accent);
        imgui.text(m_card.title.empty() ? _u8L("AI Coach") : m_card.title.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
        const float wrap_w = width - 28.f;
        if (!m_card.body.empty()) {
            imgui.text_wrapped(m_card.body, wrap_w);
            ImGui::Spacing();
        }
        for (const std::string& line : m_card.finishing.summary_lines) {
            ImGui::Bullet();
            ImGui::SameLine();
            imgui.text_wrapped(line, wrap_w - 16.f);
        }
        for (const std::string& line : m_card.bullets) {
            ImGui::Bullet();
            ImGui::SameLine();
            imgui.text_wrapped(line, wrap_w - 16.f);
        }
        for (const std::string& line : m_card.sections) {
            if (is_section_heading(line)) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, theme.accent);
                imgui.text_wrapped(line, wrap_w);
                ImGui::PopStyleColor();
            } else if (!line.empty() && line.size() >= 3 && line.compare(0, 3, "☐") == 0) {
                imgui.text_wrapped(line, wrap_w);
            } else {
                imgui.text_wrapped(line, wrap_w);
            }
        }
        if (!m_card.sections.empty() || !m_card.bullets.empty())
            ImGui::Spacing();

        m_clicked_button = -1;
        for (size_t i = 0; i < m_card.buttons.size(); ++i) {
            const AICoachButton& btn = m_card.buttons[i];
            if (!btn.enabled)
                ImGuiWrapper::push_button_disable_style();
            else if (btn.role == AICoachButtonRole::Primary)
                ImGuiWrapper::push_confirm_button_style();
            else
                ImGuiWrapper::push_cancel_button_style();

            if (i > 0)
                ImGui::SameLine();
            if (ImGui::Button(btn.label.c_str())) {
                if (btn.enabled)
                    m_clicked_button = static_cast<int>(i);
            }

            if (!btn.enabled)
                ImGuiWrapper::pop_button_disable_style();
            else if (btn.role == AICoachButtonRole::Primary)
                ImGuiWrapper::pop_confirm_button_style();
            else
                ImGuiWrapper::pop_cancel_button_style();
        }
        m_last_window_height = ImGui::GetWindowSize().y;
    }
    imgui.end();

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(6);
}

}} // namespace

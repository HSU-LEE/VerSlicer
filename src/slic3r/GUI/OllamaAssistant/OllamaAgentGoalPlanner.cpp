#include "OllamaAgentGoalPlanner.hpp"

#include "OllamaAgentStateService.hpp"

#include <boost/algorithm/string.hpp>

namespace Slic3r { namespace GUI {

namespace {

bool contains_utf8(const std::string& s, const char* needle)
{
    return needle && *needle && s.find(needle) != std::string::npos;
}

bool contains_ci(const std::string& hay, const char* needle)
{
    return needle && *needle && boost::ifind_first(hay, needle);
}

bool any_tool_ok(const std::vector<nlohmann::json>& step_tool_results, const std::string& tool,
                 bool require_changed = false)
{
    for (const auto& step : step_tool_results) {
        if (!step.is_array())
            continue;
        for (const auto& r : step) {
            if (!r.is_object() || r.value("tool", "") != tool || !r.value("ok", false))
                continue;
            if (!require_changed || r.value("changed", false))
                return true;
        }
    }
    return false;
}

bool options_contain_key_substr(const nlohmann::json& data, const char* key_substr)
{
    if (!data.is_object() || !data.contains("options") || !data["options"].is_object())
        return false;
    for (auto it = data["options"].begin(); it != data["options"].end(); ++it) {
        if (it.key().find(key_substr) != std::string::npos)
            return true;
    }
    return false;
}

bool set_config_touched_key(const std::vector<nlohmann::json>& step_tool_results, const char* key_substr)
{
    for (const auto& step : step_tool_results) {
        if (!step.is_array())
            continue;
        for (const auto& r : step) {
            if (!r.is_object() || r.value("tool", "") != "set_config" || !r.value("ok", false)
                || !r.value("changed", false))
                continue;
            if (r.contains("data") && options_contain_key_substr(r["data"], key_substr))
                return true;
            const std::string msg = r.value("message", "");
            if (msg.find(key_substr) != std::string::npos)
                return true;
        }
    }
    return false;
}

nlohmann::json make_action_root(std::initializer_list<nlohmann::json> actions, const std::string& message)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& a : actions)
        arr.push_back(a);
    return nlohmann::json{{"message", message}, {"done", false}, {"actions", std::move(arr)}};
}

} // namespace

AgentGoalIntent OllamaAgentGoalPlanner::parse_goal_intent(const std::string& user_goal)
{
    AgentGoalIntent g;
    if (user_goal.empty())
        return g;

    if (contains_utf8(user_goal, "브림") || contains_ci(user_goal, "brim"))
        g.wants_brim = true;
    if (contains_utf8(user_goal, "서포트") || contains_utf8(user_goal, "받침") || contains_ci(user_goal, "support"))
        g.wants_support = true;
    if (contains_utf8(user_goal, "슬라이스") || contains_ci(user_goal, "slice") || contains_ci(user_goal, "slicing"))
        g.wants_slice = true;
    if (contains_utf8(user_goal, "보내") || contains_utf8(user_goal, "전송") || contains_ci(user_goal, "send to")
        || contains_ci(user_goal, "send print"))
        g.wants_send = true;
    if (contains_ci(user_goal, "export") || contains_utf8(user_goal, "gcode") || contains_utf8(user_goal, "g-code")
        || contains_utf8(user_goal, "G-code"))
        g.wants_export = true;
    if (contains_ci(user_goal, "arrange") || contains_utf8(user_goal, "정렬") || contains_utf8(user_goal, "배치"))
        g.wants_arrange = true;

    if ((contains_ci(user_goal, "print") || contains_utf8(user_goal, "출력"))
        && (contains_ci(user_goal, "send") || contains_utf8(user_goal, "프린터") || contains_utf8(user_goal, "보내")))
        g.wants_send = true;

    return g;
}

nlohmann::json OllamaAgentGoalPlanner::build_plan_hint(const std::string& user_goal, bool korean)
{
    const AgentGoalIntent intent = parse_goal_intent(user_goal);
    nlohmann::json        steps  = nlohmann::json::array();

    if (intent.wants_arrange)
        steps.push_back(korean ? "모델 자동 배치 (arrange)" : "Auto-arrange models (arrange)");
    if (intent.wants_brim)
        steps.push_back(korean ? "브림 켜기 (set_config)" : "Enable brim (set_config)");
    if (intent.wants_support)
        steps.push_back(korean ? "서포트 켜기 (set_config)" : "Enable supports (set_config)");
    if (intent.wants_slice)
        steps.push_back(korean ? "슬라이스 (slice)" : "Slice current plate (slice)");
    if (intent.wants_send)
        steps.push_back(korean ? "슬라이스 완료 확인 후 프린터 전송 (get_state → send_print)"
                               : "Verify slice, then send_print");
    if (intent.wants_export)
        steps.push_back(korean ? "슬라이스 완료 후 G-code보내기 (export_gcode)" : "Export G-code after slice");

    return nlohmann::json{
        {"multi_step", steps.size() > 1},
        {"suggested_steps", std::move(steps)},
        {"hint",
         korean ? "여러 단계 목표면 get_state로 확인한 뒤 순서대로 도구를 호출하세요."
                : "For multi-step goals, call get_state between steps and proceed in order."},
    };
}

std::optional<nlohmann::json> OllamaAgentGoalPlanner::try_deterministic_follow_up(
    const std::string& user_goal, const std::vector<nlohmann::json>& step_tool_results)
{
    const AgentGoalIntent intent = parse_goal_intent(user_goal);
    if (!intent.wants_slice && !intent.wants_send && !intent.wants_export && !intent.wants_arrange)
        return std::nullopt;

    const bool brim_ready =
        !intent.wants_brim || set_config_touched_key(step_tool_results, "brim")
        || set_config_touched_key(step_tool_results, "enable_brim");
    const bool support_ready =
        !intent.wants_support || set_config_touched_key(step_tool_results, "support")
        || set_config_touched_key(step_tool_results, "enable_support");
    const bool config_ready = brim_ready && support_ready;

    const bool slice_started = any_tool_ok(step_tool_results, "slice", true);
    const bool send_done     = any_tool_ok(step_tool_results, "send_print", true);
    const bool export_done   = any_tool_ok(step_tool_results, "export_gcode", true);
    const bool arrange_done  = any_tool_ok(step_tool_results, "arrange", true);

    const nlohmann::json state            = OllamaAgentStateService::snapshot();
    const bool           plate_sliced     = state.contains("slice_state")
        && state["slice_state"].value("current_plate_sliced", false);

    if (intent.wants_arrange && !arrange_done)
        return make_action_root({nlohmann::json{{"type", "arrange"}}}, "Auto-arranging models");

    if (intent.wants_support && !support_ready)
        return make_action_root({nlohmann::json{{"type", "set_config"},
                                                {"preset", "print"},
                                                {"options",
                                                 {{"enable_support", true}, {"support_type", "tree(auto)"}}}}},
                                "Enabling tree supports");

    if (config_ready && intent.wants_slice && !slice_started)
        return make_action_root({nlohmann::json{{"type", "slice"}, {"scope", "plate"}}}, "Slicing current plate");

    if (intent.wants_send && !send_done) {
        if (plate_sliced)
            return make_action_root({nlohmann::json{{"type", "send_print"}}}, "Sending to printer");
        if (slice_started && !any_tool_ok(step_tool_results, "get_state"))
            return make_action_root({nlohmann::json{{"type", "get_state"}}}, "Checking slice state");
    }

    if (intent.wants_export && !export_done) {
        if (plate_sliced)
            return make_action_root({nlohmann::json{{"type", "export_gcode"}}}, "Exporting G-code");
        if (slice_started && !any_tool_ok(step_tool_results, "get_state"))
            return make_action_root({nlohmann::json{{"type", "get_state"}}}, "Checking slice state");
    }

    return std::nullopt;
}

}} // namespace

#include "OllamaAgentGoalPlanner.hpp"

#include "OllamaRequestRouter.hpp"
#include "OllamaSettingSearch.hpp"
#include "OllamaUserFlow.hpp"

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

bool tool_applied(const std::vector<nlohmann::json>& step_tool_results, const char* tool)
{
    for (const auto& step : step_tool_results) {
        if (!step.is_array())
            continue;
        for (const auto& r : step) {
            if (!r.is_object())
                continue;
            if (r.value("tool", "") == tool && r.value("ok", false) && r.value("changed", false))
                return true;
        }
    }
    return false;
}

} // namespace

GeometryGoalNeeds OllamaAgentGoalPlanner::parse_geometry_needs(const std::string& user_goal)
{
    GeometryGoalNeeds needs;
    if (user_goal.empty())
        return needs;

    if (contains_utf8(user_goal, "돌려") || contains_utf8(user_goal, "회전") || contains_utf8(user_goal, "뒤집")
        || contains_ci(user_goal, "rotate") || contains_ci(user_goal, "flip"))
        needs.rotate = true;

    if (contains_utf8(user_goal, "크기") || contains_utf8(user_goal, "줄") || contains_utf8(user_goal, "늘")
        || contains_utf8(user_goal, "축소") || contains_utf8(user_goal, "확대") || contains_utf8(user_goal, "키워")
        || user_goal.find('%') != std::string::npos || contains_ci(user_goal, "scale")
        || contains_ci(user_goal, "resize") || contains_ci(user_goal, "bigger") || contains_ci(user_goal, "smaller"))
        needs.scale = true;

    if (contains_utf8(user_goal, "이동") || contains_ci(user_goal, "translate") || contains_ci(user_goal, "move"))
        needs.translate = true;

    if ((contains_utf8(user_goal, "객체") || contains_utf8(user_goal, "물체 객체"))
        && (contains_utf8(user_goal, "배치") || contains_utf8(user_goal, "재배치")))
        needs.arrange_objects = true;
    else if (contains_ci(user_goal, "by object") || contains_ci(user_goal, "by-object")
             || contains_ci(user_goal, "print by object"))
        needs.arrange_objects = true;
    else if (contains_ci(user_goal, "arrange") || contains_utf8(user_goal, "정렬") || contains_utf8(user_goal, "배치"))
        needs.arrange = true;

    if (contains_utf8(user_goal, "분할") || contains_utf8(user_goal, "나누") || contains_ci(user_goal, "split"))
        needs.split_object = true;

    if ((contains_utf8(user_goal, "팔레트") || contains_utf8(user_goal, "플레이트"))
        && (contains_utf8(user_goal, "추가") || contains_utf8(user_goal, "새")))
        needs.add_plate = true;
    else if (contains_ci(user_goal, "add plate") || contains_ci(user_goal, "new plate")
             || contains_ci(user_goal, "add palette") || contains_ci(user_goal, "new palette"))
        needs.add_plate = true;

    if (contains_utf8(user_goal, "삭제") || contains_ci(user_goal, "delete"))
        needs.delete_selection = true;

    return needs;
}

bool OllamaAgentGoalPlanner::geometry_needs_met(const GeometryGoalNeeds& needs,
                                                const std::vector<nlohmann::json>& step_tool_results)
{
    if (!needs.any())
        return false;
    if (needs.rotate && !tool_applied(step_tool_results, "rotate"))
        return false;
    if (needs.scale && !tool_applied(step_tool_results, "scale"))
        return false;
    if (needs.translate && !tool_applied(step_tool_results, "translate"))
        return false;
    if (needs.arrange_objects && !tool_applied(step_tool_results, "arrange_objects"))
        return false;
    if (needs.arrange && !tool_applied(step_tool_results, "arrange"))
        return false;
    if (needs.split_object && !tool_applied(step_tool_results, "split_object"))
        return false;
    if (needs.add_plate && !tool_applied(step_tool_results, "add_plate"))
        return false;
    if (needs.delete_selection && !tool_applied(step_tool_results, "delete_selection"))
        return false;
    return true;
}

bool OllamaAgentGoalPlanner::is_simple_geometry_apply(const std::string& user_goal)
{
    return OllamaRequestRouter::is_geometry_request(user_goal);
}

bool OllamaAgentGoalPlanner::is_single_shot_apply(const std::string& user_goal)
{
    return !goal_expects_multi_step(user_goal);
}

bool OllamaAgentGoalPlanner::goal_expects_multi_step(const std::string& user_goal)
{
    // Find-and-print (MakerWorld search → import → slice) must not auto-finish
    // after a single set_config or a bare done:true from the model.
    return OllamaUserFlow::is_acquisition_print_request(user_goal, /*plate_has_model=*/false);
}

bool OllamaAgentGoalPlanner::should_auto_finish_after_apply(const std::string& user_goal, bool applied,
                                                            const std::vector<nlohmann::json>& step_tool_results)
{
    if (!applied)
        return false;
    const GeometryGoalNeeds needs = parse_geometry_needs(user_goal);
    if (needs.any())
        return geometry_needs_met(needs, step_tool_results);
    return !goal_expects_multi_step(user_goal);
}

nlohmann::json OllamaAgentGoalPlanner::build_plan_hint(const std::string& user_goal, bool korean)
{
    nlohmann::json hint{
        {"multi_step", false},
        {"suggested_steps",
         nlohmann::json::array({korean ? "요청에 맞는 변경을 확인하고 적용합니다"
                                       : "Review context and apply the needed changes"})},
        {"hint",
         korean ? "슬라이서 컨텍스트와 candidate_keys를 참고해 필요한 actions를 실행하고 done:true로 마무리하세요."
                : "Use slicer context and candidate_keys; run the needed actions, then set done:true."},
    };
    const auto keys = OllamaSettingSearch::candidate_keys_for_request(user_goal, 3, 8);
    if (!keys.empty())
        hint["candidate_keys"] = keys;

    const GeometryGoalNeeds needs = parse_geometry_needs(user_goal);
    nlohmann::json            tools = nlohmann::json::array();
    if (needs.add_plate)
        tools.push_back("add_plate");
    if (needs.arrange_objects)
        tools.push_back("arrange_objects");
    else if (needs.arrange)
        tools.push_back("arrange");
    if (needs.split_object)
        tools.push_back("split_object");
    if (needs.scale)
        tools.push_back("scale");
    if (needs.rotate)
        tools.push_back("rotate");
    if (!tools.empty())
        hint["suggested_tools"] = std::move(tools);

    return hint;
}

}} // namespace

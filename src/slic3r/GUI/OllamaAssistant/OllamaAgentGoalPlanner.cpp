#include "OllamaAgentGoalPlanner.hpp"

namespace Slic3r { namespace GUI {

bool OllamaAgentGoalPlanner::is_single_shot_apply(const std::string& user_goal)
{
    return !goal_expects_multi_step(user_goal);
}

bool OllamaAgentGoalPlanner::goal_expects_multi_step(const std::string& /*user_goal*/)
{
    return false;
}

bool OllamaAgentGoalPlanner::should_auto_finish_after_apply(const std::string& /*user_goal*/, bool /*applied*/,
                                                            const std::vector<nlohmann::json>& /*step_tool_results*/)
{
    return false;
}

nlohmann::json OllamaAgentGoalPlanner::build_plan_hint(const std::string& /*user_goal*/, bool korean)
{
    return {
        {"multi_step", false},
        {"suggested_steps",
         nlohmann::json::array({korean ? "요청에 맞는 변경을 확인하고 적용합니다"
                                       : "Review context and apply the needed changes"})},
        {"hint",
         korean ? "상태를 읽고 필요한 도구를 직접 고르세요. 결과가 목표를 충족하면 done:true."
                : "Read state and choose tools yourself. Set done:true when the results meet the goal."},
    };
}

}} // namespace

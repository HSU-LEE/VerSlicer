#include "AssistantAgentGoalPlanner.hpp"
#include "AiCatalog.hpp"

namespace Slic3r { namespace GUI {

bool AssistantAgentGoalPlanner::is_single_shot_apply(const std::string& user_goal)
{
    return !goal_expects_multi_step(user_goal);
}

bool AssistantAgentGoalPlanner::goal_expects_multi_step(const std::string& /*user_goal*/)
{
    return false;
}

bool AssistantAgentGoalPlanner::should_auto_finish_after_apply(const std::string& /*user_goal*/, bool /*applied*/,
                                                            const std::vector<nlohmann::json>& /*step_tool_results*/)
{
    return false;
}

nlohmann::json AssistantAgentGoalPlanner::build_plan_hint(const std::string& /*user_goal*/, bool korean)
{
    return {
        {"multi_step", false},
        {"suggested_steps",
         nlohmann::json::array({ai_catalog_text("Review context and apply the needed changes")})},
        {"hint",
         ai_catalog_text("Read state and choose tools yourself. Set done:true when the results meet the goal.")},
    };
}

}} // namespace

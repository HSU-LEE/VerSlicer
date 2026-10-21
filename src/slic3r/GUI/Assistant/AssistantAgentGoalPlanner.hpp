#ifndef slic3r_AssistantAgentGoalPlanner_hpp_
#define slic3r_AssistantAgentGoalPlanner_hpp_

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

/** Hint and finish policy for the assist loop. The model chooses tools. */
class AssistantAgentGoalPlanner
{
public:
    static nlohmann::json build_plan_hint(const std::string& user_goal, bool korean);

    /** True when the chat panel should attach a plan hint before a single LLM turn. */
    static bool is_single_shot_apply(const std::string& user_goal);

    /** Kept for tests. The loop does not finish from a sentence match. */
    static bool goal_expects_multi_step(const std::string& user_goal);

    /** A successful tool does not end the loop. The model sets done:true. */
    static bool should_auto_finish_after_apply(const std::string& user_goal, bool applied,
                                               const std::vector<nlohmann::json>& step_tool_results);
};

}} // namespace

#endif

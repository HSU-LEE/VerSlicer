#ifndef slic3r_OllamaAgentGoalPlanner_hpp_
#define slic3r_OllamaAgentGoalPlanner_hpp_

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

struct AgentGoalIntent
{
    bool wants_brim{false};
    bool wants_support{false};
    bool wants_slice{false};
    bool wants_send{false};
    bool wants_export{false};
    bool wants_arrange{false};
};

/** Parse multi-step goals (brim → slice → send, etc.) for the agent loop. */
class OllamaAgentGoalPlanner
{
public:
    static AgentGoalIntent parse_goal_intent(const std::string& user_goal);

    /** Hint JSON injected into the agent's first user message. */
    static nlohmann::json build_plan_hint(const std::string& user_goal, bool korean);

    /**
     * When the goal implies a fixed sequence, return the next actions without waiting for the LLM.
     * Uses live slice_state from OllamaAgentStateService when deciding send/export.
     */
    static std::optional<nlohmann::json> try_deterministic_follow_up(
        const std::string& user_goal, const std::vector<nlohmann::json>& step_tool_results);
};

}} // namespace

#endif

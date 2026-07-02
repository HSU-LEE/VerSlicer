#ifndef slic3r_OllamaAgentGoalPlanner_hpp_
#define slic3r_OllamaAgentGoalPlanner_hpp_

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

/** Parsed rotate/scale/translate needs for simple geometry goals. */
struct GeometryGoalNeeds
{
    bool rotate{false};
    bool scale{false};
    bool translate{false};
    bool arrange{false};
    bool arrange_objects{false};
    bool split_object{false};
    bool add_plate{false};
    bool delete_selection{false};

    bool any() const
    {
        return rotate || scale || translate || arrange || arrange_objects || split_object || add_plate
            || delete_selection;
    }
};

/** Parse multi-step goals (brim → slice → send, etc.) for the agent loop. */
class OllamaAgentGoalPlanner
{
public:
    /** Hint JSON injected into the agent's first user message. */
    static nlohmann::json build_plan_hint(const std::string& user_goal, bool korean);

    static GeometryGoalNeeds parse_geometry_needs(const std::string& user_goal);

    /** True when every requested geometry op succeeded in the agent run so far. */
    static bool geometry_needs_met(const GeometryGoalNeeds& needs,
                                   const std::vector<nlohmann::json>& step_tool_results);

    /** Rotate/scale/translate only — one LLM turn is enough (no multi-step loop). */
    static bool is_simple_geometry_apply(const std::string& user_goal);

    /** Direct apply in one LLM turn: geometry, mesh repair, mesh edit (hole/handle/mirror). */
    static bool is_single_shot_apply(const std::string& user_goal);

    /** Slice/send/export, functional design — needs agent loop. */
    static bool goal_expects_multi_step(const std::string& user_goal);

    /** True when a successful apply should end the agent loop (no further LLM steps). */
    static bool should_auto_finish_after_apply(const std::string& user_goal, bool applied,
                                               const std::vector<nlohmann::json>& step_tool_results);
};

}} // namespace

#endif

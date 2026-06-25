#include "OllamaAssistRouter.hpp"

#include "OllamaAgentGoalPlanner.hpp"
#include "OllamaConfig.hpp"
#include "OllamaDiagnosticPipeline.hpp"
#include "OllamaRequestRouter.hpp"

#include "../MakerWorld/MakerWorldSearchService.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI {

bool OllamaAssistRouter::should_use_assist_loop(const std::string& user_request, bool apply_mode)
{
    if (!apply_mode || user_request.empty() || !ollama_assist_loop_enabled())
        return false;
    if (MakerWorldSearchService::is_pure_makerworld_request(user_request))
        return false;
    if (OllamaDiagnosticPipeline::needs_pipeline(user_request, apply_mode))
        return false;
    if (should_use_two_hop(user_request, apply_mode))
        return false;

    const AgentGoalIntent intent = OllamaAgentGoalPlanner::parse_goal_intent(user_request);
    int                   flags  = 0;
    if (intent.wants_slice)
        ++flags;
    if (intent.wants_send)
        ++flags;
    if (intent.wants_export)
        ++flags;
    if (intent.wants_arrange)
        ++flags;
    if (flags >= 1 && (intent.wants_brim || intent.wants_support))
        return true;
    if (flags >= 2)
        return true;
    if (intent.wants_slice || intent.wants_send || intent.wants_export)
        return true;

    const nlohmann::json hint = OllamaAgentGoalPlanner::build_plan_hint(user_request, false);
    if (hint.value("multi_step", false))
        return true;

    if (OllamaRequestRouter::is_geometry_request(user_request)
        && OllamaRequestRouter::classify(user_request) == OllamaRequestRoute::Fast)
        return false;

    return false;
}

bool OllamaAssistRouter::should_use_two_hop(const std::string& user_request, bool apply_mode)
{
    if (!apply_mode || user_request.empty())
        return false;
    if (MakerWorldSearchService::is_pure_makerworld_request(user_request))
        return false;
    if (OllamaRequestRouter::is_geometry_request(user_request))
        return false;

    const OllamaRequestRoute route = OllamaRequestRouter::classify(user_request);
    if (route == OllamaRequestRoute::Fast)
        return false;
    if (route == OllamaRequestRoute::Deep)
        return ollama_two_hop_enabled() && !ollama_adaptive_routing_enabled();

    if (ollama_two_hop_enabled())
        return true;
    return ollama_adaptive_routing_enabled() && route == OllamaRequestRoute::Standard;
}

}} // namespace

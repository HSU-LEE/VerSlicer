#include "OllamaAssistContextBuilder.hpp"

#include "OllamaActionExecutor.hpp"
#include "OllamaAgentStateService.hpp"
#include "OllamaMeshOps.hpp"

#ifndef OLLAMA_HEADLESS_TEST
#include "../GUI_App.hpp"
#include "../Plater.hpp"
#endif

namespace Slic3r { namespace GUI {

namespace {

nlohmann::json parse_context_json(const std::string& raw)
{
    try {
        return nlohmann::json::parse(raw);
    } catch (...) {
        return nlohmann::json::object();
    }
}

} // namespace

OllamaAssistContextBuilder::PrefetchBundle OllamaAssistContextBuilder::prefetch_for_goal(const std::string& user_goal,
                                                                                        bool korean)
{
    PrefetchBundle out;

    (void) user_goal;
    (void) korean;

#ifndef OLLAMA_HEADLESS_TEST
    if (Plater* plater = wxGetApp().plater()) {
        out.mesh_health  = OllamaMeshOps::mesh_health_for_plate(plater);
        out.mesh_summary = OllamaMeshOps::summarize_mesh_health(out.mesh_health);
    }
#endif

    return out;
}

std::string OllamaAssistContextBuilder::build_initial_user_block(const std::string& user_goal,
                                                                 const nlohmann::json& plan_hint,
                                                                 const PrefetchBundle& prefetch, bool korean)
{
    nlohmann::json slicer_context = parse_context_json(OllamaActionExecutor::build_context_json());

    nlohmann::json block = {
        {"goal", user_goal},
        {"config_digest", OllamaAgentStateService::config_digest(user_goal)},
    };

    if (slicer_context.is_object()) {
        auto intent = slicer_context.find("print_intent");
        if (intent != slicer_context.end())
            slicer_context.erase(intent);
        auto proposal = slicer_context.find("config_proposal");
        if (proposal != slicer_context.end())
            slicer_context.erase(proposal);
        auto geom = slicer_context.find("geometry_assessment");
        if (geom != slicer_context.end()) {
            block["geometry_assessment"] = *geom;
            slicer_context.erase(geom);
        }
    }
    block["slicer_context"] = std::move(slicer_context);
    if (prefetch.mesh_health.is_array() && !prefetch.mesh_health.empty())
        block["mesh_health"] = prefetch.mesh_health;
    if (prefetch.mesh_summary.is_object() && !prefetch.mesh_summary.empty())
        block["mesh_summary"] = prefetch.mesh_summary;

    (void) plan_hint;
    const std::string header =
        "Goal and current slicer state. Do not follow a script. Read the state, then choose tools. "
        "Batch independent changes in one reply. Set done:true when the goal is met.\n\n";

    std::string body = block.dump(2);
    body             = OllamaActionExecutor::fit_context_json_to_limit(std::move(body), 48000);
    return header + body;
}

}} // namespace

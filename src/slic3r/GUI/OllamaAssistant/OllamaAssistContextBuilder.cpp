#include "OllamaAssistContextBuilder.hpp"

#include "BambuLabWikiSearch.hpp"
#include "OllamaActionExecutor.hpp"
#include "OllamaAgentStateService.hpp"
#include "OllamaConfig.hpp"
#include "OllamaDiagnosticPipeline.hpp"
#include "OllamaMeshOps.hpp"
#include "OllamaPrintingTips.hpp"
#include "OllamaRequestRouter.hpp"
#include "OllamaSettingSearch.hpp"

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

bool should_prefetch_wiki(const std::string& user_goal)
{
    if (!ollama_wiki_search_enabled())
        return false;
    return OllamaRequestRouter::benefits_from_wiki(user_goal);
}

bool user_wants_slice_workflow(const std::string& user_goal)
{
    return user_goal.find("슬라이스") != std::string::npos || user_goal.find("slice") != std::string::npos
        || user_goal.find("slicing") != std::string::npos || user_goal.find("gcode") != std::string::npos
        || user_goal.find("g-code") != std::string::npos || user_goal.find("G-code") != std::string::npos
        || user_goal.find("export") != std::string::npos || user_goal.find("보내") != std::string::npos
        || user_goal.find("전송") != std::string::npos || user_goal.find("send to") != std::string::npos
        || user_goal.find("send print") != std::string::npos || user_goal.find("프린터") != std::string::npos;
}

} // namespace

OllamaAssistContextBuilder::PrefetchBundle OllamaAssistContextBuilder::prefetch_for_goal(const std::string& user_goal,
                                                                                        bool korean)
{
    PrefetchBundle out;

    std::vector<std::string> keys = OllamaSettingSearch::candidate_keys_for_request(user_goal, 3, 10);
    if (!keys.empty()) {
        OllamaDiagnosis pseudo;
        pseudo.symptom     = user_goal;
        pseudo.diagnosis   = user_goal;
        pseudo.wiki_queries.push_back(user_goal);
        out.settings_analysis = OllamaDiagnosticPipeline::analyze_current_settings(keys, pseudo, korean);
        out.candidate_keys    = std::move(keys);
    }

    // Wiki evidence is deliberately not fetched here: it is sync HTTP with long
    // timeouts and this function runs on the main thread. Callers that want it
    // run fetch_wiki_evidence() on a worker (see OllamaAgentController::run_goal).

#ifndef OLLAMA_HEADLESS_TEST
    if (Plater* plater = wxGetApp().plater()) {
        out.mesh_health  = OllamaMeshOps::mesh_health_for_plate(plater);
        out.mesh_summary = OllamaMeshOps::summarize_mesh_health(out.mesh_health);
    }
#endif

    return out;
}

bool OllamaAssistContextBuilder::wants_wiki_prefetch(const std::string& user_goal)
{
    return should_prefetch_wiki(user_goal);
}

nlohmann::json OllamaAssistContextBuilder::fetch_wiki_evidence(const std::string& user_goal, bool korean)
{
    if (!should_prefetch_wiki(user_goal))
        return nlohmann::json::array();
    OllamaDiagnosis pseudo;
    pseudo.symptom   = user_goal;
    pseudo.diagnosis = user_goal;
    pseudo.wiki_queries.push_back(user_goal);
    return OllamaDiagnosticPipeline::build_wiki_evidence(pseudo, user_goal, korean);
}

std::string OllamaAssistContextBuilder::build_initial_user_block(const std::string& user_goal,
                                                                 const nlohmann::json& plan_hint,
                                                                 const PrefetchBundle& prefetch, bool korean)
{
    nlohmann::json slicer_context = parse_context_json(OllamaActionExecutor::build_context_json());

    nlohmann::json block = {
        {"goal", user_goal},
        {"pro_tips", OllamaPrintingTips::tips_for_request(user_goal, korean)},
        {"config_digest", OllamaAgentStateService::config_digest(user_goal)},
        {"plan_hint", plan_hint},
    };

    // Surface the deterministic print intent, geometry assessment, and config proposal at the
    // top level so the LLM treats them as the baseline to refine. They are computed once by the
    // executor context; hoist (move) them out of slicer_context to avoid duplicating payload.
    if (slicer_context.is_object()) {
        for (const char* key : {"print_intent", "geometry_assessment", "config_proposal"}) {
            auto it = slicer_context.find(key);
            if (it != slicer_context.end()) {
                block[key] = *it;
                slicer_context.erase(it);
            }
        }
    }
    block["slicer_context"] = std::move(slicer_context);
    if (!prefetch.candidate_keys.empty())
        block["candidate_keys"] = prefetch.candidate_keys;
    if (!prefetch.wiki.empty())
        block["wiki_evidence"] = prefetch.wiki;
    if (!prefetch.settings_analysis.empty())
        block["settings_analysis"] = prefetch.settings_analysis;
    if (prefetch.mesh_health.is_array() && !prefetch.mesh_health.empty())
        block["mesh_health"] = prefetch.mesh_health;
    if (prefetch.mesh_summary.is_object() && !prefetch.mesh_summary.empty())
        block["mesh_summary"] = prefetch.mesh_summary;

    if (prefetch.mesh_summary.value("plate_needs_repair", false)) {
        block["mesh_repair_recommended"] = true;
        if (user_wants_slice_workflow(user_goal)) {
            block["repair_before_slice"] = true;
            block["mesh_hint"] =
                korean ? "mesh_health에 결함이 있습니다. slice 전에 repair_mesh를 실행하세요."
                       : "mesh_health shows defects — run repair_mesh before slice.";
        } else {
            block["mesh_hint"] =
                korean ? "가져온 모델에 메쉬 결함이 있습니다. 슬라이스 전 repair_mesh를 권장합니다."
                       : "Imported model has mesh defects; recommend repair_mesh before slicing.";
        }
    } else if (prefetch.mesh_summary.value("volume_count", 0) > 0) {
        block["mesh_hint"] = korean ? "mesh_health: 모델이 manifold이며 슬라이스 준비가 되었습니다."
                                    : "mesh_health: models are manifold and ready to slice.";
    }

    const std::string header =
        korean
            ? "목표와 슬라이서 컨텍스트입니다. plan_hint를 참고해 필요한 actions를 실행하세요. "
              "독립적인 변경(회전+크기 등)은 한 번에 묶어 실행하고, 목표 달성 시 done:true로 마무리하세요.\n\n"
            : "Goal and slicer context. Follow plan_hint and run the needed actions. "
              "Batch independent changes (rotate + scale, etc.) in one reply; set done:true when finished.\n\n";

    std::string body = block.dump(2);
    body             = OllamaActionExecutor::fit_context_json_to_limit(std::move(body), 12000);
    return header + body;
}

}} // namespace

#include "OllamaAssistContextBuilder.hpp"

#include "BambuLabWikiSearch.hpp"
#include "OllamaAgentStateService.hpp"
#include "OllamaConfig.hpp"
#include "OllamaDiagnosticPipeline.hpp"
#include "OllamaRequestRouter.hpp"
#include "OllamaSettingSearch.hpp"

namespace Slic3r { namespace GUI {

OllamaAssistContextBuilder::PrefetchBundle OllamaAssistContextBuilder::prefetch_for_goal(const std::string& user_goal,
                                                                                        bool korean)
{
    PrefetchBundle out;
    if (!ollama_wiki_search_enabled() || !OllamaRequestRouter::benefits_from_wiki(user_goal))
        return out;

    OllamaDiagnosis pseudo;
    pseudo.symptom     = user_goal;
    pseudo.diagnosis   = user_goal;
    pseudo.wiki_queries.push_back(user_goal);
    out.wiki = OllamaDiagnosticPipeline::build_wiki_evidence(pseudo, user_goal, korean);

    std::vector<std::string> keys = OllamaSettingSearch::candidate_keys_for_request(user_goal, 3, 10);
    if (!keys.empty())
        out.settings_analysis = OllamaDiagnosticPipeline::analyze_current_settings(keys, pseudo, korean);

    return out;
}

std::string OllamaAssistContextBuilder::build_initial_user_block(const std::string& user_goal,
                                                                 const nlohmann::json& plan_hint,
                                                                 const PrefetchBundle& prefetch, bool korean)
{
    nlohmann::json block = {
        {"goal", user_goal},
        {"state", OllamaAgentStateService::snapshot()},
        {"config_digest", OllamaAgentStateService::config_digest(user_goal)},
        {"plan_hint", plan_hint},
    };
    if (!prefetch.wiki.empty())
        block["wiki_evidence"] = prefetch.wiki;
    if (!prefetch.settings_analysis.empty())
        block["settings_analysis"] = prefetch.settings_analysis;

    const std::string header =
        korean ? "목표와 초기 컨텍스트입니다. get_state로 확인한 뒤 필요한 actions를 실행하세요.\n\n"
               : "Goal and initial context. Verify with get_state, then execute needed actions.\n\n";
    return header + block.dump(2);
}

}} // namespace

#ifndef slic3r_OllamaAssistContextBuilder_hpp_
#define slic3r_OllamaAssistContextBuilder_hpp_

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

/** Goal-aware context prefetch for the assist loop (no UI). */
class OllamaAssistContextBuilder
{
public:
    struct PrefetchBundle
    {
        nlohmann::json              wiki;
        nlohmann::json              settings_analysis;
        nlohmann::json              mesh_health;
        nlohmann::json              mesh_summary;
        std::vector<std::string>    candidate_keys;
    };

    /** Local (no-network) prefetch: settings analysis, candidate keys, mesh
     *  health. Main thread only (reads Plater / preset state). Wiki evidence is
     *  intentionally NOT fetched here — see fetch_wiki_evidence(). */
    static PrefetchBundle prefetch_for_goal(const std::string& user_goal, bool korean);

    /** True when the goal benefits from Bambu Wiki evidence (and the feature is on). */
    static bool wants_wiki_prefetch(const std::string& user_goal);

    /** Blocking wiki fetch (sync HTTP, up to tens of seconds). Call on a WORKER
     *  thread; returns an empty array when the fetch is disabled or fails. */
    static nlohmann::json fetch_wiki_evidence(const std::string& user_goal, bool korean);

    static std::string build_initial_user_block(const std::string& user_goal, const nlohmann::json& plan_hint,
                                                const PrefetchBundle& prefetch, bool korean);
};

}} // namespace

#endif

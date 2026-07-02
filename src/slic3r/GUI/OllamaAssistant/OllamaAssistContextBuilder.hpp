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

    static PrefetchBundle prefetch_for_goal(const std::string& user_goal, bool korean);

    static std::string build_initial_user_block(const std::string& user_goal, const nlohmann::json& plan_hint,
                                                const PrefetchBundle& prefetch, bool korean);
};

}} // namespace

#endif

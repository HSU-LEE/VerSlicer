#ifndef slic3r_OllamaActionCritic_hpp_
#define slic3r_OllamaActionCritic_hpp_

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

enum class OllamaCriticVerdict
{
    Approve,
    Revise,
    Block,
};

struct OllamaCriticResult
{
    OllamaCriticVerdict           verdict{OllamaCriticVerdict::Approve};
    std::string                   message;
    std::vector<std::string>      suggested_keys;
    std::vector<std::string>      warnings;
};

/** Phase B rule critic: validate proposed actions against symptoms + wiki hints. */
class OllamaActionCritic
{
public:
    static OllamaCriticResult review(const nlohmann::json& root, const std::string& user_request,
                                     const nlohmann::json& wiki_context);
};

}} // namespace

#endif

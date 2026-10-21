#ifndef slic3r_AssistantActionCritic_hpp_
#define slic3r_AssistantActionCritic_hpp_

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

enum class AssistantCriticVerdict
{
    Approve,
    Revise,
    Block,
};

struct AssistantCriticResult
{
    AssistantCriticVerdict           verdict{AssistantCriticVerdict::Approve};
    std::string                   message;
    std::vector<std::string>      suggested_keys;
    std::vector<std::string>      warnings;
};

/** Phase B rule critic: validate proposed actions against symptoms + wiki hints. */
class AssistantActionCritic
{
public:
    static AssistantCriticResult review(const nlohmann::json& root, const std::string& user_request,
                                     const nlohmann::json& wiki_context);
};

}} // namespace

#endif

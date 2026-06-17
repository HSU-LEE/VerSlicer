#ifndef slic3r_OllamaActionPipelineCore_hpp_
#define slic3r_OllamaActionPipelineCore_hpp_

#include <nlohmann/json.hpp>

#include <string>

namespace Slic3r { namespace GUI {

/** Pure helpers shared by the action pipeline and dev-utils tests (no GUI deps). */
class OllamaActionPipelineCore
{
public:
    static std::string action_fingerprint(const nlohmann::json& action);
    static void        dedupe_actions_in_turn(nlohmann::json& root);
};

}} // namespace

#endif

#ifndef slic3r_AssistantActionValidator_hpp_
#define slic3r_AssistantActionValidator_hpp_

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI {

struct AssistantActionSanitizeResult
{
    int                 blocked_count{0};
    std::vector<std::string> warnings;
};

/** Filter and clamp model-proposed actions before execution. */
class AssistantActionValidator
{
public:
    static constexpr size_t kMaxActionsPerTurn = 32;

    /** Map common LLM type aliases to canonical action ids (no user-intent rules). */
    static std::string canonical_action_type(std::string type);

    static AssistantActionSanitizeResult sanitize(nlohmann::json& root, const std::string& user_request);

    static bool is_allowed_config_key(const std::string& key);
};

}} // namespace

#endif

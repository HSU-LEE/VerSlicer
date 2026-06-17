#ifndef slic3r_OllamaActionValidator_hpp_
#define slic3r_OllamaActionValidator_hpp_

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI {

struct OllamaActionSanitizeResult
{
    int                 blocked_count{0};
    std::vector<std::string> warnings;
};

/** Filter and clamp model-proposed actions before execution. */
class OllamaActionValidator
{
public:
    static constexpr size_t kMaxActionsPerTurn = 12;

    static OllamaActionSanitizeResult sanitize(nlohmann::json& root, const std::string& user_request);

    static bool is_allowed_config_key(const std::string& key);
};

}} // namespace

#endif

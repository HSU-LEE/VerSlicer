#ifndef slic3r_OllamaAgentStateService_hpp_
#define slic3r_OllamaAgentStateService_hpp_

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

/** Machine-readable slicer snapshot for agent get_state tool and verifier. */
class OllamaAgentStateService
{
public:
    static nlohmann::json snapshot();
    static std::string    snapshot_json();

    /** Compact print-setting values for agent context (goal-aware key pick). */
    static nlohmann::json config_digest(const std::string& user_goal_hint = {});

    /** Compare key fields after set_config; returns mismatched keys. */
    static std::vector<std::string> verify_config_applied(const nlohmann::json& expected_options,
                                                          const std::string&    preset = "print");
};

}} // namespace

#endif

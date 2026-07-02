#ifndef slic3r_OllamaAgentStateService_hpp_
#define slic3r_OllamaAgentStateService_hpp_

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

/** Post-apply verification for set_config actions in an agent turn. */
struct OllamaConfigVerifyReport
{
    bool                     all_ok{true};
    std::vector<std::string> mismatches;
    nlohmann::json           tool_results{nlohmann::json::array()};
    nlohmann::json           config_digest;
};

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

    /** Verify every set_config in root; emits verify_config tool rows + config_digest. */
    static OllamaConfigVerifyReport verify_set_config_actions(const nlohmann::json& root,
                                                              const std::string&    user_goal_hint = {});

    /** Agent-loop nudge after verify_config mismatch (includes digest when available). */
    static std::string build_verify_retry_nudge(const OllamaConfigVerifyReport& report, bool korean);
};

}} // namespace

#endif

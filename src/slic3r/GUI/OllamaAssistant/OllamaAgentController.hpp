#ifndef slic3r_OllamaAgentController_hpp_
#define slic3r_OllamaAgentController_hpp_

#include "OllamaAgentEventBus.hpp"
#include "OllamaClient.hpp"
#include "OllamaExecutionPolicy.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

class wxString;
class wxWindow;

namespace Slic3r { namespace GUI {

struct OllamaAgentRunResult
{
    bool                             completed{false};
    bool                             cancelled{false};
    bool                             blocked{false};
    std::string                      final_message;
    std::vector<nlohmann::json>      step_tool_results;
    int                              steps_taken{0};
};

struct OllamaAgentCallbacks
{
    std::function<void(const wxString&)>              on_thinking;
    std::function<void(const OllamaAgentRunResult&)>   on_finished;
};

/** Multi-step observe → plan → act loop (Cursor-like agent). */
class OllamaAgentController
{
public:
    static constexpr int kDefaultMaxSteps = 8;

    OllamaAgentController(OllamaClient& client, std::string model);

    void run_goal(const std::string& user_goal, OllamaExecutionPolicy policy, wxWindow* parent,
                  OllamaAgentCallbacks callbacks, int max_steps = kDefaultMaxSteps);

    void cancel();

    void set_model(std::string model);

    bool is_running() const { return m_running; }

    /** Called when slice completes while the agent is waiting. */
    void handle_agent_event(const OllamaAgentEvent& evt);

private:
    void begin_step();
    void on_llm_response(const std::string& text, const std::string& error);
    void finish(OllamaAgentRunResult result);
    bool execute_agent_root(const nlohmann::json& root);
    void run_deterministic_follow_ups();
    bool try_rule_fallback_finish();
    void continue_after_slice(bool slice_ok);
    void proceed_after_tool_execution(const nlohmann::json& executed_root, const std::string& assistant_msg,
                                      const std::string& raw_text);
    bool update_awaiting_slice_from_results();

    OllamaClient&                  m_client;
    std::string                    m_model;
    std::shared_ptr<std::atomic<bool>> m_alive;
    bool                           m_running{false};
    bool                           m_cancelled{false};
    int                            m_step{0};
    int                            m_max_steps{kDefaultMaxSteps};
    OllamaExecutionPolicy          m_policy{OllamaExecutionPolicy::AutoSafe};
    wxWindow*                      m_parent{nullptr};
    OllamaAgentCallbacks           m_callbacks;
    std::string                    m_user_goal;
    std::vector<OllamaMessage>     m_messages;
    std::vector<nlohmann::json>    m_step_tool_results;
    bool                           m_awaiting_slice{false};
    std::string                    m_pending_assistant_msg;
    std::string                    m_pending_raw_text;
    nlohmann::json                 m_pending_executed_root;
};

}} // namespace

#endif

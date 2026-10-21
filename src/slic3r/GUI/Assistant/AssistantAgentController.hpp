#ifndef slic3r_AssistantAgentController_hpp_
#define slic3r_AssistantAgentController_hpp_

#include "AssistantAgentEventBus.hpp"
#include "AssistContextBuilder.hpp"
#include "AssistantClient.hpp"
#include "AssistantExecutionPolicy.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

class wxString;
class wxWindow;

namespace Slic3r { namespace GUI {

struct AssistantWorkflowRun;
struct AssistantPipelineOptions;
struct AssistantConfigVerifyReport;

struct AssistantAgentRunResult
{
    bool                             completed{false};
    bool                             cancelled{false};
    bool                             blocked{false};
    std::string                      final_message;
    std::vector<nlohmann::json>      step_tool_results;
    int                              steps_taken{0};
};

struct AssistantAgentCallbacks
{
    std::function<void(const wxString&)>              on_thinking;
    std::function<void(const AssistantAgentRunResult&)>   on_finished;
};

/** Multi-step observe → plan → act loop (Cursor-like agent). */
class AssistantAgentController
{
public:
    static constexpr int kDefaultMaxSteps = 40;

    AssistantAgentController(AssistantClient& client, std::string model);
    ~AssistantAgentController();

    void run_goal(const std::string& user_goal, AssistantExecutionPolicy policy, wxWindow* parent,
                  AssistantAgentCallbacks callbacks, int max_steps = kDefaultMaxSteps);

    void cancel();

    void set_model(std::string model);

    bool is_running() const { return m_running; }

    /** Called when slice completes while the agent is waiting. */
    void handle_agent_event(const AssistantAgentEvent& evt);

private:
    void begin_step();
    /** Merge the goal into the print-intent session and refresh the deterministic proposal. */
    void refresh_print_intent_and_proposal(bool korean);
    void on_llm_response(const std::string& text, const std::string& error);
    // Extracted body of on_llm_response: action extraction, normalization, and
    // execution — all of which run on the wx main thread. on_llm_response wraps this
    // in a try/catch so an uncaught throw cannot unwind the main loop and quit the app.
    void dispatch_llm_actions(const std::string& text);
    void fail_turn_after_exception();
    void finish(AssistantAgentRunResult result);
    void continue_after_slice(bool slice_ok);
    void proceed_after_tool_execution(const nlohmann::json& executed_root, const std::string& assistant_msg,
                                      const std::string& raw_text);
    bool update_awaiting_slice_from_results();

    AssistantClient&                  m_client;
    std::string                    m_model;
    std::shared_ptr<std::atomic<bool>> m_alive;
    bool                           m_running{false};
    bool                           m_cancelled{false};
    int                            m_step{0};
    int                            m_max_steps{kDefaultMaxSteps};
    AssistantExecutionPolicy          m_policy{AssistantExecutionPolicy::AutoSafe};
    wxWindow*                      m_parent{nullptr};
    AssistantAgentCallbacks           m_callbacks;
    std::string                    m_user_goal;
    std::vector<AssistantMessage>     m_messages;
    std::vector<nlohmann::json>    m_step_tool_results;
    AssistContextBuilder::PrefetchBundle m_prefetch;
    bool                           m_awaiting_slice{false};
    std::string                    m_pending_assistant_msg;
    std::string                    m_pending_raw_text;
    nlohmann::json                 m_pending_executed_root;
    bool                           m_mutations_applied{false};
    bool                           m_readonly_stall_nudged{false};
    bool                           m_setting_search_nudged{false};
    std::string                    m_applied_action_fingerprint;

    void note_workflow_mutations(const AssistantWorkflowRun& workflow);
    AssistantPipelineOptions          pipeline_options() const;

    void schedule_post_apply_verification(const nlohmann::json& root, const std::string& assistant_msg,
                                          const std::string& raw_text, bool applied,
                                          AssistantWorkflowRun workflow);
    void handle_post_apply_verification(const nlohmann::json& root, const std::string& assistant_msg,
                                        const std::string& raw_text, bool applied,
                                        const AssistantWorkflowRun& workflow);
    bool handle_verify_report(const AssistantConfigVerifyReport& report, const std::string& assistant_msg,
                              const nlohmann::json& root, const std::string& raw_text, bool applied,
                              const AssistantWorkflowRun& workflow);
};

}} // namespace

#endif

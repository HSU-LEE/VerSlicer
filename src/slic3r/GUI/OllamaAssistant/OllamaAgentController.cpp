#include "OllamaAgentController.hpp"

#include "OllamaActionExecutor.hpp"
#include "OllamaActionPipeline.hpp"
#include "OllamaActionWorkflow.hpp"
#include "OllamaAgentGoalPlanner.hpp"
#include "OllamaAgentStateService.hpp"
#include "OllamaAgentEventBus.hpp"
#include "OllamaAssistContextBuilder.hpp"
#include "OllamaConfig.hpp"
#include "OllamaExecutionPolicy.hpp"
#include "OllamaToolRegistry.hpp"
#include "OllamaToolResult.hpp"

#include "../AICoach/AIGuiOrchestrator.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"

#include <atomic>

namespace Slic3r { namespace GUI {

namespace {

std::string build_assist_loop_system_prompt(bool korean)
{
    const std::string tools = OllamaToolRegistry::agent_tools_schema_block(korean);
    if (korean) {
        return std::string(
                   "당신은 VerSlicer AI Assist입니다. 사용자 목표를 달성할 때까지 도구를 연속 호출하세요.\n"
                   "먼저 get_state로 상태를 확인하고, 필요한 actions를 실행한 뒤 결과를 반영해 다음 단계를 정하세요.\n"
                   "여러 단계 목표(예: 브림 → 슬라이스 → 전송)는 한 번에 하나씩 실행하고, 슬라이스 후에는 get_state로 "
                   "current_plate_sliced를 확인한 뒤 send_print 하세요.\n")
               + tools;
    }
    return std::string(
               "You are VerSlicer AI Assist. Call tools in a loop until the user's goal is met.\n"
               "Start with get_state, execute actions, read results, then plan the next step.\n"
               "For multi-step goals (e.g. brim → slice → send), run one step at a time; after slice, call get_state "
               "and check current_plate_sliced before send_print.\n")
           + tools;
}

void trim_assist_messages(std::vector<OllamaMessage>& messages)
{
    constexpr size_t kKeepTail = 12;
    if (messages.size() <= 1 + kKeepTail)
        return;
    std::vector<OllamaMessage> trimmed;
    trimmed.reserve(1 + kKeepTail);
    trimmed.push_back(messages.front());
    trimmed.insert(trimmed.end(), messages.end() - static_cast<ptrdiff_t>(kKeepTail), messages.end());
    messages.swap(trimmed);
}

void reorder_actions_readonly_first(nlohmann::json& root)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return;
    nlohmann::json readonly = nlohmann::json::array();
    nlohmann::json mutating = nlohmann::json::array();
    for (const auto& a : root["actions"]) {
        if (!a.is_object()) {
            mutating.push_back(a);
            continue;
        }
        const std::string type = a.value("type", "");
        if (ollama_action_is_readonly(type))
            readonly.push_back(a);
        else
            mutating.push_back(a);
    }
    root["actions"] = nlohmann::json::array();
    for (const auto& a : readonly)
        root["actions"].push_back(a);
    for (const auto& a : mutating)
        root["actions"].push_back(a);
}

bool root_is_done(const nlohmann::json& root)
{
    return root.value("done", false) || root.value("status", "") == "complete";
}

OllamaAgentController* g_active_agent = nullptr;
bool                   g_agent_event_hook_installed = false;

void on_global_agent_event(const OllamaAgentEvent& evt)
{
    if (evt.kind != OllamaAgentEventKind::SliceDone)
        return;
    OllamaAgentController* agent = g_active_agent;
    if (!agent || !agent->is_running())
        return;
    const bool success = evt.payload.value("success", false);
    wxGetApp().CallAfter([agent, success]() {
        if (!wxGetApp().initialized() || wxGetApp().is_closing())
            return;
        if (!g_active_agent || g_active_agent != agent || !agent->is_running())
            return;
        agent->handle_agent_event({OllamaAgentEventKind::SliceDone, {{"success", success}}});
    });
}

void ensure_agent_event_hook()
{
    if (g_agent_event_hook_installed)
        return;
    g_agent_event_hook_installed = true;
    OllamaAgentEventBus::instance().subscribe(on_global_agent_event);
}

} // namespace

OllamaAgentController::OllamaAgentController(OllamaClient& client, std::string model)
    : m_client(client), m_model(std::move(model)), m_alive(std::make_shared<std::atomic<bool>>(true))
{
}

void OllamaAgentController::set_model(std::string model)
{
    m_model = std::move(model);
}

void OllamaAgentController::cancel()
{
    m_cancelled      = true;
    m_awaiting_slice = false;
    if (g_active_agent == this)
        g_active_agent = nullptr;
    OllamaClient::cancel_active_requests(OllamaCancelDomain::Chat);
}

void OllamaAgentController::run_goal(const std::string& user_goal, OllamaExecutionPolicy policy, wxWindow* parent,
                                     OllamaAgentCallbacks callbacks, int max_steps)
{
    if (m_running)
        return;
    ensure_agent_event_hook();
    m_running        = true;
    m_cancelled      = false;
    m_awaiting_slice = false;
    m_step           = 0;
    m_max_steps      = max_steps > 0 ? max_steps : ollama_assist_max_steps();
    m_policy         = policy;
    m_parent         = parent;
    m_callbacks      = std::move(callbacks);
    m_user_goal      = user_goal;
    m_step_tool_results.clear();
    m_messages.clear();
    m_pending_assistant_msg.clear();
    m_pending_raw_text.clear();
    m_pending_executed_root = nlohmann::json::object();

    const bool ko = wxGetApp().current_language_code().StartsWith("ko");
    m_prefetch    = OllamaAssistContextBuilder::prefetch_for_goal(user_goal, ko);
    m_messages.push_back({"system", build_assist_loop_system_prompt(ko)});

    const nlohmann::json plan_hint = OllamaAgentGoalPlanner::build_plan_hint(user_goal, ko);
    m_messages.push_back(
        {"user", OllamaAssistContextBuilder::build_initial_user_block(user_goal, plan_hint, m_prefetch, ko)});

    begin_step();
}

void OllamaAgentController::begin_step()
{
    if (m_cancelled || !m_alive->load()) {
        finish({.cancelled = true});
        return;
    }
    if (m_step >= m_max_steps) {
        OllamaAgentRunResult r;
        r.completed     = false;
        r.blocked       = true;
        r.steps_taken   = m_step;
        r.step_tool_results = m_step_tool_results;
        const bool ko   = wxGetApp().current_language_code().StartsWith("ko");
        r.final_message = ko ? "최대 단계 수에 도달했습니다." : "Reached maximum agent steps.";
        finish(std::move(r));
        return;
    }

    ++m_step;
    if (m_callbacks.on_thinking) {
        const bool ko = wxGetApp().current_language_code().StartsWith("ko");
        m_callbacks.on_thinking(ko ? wxString::Format(wxString::FromUTF8("작업 중… (%d)"), m_step)
                                   : wxString::Format("Working… (%d)", m_step));
    }

    trim_assist_messages(m_messages);

    const auto alive = m_alive;
    const std::string model = m_model;
    m_client.chat(model, m_messages,
                  [this, alive](const std::string& text, const std::string& error) {
                      wxGetApp().CallAfter([this, alive, text, error]() {
                          if (!alive->load() || wxGetApp().is_closing())
                              return;
                          on_llm_response(text, error);
                      });
                  });
}

bool OllamaAgentController::try_rule_fallback_finish()
{
    std::vector<OllamaActionResult> results;
    if (!OllamaActionPipeline::try_symptom_fallback_apply(m_user_goal, m_parent, m_policy, &results))
        return false;

    const bool ko = wxGetApp().current_language_code().StartsWith("ko");
    nlohmann::json step = nlohmann::json::array();
    for (const auto& r : results)
        step.push_back(ollama_tool_result_json("set_config", r));
    m_step_tool_results.push_back(std::move(step));

    OllamaAgentRunResult out;
    out.completed         = true;
    out.steps_taken       = m_step;
    out.step_tool_results = m_step_tool_results;
    out.final_message     = ko ? "규칙 기반으로 설정을 적용했습니다." : "Applied rule-based settings.";
    finish(std::move(out));
    return true;
}

void OllamaAgentController::on_llm_response(const std::string& text, const std::string& error)
{
    if (m_cancelled) {
        finish({.cancelled = true, .steps_taken = m_step, .step_tool_results = m_step_tool_results});
        return;
    }
    if (!error.empty()) {
        if (try_rule_fallback_finish())
            return;
        OllamaAgentRunResult r;
        r.blocked           = true;
        r.steps_taken       = m_step;
        r.step_tool_results = m_step_tool_results;
        r.final_message     = error;
        finish(std::move(r));
        return;
    }

    m_messages.push_back({"assistant", text});

    nlohmann::json root;
    try {
        root = OllamaActionExecutor::extract_action_json(text);
    } catch (...) {
        root = nlohmann::json::object({{"message", text}});
    }

    reorder_actions_readonly_first(root);

    OllamaPipelineOptions opt;
    opt.apply_mode         = true;
    opt.include_makerworld = true;
    opt.user_request       = m_user_goal;
    OllamaActionPipeline::process_actions(root, opt);

    const std::string assistant_msg = root.value("message", "");
    if (root_is_done(root) && !OllamaActionWorkflow::has_executable_actions(root)) {
        OllamaAgentRunResult r;
        r.completed         = true;
        r.steps_taken       = m_step;
        r.step_tool_results = m_step_tool_results;
        r.final_message     = assistant_msg.empty() ? text : assistant_msg;
        finish(std::move(r));
        return;
    }

    if (!OllamaActionWorkflow::has_executable_actions(root)) {
        if (!assistant_msg.empty() && m_step < m_max_steps) {
            m_messages.push_back({"user", std::string("Continue toward the goal. Call get_state if needed. Goal:\n") + m_user_goal});
            begin_step();
            return;
        }
        OllamaAgentRunResult r;
        r.completed         = true;
        r.steps_taken       = m_step;
        r.step_tool_results = m_step_tool_results;
        r.final_message     = assistant_msg.empty() ? text : assistant_msg;
        finish(std::move(r));
        return;
    }

    AIGuiOrchestrator::instance().on_chat_apply_begin();
    const OllamaWorkflowRun workflow = OllamaActionWorkflow::execute_with_policy(root, m_parent, m_policy);
    const nlohmann::json    tool_results = ollama_tool_results_from_workflow(root, workflow);
    m_step_tool_results.push_back(tool_results);

    bool applied = false;
    for (const auto& r : workflow.results) {
        if (r.effective_change)
            applied = true;
    }
    AIGuiOrchestrator::instance().on_chat_apply_end(applied, root, "ollama_assist_loop");

    bool verify_failed = false;
    if (root.contains("actions") && root["actions"].is_array()) {
        for (const auto& a : root["actions"]) {
            if (!a.is_object() || a.value("type", "") != "set_config")
                continue;
            if (!a.contains("options") || !a["options"].is_object())
                continue;
            const auto mismatches = OllamaAgentStateService::verify_config_applied(a["options"], a.value("preset", "print"));
            if (!mismatches.empty()) {
                verify_failed = true;
                m_step_tool_results.back().push_back(nlohmann::json::object({
                    {"tool", "verify_config"},
                    {"ok", false},
                    {"changed", false},
                    {"message", "Config verification failed"},
                    {"blocker", mismatches.front()},
                }));
            }
        }
    }

    if (verify_failed && m_step < m_max_steps) {
        const bool ko = wxGetApp().current_language_code().StartsWith("ko");
        std::string fb = ko ? "verify_config 실패. mismatches를 수정해 set_config를 다시 호출하세요.\n\nGoal:\n"
                            : "verify_config failed. Fix mismatches and call set_config again.\n\nGoal:\n";
        fb += m_user_goal;
        m_messages.push_back({"user", std::move(fb)});
        begin_step();
        return;
    }

    if (verify_failed) {
        OllamaAgentRunResult r;
        r.blocked           = true;
        r.steps_taken       = m_step;
        r.step_tool_results = m_step_tool_results;
        r.final_message     = assistant_msg.empty() ? "Config verification failed." : assistant_msg;
        finish(std::move(r));
        return;
    }

    if (workflow.cancelled || workflow.preview_only) {
        OllamaAgentRunResult r;
        r.blocked           = true;
        r.steps_taken       = m_step;
        r.step_tool_results = m_step_tool_results;
        r.final_message     = assistant_msg.empty() ? "Workflow cancelled or preview only." : assistant_msg;
        finish(std::move(r));
        return;
    }

    run_deterministic_follow_ups();

    m_pending_assistant_msg = assistant_msg;
    m_pending_raw_text      = text;
    m_pending_executed_root = root;
    if (update_awaiting_slice_from_results()) {
        g_active_agent = this;
        return;
    }

    proceed_after_tool_execution(root, assistant_msg, text);
}

bool OllamaAgentController::update_awaiting_slice_from_results()
{
    const nlohmann::json state = OllamaAgentStateService::snapshot();
    const bool           plate_sliced =
        state.contains("slice_state") && state["slice_state"].value("current_plate_sliced", false);
    if (plate_sliced) {
        m_awaiting_slice = false;
        return false;
    }

    bool slice_started = false;
    for (const auto& step : m_step_tool_results) {
        if (!step.is_array())
            continue;
        for (const auto& r : step) {
            if (r.is_object() && r.value("tool", "") == "slice" && r.value("ok", false))
                slice_started = true;
        }
    }
    m_awaiting_slice = slice_started;
    return m_awaiting_slice;
}

void OllamaAgentController::handle_agent_event(const OllamaAgentEvent& evt)
{
    if (evt.kind != OllamaAgentEventKind::SliceDone || !m_awaiting_slice)
        return;
    continue_after_slice(evt.payload.value("success", false));
}

void OllamaAgentController::continue_after_slice(bool slice_ok)
{
    if (!m_running || m_cancelled)
        return;

    m_awaiting_slice = false;
    if (g_active_agent == this)
        g_active_agent = nullptr;

    if (!slice_ok) {
        const bool ko = wxGetApp().current_language_code().StartsWith("ko");
        OllamaAgentRunResult r;
        r.blocked           = true;
        r.steps_taken       = m_step;
        r.step_tool_results = m_step_tool_results;
        r.final_message     = ko ? "슬라이싱에 실패했습니다." : "Slicing failed.";
        finish(std::move(r));
        return;
    }

    run_deterministic_follow_ups();
    if (update_awaiting_slice_from_results()) {
        g_active_agent = this;
        return;
    }

    proceed_after_tool_execution(m_pending_executed_root, m_pending_assistant_msg, m_pending_raw_text);
}

void OllamaAgentController::proceed_after_tool_execution(const nlohmann::json& executed_root,
                                                         const std::string& assistant_msg, const std::string& raw_text)
{
    if (root_is_done(executed_root)) {
        OllamaAgentRunResult r;
        r.completed         = true;
        r.steps_taken       = m_step;
        r.step_tool_results = m_step_tool_results;
        r.final_message     = assistant_msg.empty() ? raw_text : assistant_msg;
        finish(std::move(r));
        return;
    }

    const nlohmann::json& tool_results =
        m_step_tool_results.empty() ? nlohmann::json::array() : m_step_tool_results.back();
    std::string           feedback     = std::string("Tool results (JSON):\n") + tool_results.dump(2);
    if (!assistant_msg.empty())
        feedback += std::string("\n\nAssistant note:\n") + assistant_msg;
    feedback += std::string("\n\nContinue toward goal:\n") + m_user_goal;
    m_messages.push_back({"user", std::move(feedback)});
    begin_step();
}

void OllamaAgentController::finish(OllamaAgentRunResult result)
{
    m_running        = false;
    m_awaiting_slice = false;
    if (g_active_agent == this)
        g_active_agent = nullptr;
    if (m_callbacks.on_finished)
        m_callbacks.on_finished(result);
}

bool OllamaAgentController::execute_agent_root(const nlohmann::json& root)
{
    nlohmann::json work = root;
    reorder_actions_readonly_first(work);

    OllamaPipelineOptions opt;
    opt.apply_mode         = true;
    opt.include_makerworld = true;
    opt.user_request       = m_user_goal;

    OllamaActionPipeline::process_actions(work, opt);
    if (!OllamaActionWorkflow::has_executable_actions(work))
        return false;

    AIGuiOrchestrator::instance().on_chat_apply_begin();
    const OllamaWorkflowRun workflow = OllamaActionWorkflow::execute_with_policy(work, m_parent, m_policy);
    const nlohmann::json    tool_results = ollama_tool_results_from_workflow(work, workflow);
    m_step_tool_results.push_back(tool_results);

    bool applied = false;
    for (const auto& r : workflow.results) {
        if (r.effective_change)
            applied = true;
    }
    AIGuiOrchestrator::instance().on_chat_apply_end(applied, work, "ollama_assist_loop");

    if (workflow.cancelled || workflow.preview_only)
        return false;

    if (work.contains("actions") && work["actions"].is_array()) {
        for (const auto& a : work["actions"]) {
            if (!a.is_object() || a.value("type", "") != "set_config")
                continue;
            if (!a.contains("options") || !a["options"].is_object())
                continue;
            const auto mismatches =
                OllamaAgentStateService::verify_config_applied(a["options"], a.value("preset", "print"));
            if (!mismatches.empty()) {
                m_step_tool_results.back().push_back(nlohmann::json::object({
                    {"tool", "verify_config"},
                    {"ok", false},
                    {"changed", false},
                    {"message", "Config verification failed"},
                    {"blocker", mismatches.front()},
                }));
            }
        }
    }

    return true;
}

void OllamaAgentController::run_deterministic_follow_ups()
{
    constexpr int kMaxChain = 3;
    for (int i = 0; i < kMaxChain; ++i) {
        if (m_cancelled || m_step >= m_max_steps)
            break;
        const auto follow = OllamaAgentGoalPlanner::try_deterministic_follow_up(m_user_goal, m_step_tool_results);
        if (!follow || !OllamaActionWorkflow::has_executable_actions(*follow))
            break;

        ++m_step;
        if (m_callbacks.on_thinking) {
            const bool ko = wxGetApp().current_language_code().StartsWith("ko");
            m_callbacks.on_thinking(ko ? wxString::Format(wxString::FromUTF8("다음 단계 %d…"), m_step)
                                         : wxString::Format("Next step %d…", m_step));
        }

        if (!execute_agent_root(*follow))
            break;

        if (follow->value("done", false))
            break;
    }
}

}} // namespace

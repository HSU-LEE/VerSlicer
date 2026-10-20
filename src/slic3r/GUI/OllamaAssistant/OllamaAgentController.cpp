#include "OllamaAgentController.hpp"
#include "AiLocale.hpp"
#include "AiCatalog.hpp"

#include "../UiDispatch.hpp"
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
#include "OllamaSettingSearch.hpp"
#include "OllamaToolGateway.hpp"
#include "OllamaToolResult.hpp"
#include "OllamaConfigProposalBuilder.hpp"

#include "../AICoach/AIGuiOrchestrator.hpp"
#include "../BambuSmartPrint/PrintPlannerGui.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../Plater.hpp"

#include "libslic3r/BambuSmartPrint/PrintIntentSession.hpp"

#include <boost/log/trivial.hpp>

#include <atomic>

namespace Slic3r { namespace GUI {

namespace {

void emit_thinking_lines(const OllamaAgentCallbacks& callbacks, const std::string& block)
{
    if (!callbacks.on_thinking || block.empty())
        return;
    size_t start = 0;
    while (start < block.size()) {
        size_t end = block.find('\n', start);
        if (end == std::string::npos)
            end = block.size();
        const std::string line = block.substr(start, end - start);
        if (!line.empty())
            callbacks.on_thinking(wxString::FromUTF8(line));
        start = end + 1;
    }
}

std::string build_assist_loop_system_prompt(bool korean)
{
    std::string prompt = OllamaActionExecutor::build_system_prompt(true);
    prompt += ai_catalog_text("\n\n## Loop\n- Batch only actions that do not depend on each other's results.\n- Do not set done:true before you have seen tool results. Set it as soon as the request is reasonably satisfied.\n- After a change lands, do not look up more related settings or read state again.\n- If the same call returns the same result again, say why and stop.\n");
    prompt += OllamaToolRegistry::agent_tools_schema_block(korean);
    return prompt;
}

void trim_assist_messages(std::vector<OllamaMessage>& messages)
{
    constexpr size_t kKeepTail = 60;
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

std::string mutating_action_fingerprint(const nlohmann::json& root)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return {};
    std::string out;
    for (const auto& action : root["actions"]) {
        if (!action.is_object())
            continue;
        const std::string type = action.value("type", "");
        if (type.empty() || OllamaToolRegistry::is_observation(type))
            continue;
        out += type;
        out += '\n';
        out += action.dump();
        out += '\n';
    }
    return out;
}

bool step_has_substantive_change(const std::vector<nlohmann::json>& steps)
{
    for (const auto& step : steps) {
        if (!step.is_array())
            continue;
        for (const auto& r : step) {
            if (!r.is_object() || !r.value("changed", false))
                continue;
            const std::string tool = r.value("tool", "");
            if (OllamaToolRegistry::is_observation(tool) || tool == "verify_config" || tool == "ui_select_tab")
                continue;
            return true;
        }
    }
    return false;
}

bool tool_step_is_readonly(const nlohmann::json& step)
{
    if (!step.is_array() || step.empty())
        return false;
    for (const auto& r : step) {
        if (!r.is_object())
            return false;
        const std::string tool = r.value("tool", "");
        if (!OllamaToolRegistry::is_observation(tool))
            return false;
    }
    return true;
}

std::string pick_user_facing_message(const std::vector<nlohmann::json>& step_tool_results,
                                     const std::string& assistant_msg, bool korean)
{
    return ollama_verified_user_message(step_tool_results, assistant_msg, korean);
}

void on_global_agent_event(const OllamaAgentEvent& evt)
{
    if (evt.kind != OllamaAgentEventKind::SliceDone)
        return;
    OllamaAgentController* agent = g_active_agent;
    if (!agent || !agent->is_running())
        return;
    const bool success = evt.payload.value("success", false);
    post_to_ui([agent, success]() {
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

OllamaAgentController::~OllamaAgentController()
{
    // Invalidate the liveness token BEFORE the object is gone. All async
    // continuations (client callbacks, scheduled verifications, the global
    // SliceDone hook) run on the main thread via CallAfter and check
    // m_alive / g_active_agent first; this destructor also runs on the main
    // thread, so flipping the token here makes any captured raw `this`
    // unreachable after the check.
    m_alive->store(false);
    m_running        = false;
    m_awaiting_slice = false;
    if (g_active_agent == this)
        g_active_agent = nullptr;
    OllamaClient::cancel_active_requests(OllamaCancelDomain::Chat);
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
    m_mutations_applied = false;
    m_readonly_stall_nudged = false;
    m_applied_action_fingerprint.clear();

    const bool ko = wxGetApp().current_language_code().StartsWith("ko");
    m_prefetch = OllamaAssistContextBuilder::prefetch_for_goal(user_goal, ko);
    m_messages.push_back({"system", build_assist_loop_system_prompt(ko)});

    m_messages.push_back(
        {"user", OllamaAssistContextBuilder::build_initial_user_block(user_goal, nlohmann::json::object(), m_prefetch, ko)});

    begin_step();
}

void OllamaAgentController::refresh_print_intent_and_proposal(bool korean)
{
    // Best-effort deterministic proposal. Never let an exception escape here: this runs on
    // the main thread inside the agent loop, and an uncaught throw would unwind the wx main
    // loop (OnExceptionInMainLoop rethrows) and quit the whole app.
    try {
        auto& session = BambuSmartPrint::PrintIntentSession::instance();
        if (Plater* plater = wxGetApp().plater()) {
            const BambuSmartPrint::PlateContext ctx = PrintPlannerGui::build_plate_context(plater);
            session.merge_turn(m_user_goal, ctx.mesh, ctx.base_config);
            OllamaConfigProposalBuilder::build_from_context(plater, ctx, session.intent(), korean);
        } else {
            session.merge_turn(m_user_goal);
        }
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "Ollama agent proposal build failed: " << ex.what();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "Ollama agent proposal build failed: unknown error";
    }
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
        r.final_message = ai_catalog_text("Reached maximum agent steps.");
        finish(std::move(r));
        return;
    }

    ++m_step;
    if (m_step == 1 && m_callbacks.on_thinking) {
        const bool ko = wxGetApp().current_language_code().StartsWith("ko");
        m_callbacks.on_thinking(AiLocale::text("Thinking about how to do this."));
    }

    trim_assist_messages(m_messages);

    const auto alive = m_alive;
    const std::string model = m_model;
    m_client.chat(model, m_messages,
                  [this, alive](const std::string& text, const std::string& error) {
                      post_to_ui([this, alive, text, error]() {
                          if (!alive->load() || wxGetApp().is_closing())
                              return;
                          on_llm_response(text, error);
                      });
                  },
                  OllamaRequestKind::Chat, nullptr, OllamaToolGateway::ollama_tools().dump());
}

void OllamaAgentController::note_workflow_mutations(const OllamaWorkflowRun& workflow)
{
    for (const auto& r : workflow.results) {
        if (r.success && r.effective_change) {
            m_mutations_applied = true;
            return;
        }
    }
}

OllamaPipelineOptions OllamaAgentController::pipeline_options() const
{
    OllamaPipelineOptions opt;
    opt.apply_mode                = true;
    opt.include_makerworld        = true;
    opt.user_request              = m_user_goal;
    opt.mutations_already_applied = m_mutations_applied;
    return opt;
}

void OllamaAgentController::on_llm_response(const std::string& text, const std::string& error)
{
    if (m_cancelled) {
        finish({.cancelled = true, .steps_taken = m_step, .step_tool_results = m_step_tool_results});
        return;
    }
    if (!error.empty()) {
        OllamaAgentRunResult r;
        r.blocked           = true;
        r.steps_taken       = m_step;
        r.step_tool_results = m_step_tool_results;
        r.final_message     = error;
        finish(std::move(r));
        return;
    }

    // Action extraction, normalization, and execution run on the wx main thread. An
    // uncaught throw (JSON type errors, mesh/plater edge cases in a tool) would unwind
    // the wx main loop (OnExceptionInMainLoop rethrows) and terminate the whole app.
    // Contain it here and end the turn gracefully with a user-visible message.
    try {
        dispatch_llm_actions(text);
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(error) << "Ollama assist loop: action dispatch failed: " << ex.what();
        AIGuiOrchestrator::instance().on_chat_apply_end(false);
        fail_turn_after_exception();
    } catch (...) {
        BOOST_LOG_TRIVIAL(error) << "Ollama assist loop: action dispatch failed: unknown error";
        AIGuiOrchestrator::instance().on_chat_apply_end(false);
        fail_turn_after_exception();
    }
}

void OllamaAgentController::fail_turn_after_exception()
{
    const bool           ko = wxGetApp().current_language_code().StartsWith("ko");
    OllamaAgentRunResult r;
    r.blocked           = true;
    r.steps_taken       = m_step;
    r.step_tool_results = m_step_tool_results;
    r.final_message     = ai_catalog_text("Something went wrong while processing that. Please try again.");
    finish(std::move(r));
}

void OllamaAgentController::dispatch_llm_actions(const std::string& text)
{
    m_messages.push_back({"assistant", text});

    nlohmann::json root;
    try {
        root = OllamaActionExecutor::extract_action_json(text);
    } catch (...) {
        root = nlohmann::json::object({{"message", text}});
    }

    reorder_actions_readonly_first(root);

    OllamaPipelineOptions opt = pipeline_options();
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
        if (m_step < m_max_steps) {
            const bool ko = wxGetApp().current_language_code().StartsWith("ko");
            std::string nudge = ai_catalog_text("Reply with executable actions JSON. Call get_state first, then set_config/geometry/mesh tools. Do not only explain — return JSON actions.\n\nGoal:\n");
            nudge += m_user_goal;
            m_messages.push_back({"user", std::move(nudge)});
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

    const std::string action_fp = mutating_action_fingerprint(root);
    if (!action_fp.empty() && action_fp == m_applied_action_fingerprint) {
        const bool ko = wxGetApp().current_language_code().StartsWith("ko");
        OllamaAgentRunResult r;
        r.completed         = true;
        r.steps_taken       = m_step;
        r.step_tool_results = m_step_tool_results;
        r.final_message = ai_catalog_text("That change is already applied on the plate.");
        finish(std::move(r));
        return;
    }

    AIGuiOrchestrator::instance().on_chat_apply_begin();
    {
        const bool ko = wxGetApp().current_language_code().StartsWith("ko");
        emit_thinking_lines(m_callbacks, ollama_thinking_planned_actions(root, ko));
    }
    const OllamaWorkflowRun workflow = OllamaActionWorkflow::execute_with_policy(root, m_parent, m_policy);
    note_workflow_mutations(workflow);
    const nlohmann::json    tool_results = ollama_tool_results_from_workflow(root, workflow);
    m_step_tool_results.push_back(tool_results);

    bool applied = false;
    for (const auto& r : workflow.results) {
        if (r.effective_change)
            applied = true;
    }
    AIGuiOrchestrator::instance().on_chat_apply_end(applied, root, "ollama_assist_loop");
    if (applied) {
        const std::string fp = mutating_action_fingerprint(root);
        if (!fp.empty())
            m_applied_action_fingerprint = fp;
    }

    schedule_post_apply_verification(root, assistant_msg, text, applied, std::move(workflow));
}

void OllamaAgentController::schedule_post_apply_verification(const nlohmann::json& root,
                                                             const std::string& assistant_msg,
                                                             const std::string& raw_text, bool applied,
                                                             OllamaWorkflowRun workflow)
{
    const auto alive = m_alive;
    post_to_ui([this, alive, root, assistant_msg, raw_text, applied, workflow = std::move(workflow)]() mutable {
        if (!alive->load() || wxGetApp().is_closing() || !m_running || m_cancelled)
            return;
        handle_post_apply_verification(root, assistant_msg, raw_text, applied, workflow);
    });
}

void OllamaAgentController::handle_post_apply_verification(const nlohmann::json& root,
                                                           const std::string& assistant_msg,
                                                           const std::string& raw_text, bool applied,
                                                           const OllamaWorkflowRun& workflow)
{
    OllamaConfigVerifyReport report = OllamaAgentStateService::verify_set_config_actions(root, m_user_goal);
    if (!m_step_tool_results.empty() && report.tool_results.is_array() && !report.tool_results.empty())
        m_step_tool_results.back().insert(m_step_tool_results.back().end(), report.tool_results.begin(),
                                          report.tool_results.end());

    if (handle_verify_report(report, assistant_msg, root, raw_text, applied, workflow))
        return;

    if (workflow.cancelled || workflow.preview_only) {
        OllamaAgentRunResult r;
        r.blocked           = true;
        r.steps_taken       = m_step;
        r.step_tool_results = m_step_tool_results;
        r.final_message     = assistant_msg.empty() ? "Workflow cancelled or preview only." : assistant_msg;
        finish(std::move(r));
        return;
    }

    const nlohmann::json& tool_results =
        m_step_tool_results.empty() ? nlohmann::json::array() : m_step_tool_results.back();
    {
        const bool ko = wxGetApp().current_language_code().StartsWith("ko");
        emit_thinking_lines(m_callbacks, ollama_thinking_after_tools(tool_results, ko));
    }

    if (OllamaAgentGoalPlanner::should_auto_finish_after_apply(m_user_goal, applied, m_step_tool_results)) {
        OllamaAgentRunResult r;
        r.completed         = true;
        r.steps_taken       = m_step;
        r.step_tool_results = m_step_tool_results;
        const bool ko       = wxGetApp().current_language_code().StartsWith("ko");
        r.final_message     = pick_user_facing_message(m_step_tool_results, assistant_msg, ko);
        if (r.final_message.empty())
            r.final_message = applied
                ? (ai_catalog_text("Applied the requested changes."))
                : (ai_catalog_text("Nothing was applied."));
        finish(std::move(r));
        return;
    }

    m_pending_assistant_msg = assistant_msg;
    m_pending_raw_text      = raw_text;
    m_pending_executed_root = root;
    if (update_awaiting_slice_from_results()) {
        g_active_agent = this;
        return;
    }

    proceed_after_tool_execution(root, assistant_msg, raw_text);
}

bool OllamaAgentController::handle_verify_report(const OllamaConfigVerifyReport& report,
                                               const std::string& assistant_msg, const nlohmann::json& root,
                                               const std::string& raw_text, bool /*applied*/,
                                               const OllamaWorkflowRun& /*workflow*/)
{
    if (report.all_ok)
        return false;

    if (m_step < m_max_steps) {
        const bool ko = wxGetApp().current_language_code().StartsWith("ko");
        std::string fb = OllamaAgentStateService::build_verify_retry_nudge(report, ko);
        fb += m_user_goal;
        m_messages.push_back({"user", std::move(fb)});
        begin_step();
        return true;
    }

    OllamaAgentRunResult r;
    r.blocked           = true;
    r.steps_taken       = m_step;
    r.step_tool_results = m_step_tool_results;
    if (!assistant_msg.empty())
        r.final_message = assistant_msg;
    else if (!report.mismatches.empty())
        r.final_message = "Config verification failed: " + report.mismatches.front();
    else
        r.final_message = "Config verification failed.";
    finish(std::move(r));
    (void) root;
    (void) raw_text;
    return true;
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
        r.final_message     = ai_catalog_text("Slicing failed.");
        finish(std::move(r));
        return;
    }

    const bool ko = wxGetApp().current_language_code().StartsWith("ko");
    m_messages.push_back({"user", ai_catalog_text("Slicing finished. Call get_state to verify slice_state, then continue.\n\nGoal:\n") + m_user_goal});
    begin_step();
}

void OllamaAgentController::proceed_after_tool_execution(const nlohmann::json& executed_root,
                                                         const std::string& assistant_msg, const std::string& raw_text)
{
    if (root_is_done(executed_root)) {
        const bool ko = wxGetApp().current_language_code().StartsWith("ko");
        if (!step_has_substantive_change(m_step_tool_results)) {
            if (!m_setting_search_nudged && m_step < m_max_steps) {
                const std::vector<OllamaSettingSearchHit> hits = OllamaSettingSearch::search(m_user_goal, 2, 5);
                if (!hits.empty() && hits.front().score >= 60) {
                    m_setting_search_nudged = true;
                    std::vector<std::string> keys;
                    for (const OllamaSettingSearchHit& hit : hits)
                        keys.push_back(hit.key);
                    const DynamicPrintConfig* print_cfg = nullptr;
                    const DynamicPrintConfig* filament_cfg = nullptr;
                    if (wxGetApp().preset_bundle) {
                        print_cfg    = &wxGetApp().preset_bundle->prints.get_edited_preset().config;
                        filament_cfg = &wxGetApp().preset_bundle->filaments.get_edited_preset().config;
                    }
                    const nlohmann::json found = OllamaSettingSearch::lookup(keys, print_cfg, filament_cfg, ko);
                    std::string nudge = ai_catalog_text("No setting has changed yet. Call set_config with key, preset, and an enum_values entry from the matches. To turn a feature on, do not pick a value that starts with 'no '. Set done:true only after it changes.\n");
                    nudge += found.dump(2);
                    nudge += "\n\nGoal:\n";
                    nudge += m_user_goal;
                    m_messages.push_back({"user", std::move(nudge)});
                    begin_step();
                    return;
                }
            }
        }
        OllamaAgentRunResult r;
        r.completed         = true;
        r.steps_taken       = m_step;
        r.step_tool_results = m_step_tool_results;
        r.final_message     = pick_user_facing_message(m_step_tool_results, assistant_msg, ko);
        if (r.final_message.empty()) {
            if (!m_step_tool_results.empty())
                r.final_message = ai_catalog_text("Nothing was applied.");
            else
                r.final_message = assistant_msg.empty() ? raw_text : assistant_msg;
        }
        finish(std::move(r));
        return;
    }

    if (m_step_tool_results.size() >= 2
        && m_step_tool_results.back() == m_step_tool_results[m_step_tool_results.size() - 2]) {
        const bool readonly = tool_step_is_readonly(m_step_tool_results.back());
        if (readonly && !m_readonly_stall_nudged && m_step < m_max_steps) {
            m_readonly_stall_nudged = true;
            const bool ko = wxGetApp().current_language_code().StartsWith("ko");
            m_messages.push_back({"user", ai_catalog_text("The same read result repeated. Do not call get_state, list_objects, select_object, wiki_search, or find_setting again. You already have the state. Apply the goal now with set_config or a geometry tool. If you cannot, return done:true and the reason only.\n\nGoal:\n") + m_user_goal});
            begin_step();
            return;
        }
        const bool ko = wxGetApp().current_language_code().StartsWith("ko");
        OllamaAgentRunResult r;
        r.blocked           = true;
        r.steps_taken       = m_step;
        r.step_tool_results = m_step_tool_results;
        if (m_mutations_applied)
            r.final_message = ai_catalog_text("Applied the changes that went through. Stopped because the same result repeated.");
        else
            r.final_message = ai_catalog_text("No setting was changed. Stopped after the same state was read again. Say the value you want more specifically.");
        finish(std::move(r));
        return;
    }

    const nlohmann::json& tool_results =
        m_step_tool_results.empty() ? nlohmann::json::array() : m_step_tool_results.back();
    if (m_mutations_applied && tool_step_is_readonly(tool_results)) {
        const bool ko = wxGetApp().current_language_code().StartsWith("ko");
        OllamaAgentRunResult r;
        r.completed         = true;
        r.steps_taken       = m_step;
        r.step_tool_results = m_step_tool_results;
        r.final_message     = pick_user_facing_message(m_step_tool_results, assistant_msg, ko);
        if (r.final_message.empty())
            r.final_message = ai_catalog_text("The request is applied.");
        finish(std::move(r));
        return;
    }
    std::string           feedback     = std::string("Tool results (JSON):\n") + tool_results.dump(2);
    for (const auto& r : tool_results) {
        if (!r.is_object() || r.value("ok", true))
            continue;
        if (r.contains("blocker") && r["blocker"].is_string())
            feedback += std::string("\nBlocker: ") + r["blocker"].get<std::string>();
        if (r.value("tool", "") == "verify_config" && r.contains("data") && r["data"].is_object()
            && r["data"].contains("config_digest") && r["data"]["config_digest"].is_object()
            && !r["data"]["config_digest"].empty()) {
            feedback += std::string("\n\nConfig digest (JSON):\n") + r["data"]["config_digest"].dump(2);
        }
    }
    if (!assistant_msg.empty())
        feedback += std::string("\n\nAssistant note:\n") + assistant_msg;
    feedback += std::string("\n\nIf the request is already satisfied, return done:true and stop. Do not keep tuning.\nGoal:\n") + m_user_goal;
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

}} // namespace

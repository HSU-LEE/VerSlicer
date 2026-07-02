#ifndef slic3r_OllamaToolResult_hpp_
#define slic3r_OllamaToolResult_hpp_

#include "OllamaActionExecutor.hpp"
#include "OllamaActionWorkflow.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

/** Structured tool result for agent loop feedback: {tool, ok, changed, message, blocker?}. */
nlohmann::json ollama_tool_result_json(const std::string& tool, const OllamaActionResult& result,
                                       const nlohmann::json& payload = nlohmann::json::object());

nlohmann::json ollama_tool_results_from_workflow(const nlohmann::json& root, const OllamaWorkflowRun& run);

nlohmann::json ollama_tool_results_from_executor(const nlohmann::json& root,
                                                 const std::vector<OllamaActionResult>& results);

std::string ollama_format_agent_completion_report(const std::vector<nlohmann::json>& step_results, bool korean);

/** User-facing summary lines from agent step tool results (mesh + config). */
std::string ollama_format_agent_step_summary(const std::vector<nlohmann::json>& step_results, bool korean);

/** Prefer executed-change summary; fall back to LLM text only when nothing changed. */
std::string ollama_user_facing_summary(const std::vector<nlohmann::json>& step_tool_results,
                                       const std::string& llm_message, bool korean);

/** Plain-language intro shown in the thinking panel when a turn starts. */
std::string ollama_thinking_goal_intro(const std::string& user_goal, bool korean);

/** Describe planned actions before execution (thinking panel). */
std::string ollama_thinking_planned_actions(const nlohmann::json& root, bool korean);

/** Short label while waiting for the LLM on step N. */
std::string ollama_thinking_step_wait(int step, bool korean);

/** One-line summary after tools run on a step. */
std::string ollama_thinking_after_tools(const nlohmann::json& tool_results, bool korean);

}} // namespace

#endif

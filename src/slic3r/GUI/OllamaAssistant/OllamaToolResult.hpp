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

}} // namespace

#endif

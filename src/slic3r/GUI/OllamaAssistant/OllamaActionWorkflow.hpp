#ifndef slic3r_OllamaActionWorkflow_hpp_
#define slic3r_OllamaActionWorkflow_hpp_

#include "OllamaActionExecutor.hpp"

#include <nlohmann/json.hpp>

class wxWindow;

namespace Slic3r { namespace GUI {

struct OllamaWorkflowRun
{
    bool                             cancelled{ false };
    bool                             preview_only{ false };
    std::vector<OllamaActionResult>  results;
};

/** Show Smart Print workflow UI before running AI actions (when actions is non-empty). */
class OllamaActionWorkflow
{
public:
    static bool            has_executable_actions(const nlohmann::json& root);
    static OllamaWorkflowRun confirm_and_execute(const nlohmann::json& root, wxWindow* parent);
};

}} // namespace

#endif

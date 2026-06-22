#ifndef slic3r_OllamaActionWorkflow_hpp_
#define slic3r_OllamaActionWorkflow_hpp_

#include "OllamaActionExecutor.hpp"

#include "OllamaExecutionPolicy.hpp"
#include "libslic3r/PrintConfig.hpp"

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
    /** Apply with unified execution policy (Assist / Coach). */
    static OllamaWorkflowRun execute_with_policy(const nlohmann::json& root, wxWindow* parent,
                                                 OllamaExecutionPolicy policy);
    /** Apply without modal dialog (AI Coach inline buttons). */
    static OllamaWorkflowRun execute_inline(const nlohmann::json& root, wxWindow* parent);

    /** Preview config after set_config actions (AI Coach explainable UI). */
    static DynamicPrintConfig simulate_proposed_config(const DynamicPrintConfig& before,
                                                       const nlohmann::json& root);
};

}} // namespace

#endif

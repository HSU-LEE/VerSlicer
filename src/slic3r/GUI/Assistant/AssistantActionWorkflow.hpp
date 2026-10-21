#ifndef slic3r_AssistantActionWorkflow_hpp_
#define slic3r_AssistantActionWorkflow_hpp_

#include "AssistantActionExecutor.hpp"

#include "AssistantExecutionPolicy.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <nlohmann/json.hpp>

class wxWindow;

namespace Slic3r { namespace GUI {

struct AssistantWorkflowRun
{
    bool                             cancelled{ false };
    bool                             preview_only{ false };
    std::vector<AssistantActionResult>  results;
};

/** Show Smart Print workflow UI before running AI actions (when actions is non-empty). */
class AssistantActionWorkflow
{
public:
    static bool            has_executable_actions(const nlohmann::json& root);
    static AssistantWorkflowRun confirm_and_execute(const nlohmann::json& root, wxWindow* parent);
    /** Apply with unified execution policy (Assist / Coach). */
    static AssistantWorkflowRun execute_with_policy(const nlohmann::json& root, wxWindow* parent,
                                                 AssistantExecutionPolicy policy);
    /** Apply without modal dialog (AI Coach inline buttons). */
    static AssistantWorkflowRun execute_inline(const nlohmann::json& root, wxWindow* parent);

    /** Preview config after set_config actions (AI Coach explainable UI). */
    static DynamicPrintConfig simulate_proposed_config(const DynamicPrintConfig& before,
                                                       const nlohmann::json& root);
};

}} // namespace

#endif

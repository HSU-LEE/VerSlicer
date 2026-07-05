#include "OllamaMakerWorldActions.hpp"

#include "OllamaChatPanel.hpp"

#include "../MakerWorld/MakerWorldImportFlow.hpp"
#include "../AIPipeline/PrintJobOrchestrator.hpp"

#include "slic3r/GUI/GUI_App.hpp"

#include <string>

namespace Slic3r { namespace GUI {

OllamaActionResult OllamaMakerWorldActions::apply(const nlohmann::json& action)
{
    OllamaActionResult result;
    const std::string  type = action.value("type", "");

    // Phase 3 (flag-gated): route full "find and print" through the SAME
    // panel-owned orchestrator the interactive chat path uses, so the job runs
    // with the panel's real chat/busy callbacks (failures stay visible). Falls
    // through to the legacy MakerWorldImportFlow when no panel is alive or a
    // job is already active.
    if (type == "makerworld_find_and_print" && AIPipeline::print_job_orchestrator_enabled()) {
        const std::string query = action.value("query", std::string{});
        if (!query.empty()) {
            if (OllamaChatPanel* panel = OllamaChatPanel::active_panel()) {
                if (panel->start_orchestrator_find_and_print(query)) {
                    result.success          = true;
                    result.effective_change = true;
                    result.message          = "Starting end-to-end AI print job";
                    return result;
                }
            }
        }
    }

    // The assist loop always operates in apply mode; empty callbacks are fine because
    // the flow surfaces its own picker dialogs and error message boxes.
    const bool handled = MakerWorldImportFlow::run_agent_action(action, wxGetApp().GetTopWindow(),
                                                                /*apply_mode*/ true, /*user_req*/ std::string{}, {});
    if (!handled) {
        result.message = "Unknown action: " + type;
        return result;
    }

    result.success          = true;
    result.effective_change = true;
    if (type == "makerworld_search")
        result.message = "Searching MakerWorld";
    else if (type == "makerworld_find_and_print")
        result.message = "Searching MakerWorld, then slicing and sending to the printer";
    else
        result.message = "Importing model from MakerWorld";
    return result;
}

}} // namespace

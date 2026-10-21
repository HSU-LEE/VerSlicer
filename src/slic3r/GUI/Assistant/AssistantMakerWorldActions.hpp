#ifndef slic3r_AssistantMakerWorldActions_hpp_
#define slic3r_AssistantMakerWorldActions_hpp_

#include "AssistantActionExecutor.hpp" // AssistantActionResult

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI {

/**
 * MakerWorld action dispatch for the agent loop (moved out of
 * AssistantActionExecutor). Handles makerworld_search / import_makerworld /
 * makerworld_find_and_print actions. Main thread only.
 */
class AssistantMakerWorldActions
{
public:
    /** Dispatch a MakerWorld flow action from the agent loop. Routes through the same
     *  MakerWorldImportFlow helper used by the single-shot chat path. The flow runs
     *  asynchronously and drives its own dialogs / notifications. */
    static AssistantActionResult apply(const nlohmann::json& action);
};

}} // namespace

#endif

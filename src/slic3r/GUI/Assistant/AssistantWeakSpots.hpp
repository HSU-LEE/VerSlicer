#ifndef slic3r_AssistantWeakSpots_hpp_
#define slic3r_AssistantWeakSpots_hpp_

#include "AssistantActionExecutor.hpp"

#include "libslic3r/BambuSmartPrint/BambuSmartPrintTypes.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace Slic3r { namespace GUI {

/** "Where will this print go wrong?" in plain terms. Turns Smart Print's
 *  geometry analysis into a short list of weak spots, each with what it means
 *  for the user and the fix to offer, and shows them on the 3D view. */
class AssistantWeakSpots
{
public:
    /** Pure: weak spots for one model, worst first.
     *  [{id, severity: "high"|"medium"|"low", what, fix, fix_tool: {tool, args}}] */
    static nlohmann::json describe(const BambuSmartPrint::ModelAnalysis& a);

    /** Executor handler: analyse the plate, turn on the overhang highlight,
     *  select the model with the worst issue, and return the findings. */
    static AssistantActionResult apply(const nlohmann::json& action);
};

}} // namespace Slic3r::GUI

#endif

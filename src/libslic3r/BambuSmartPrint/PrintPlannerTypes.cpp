#include "PrintPlannerTypes.hpp"

namespace Slic3r {
namespace BambuSmartPrint {

bool PrintGoal::has_intent(PrintGoalIntent i) const
{
    for (PrintGoalIntent v : intents)
        if (v == i)
            return true;
    return false;
}

bool PrintGoal::empty() const { return intents.empty() && user_text.empty(); }

bool PrintPlan::has_actions() const
{
    return root.contains("actions") && root["actions"].is_array() && !root["actions"].empty();
}

std::string PrintPlan::message() const
{
    if (root.contains("message") && root["message"].is_string())
        return root["message"].get<std::string>();
    return explanation.summary;
}

} // namespace BambuSmartPrint
} // namespace Slic3r

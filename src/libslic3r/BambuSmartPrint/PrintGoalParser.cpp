#include "PrintGoalParser.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {
namespace BambuSmartPrint {

namespace {

void add_intent(PrintGoal& goal, PrintGoalIntent intent)
{
    if (std::find(goal.intents.begin(), goal.intents.end(), intent) == goal.intents.end())
        goal.intents.push_back(intent);
}

} // namespace

PrintGoal PrintGoalParser::parse(const std::string& user_text)
{
    PrintGoal goal;
    goal.user_text = user_text;
    return goal;
}

PrintGoal PrintGoalParser::merge(const PrintGoal& session_goal, const PrintGoal& new_goal)
{
    PrintGoal merged = session_goal;
    if (!new_goal.user_text.empty())
        merged.user_text = new_goal.user_text;
    for (PrintGoalIntent i : new_goal.intents)
        add_intent(merged, i);
    merged.weight_cosmetic = std::max(session_goal.weight_cosmetic, new_goal.weight_cosmetic);
    merged.weight_strong   = std::max(session_goal.weight_strong, new_goal.weight_strong);
    merged.weight_fast     = std::max(session_goal.weight_fast, new_goal.weight_fast);
    merged.weight_outdoor  = std::max(session_goal.weight_outdoor, new_goal.weight_outdoor);
    return merged;
}

bool PrintGoalParser::is_recognized_goal(const std::string& user_text)
{
    const PrintGoal goal = parse(user_text);
    for (PrintGoalIntent intent : goal.intents) {
        if (intent != PrintGoalIntent::Unknown)
            return true;
    }
    return false;
}

} // namespace BambuSmartPrint
} // namespace Slic3r

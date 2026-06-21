#include "PrintGoalParser.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {
namespace BambuSmartPrint {

namespace {

bool contains(const std::string& s, const char* needle)
{
    return s.find(needle) != std::string::npos;
}

void add_intent(PrintGoal& goal, PrintGoalIntent intent)
{
    if (std::find(goal.intents.begin(), goal.intents.end(), intent) == goal.intents.end())
        goal.intents.push_back(intent);
}

void bump_weight(float& w, float delta)
{
    w = std::min(1.f, w + delta);
}

} // namespace

PrintGoal PrintGoalParser::parse(const std::string& user_text)
{
    PrintGoal goal;
    goal.user_text = user_text;
    if (user_text.empty())
        return goal;

    const std::string s = user_text;

    if (contains(s, "예쁘") || contains(s, "표면") || contains(s, "거칠") || contains(s, "피규어")
        || contains(s, "pretty") || contains(s, "cosmetic") || contains(s, "smooth")
        || contains(s, "surface") || contains(s, "quality look")) {
        add_intent(goal, PrintGoalIntent::Cosmetic);
        bump_weight(goal.weight_cosmetic, 0.85f);
    }

    if (contains(s, "단단") || contains(s, "튼튼") || contains(s, "strong") || contains(s, "sturdy")
        || contains(s, "solid") || contains(s, "파손") || contains(s, "부서") || contains(s, "부러")
        || contains(s, "약해") || contains(s, "brittle") || contains(s, "break") || contains(s, "fragile")) {
        add_intent(goal, PrintGoalIntent::Strong);
        bump_weight(goal.weight_strong, 0.85f);
    }

    if (contains(s, "빨리") || contains(s, "빠르") || contains(s, "오늘") || contains(s, "급")
        || contains(s, "느려") || contains(s, "느리") || contains(s, "속도가") || contains(s, "속도를")
        || contains(s, "too slow") || contains(s, "fast") || contains(s, "quick") || contains(s, "speed")
        || contains(s, "today") || contains(s, "asap") || contains(s, "hurry") || contains(s, "faster")) {
        add_intent(goal, PrintGoalIntent::Fast);
        bump_weight(goal.weight_fast, 0.85f);
    }

    if (contains(s, "야외") || contains(s, "밖") || contains(s, "outdoor") || contains(s, "weather")
        || contains(s, "UV") || contains(s, "sun")) {
        add_intent(goal, PrintGoalIntent::Outdoor);
        bump_weight(goal.weight_outdoor, 0.8f);
        add_intent(goal, PrintGoalIntent::Strong);
        bump_weight(goal.weight_strong, 0.5f);
    }

    if (contains(s, "들뜸") || contains(s, "안 붙") || contains(s, "안붙") || contains(s, "베드")
        || contains(s, "접착") || contains(s, "stick") || contains(s, "adhesion") || contains(s, "warp")
        || contains(s, "brim") || contains(s, "브림")) {
        add_intent(goal, PrintGoalIntent::Adhesion);
    }

    if (contains(s, "오버행") || contains(s, "overhang") || contains(s, "서포트") || contains(s, "support")
        || contains(s, "공중") || contains(s, "floating") || contains(s, "받침")) {
        add_intent(goal, PrintGoalIntent::Overhang);
    }

    if (goal.intents.empty() && !user_text.empty())
        add_intent(goal, PrintGoalIntent::Unknown);

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

} // namespace BambuSmartPrint
} // namespace Slic3r

#ifndef slic3r_PrintGoalParser_hpp_
#define slic3r_PrintGoalParser_hpp_

#include "PrintPlannerTypes.hpp"

namespace Slic3r {
namespace BambuSmartPrint {

class PrintGoalParser
{
public:
    static PrintGoal parse(const std::string& user_text);
    static PrintGoal merge(const PrintGoal& session_goal, const PrintGoal& new_goal);
    /** True when parse found a concrete intent (not Unknown-only). */
    static bool is_recognized_goal(const std::string& user_text);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

#include "PrintGoalSession.hpp"
#include "PrintGoalParser.hpp"

#include <cmath>

namespace Slic3r {
namespace BambuSmartPrint {

PrintGoalSession& PrintGoalSession::instance()
{
    static PrintGoalSession s;
    return s;
}

void PrintGoalSession::set_goal(const PrintGoal& goal) { m_goal = goal; }

void PrintGoalSession::merge_goal(const PrintGoal& delta)
{
    m_goal = PrintGoalParser::merge(m_goal, delta);
}

void PrintGoalSession::set_last_plan(const PrintPlan& plan)
{
    m_last_plan     = plan;
    m_has_last_plan = true;
    m_last_mesh_height = plan.mesh.height_mm;
    m_last_overhang    = plan.mesh.overhang_face_ratio > 0 ? plan.mesh.overhang_face_ratio
                                                           : plan.mesh.overhang_ratio;
}

void PrintGoalSession::clear()
{
    m_goal          = PrintGoal{};
    m_last_plan     = PrintPlan{};
    m_has_last_plan = false;
    m_last_mesh_height = 0.f;
    m_last_overhang    = 0.f;
}

bool PrintGoalSession::needs_replan(const PlateContext& ctx) const
{
    if (!m_has_last_plan)
        return true;
    if (!ctx.has_model)
        return true;
    const float oh = ctx.mesh.overhang_face_ratio > 0 ? ctx.mesh.overhang_face_ratio : ctx.mesh.overhang_ratio;
    if (std::fabs(ctx.mesh.height_mm - m_last_mesh_height) > 0.5f)
        return true;
    if (std::fabs(oh - m_last_overhang) > 0.05f)
        return true;
    return false;
}

} // namespace BambuSmartPrint
} // namespace Slic3r

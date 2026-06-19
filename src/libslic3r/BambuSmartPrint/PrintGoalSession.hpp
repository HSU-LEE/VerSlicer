#ifndef slic3r_PrintGoalSession_hpp_
#define slic3r_PrintGoalSession_hpp_

#include "PrintPlannerTypes.hpp"

namespace Slic3r {
namespace BambuSmartPrint {

/** Plate-scoped print goal and last plan for incremental replanning. */
class PrintGoalSession
{
public:
    static PrintGoalSession& instance();

    const PrintGoal& goal() const { return m_goal; }
    const PrintPlan& last_plan() const { return m_last_plan; }
    bool             has_last_plan() const { return m_has_last_plan; }

    void set_goal(const PrintGoal& goal);
    void merge_goal(const PrintGoal& delta);
    void set_last_plan(const PrintPlan& plan);
    void clear();

    /** Replan only when goal or mesh fingerprint changed materially. */
    bool needs_replan(const PlateContext& ctx) const;

private:
    PrintGoalSession() = default;

    PrintGoal m_goal;
    PrintPlan m_last_plan;
    bool      m_has_last_plan{ false };
    float     m_last_mesh_height{ 0.f };
    float     m_last_overhang{ 0.f };
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

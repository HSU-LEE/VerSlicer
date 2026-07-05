#ifndef slic3r_OrientationOptimizer_hpp_
#define slic3r_OrientationOptimizer_hpp_

#include "libslic3r/Point.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <string>

namespace Slic3r {

class TriangleMesh;
class ModelInstance;

namespace BambuSmartPrint {

/** Tuning for the orientation search. */
struct OrientationParams {
    double overhang_angle_deg{ 60.0 }; // faces steeper than this need support
    bool   com_tiebreak{ true };       // prefer a more stable orientation among near-equivalent ones
    int    top_candidates{ 6 };        // axis-aligned resting orientations evaluated for the tie-break
};

/**
 * Result of an orientation computation. The rotation is NOT applied to any model here;
 * callers (GUI) are expected to apply `rotation_matrix` via ModelInstance::rotate().
 */
struct OrientationResult {
    bool     computed{ false };
    Matrix3d rotation_matrix{ Matrix3d::Identity() };
    Vec3d    euler_deg{ Vec3d::Zero() };
    double   objective_cost{ 0.0 };     // lower is better (overhang + stability proxy)
    bool     stability_improved{ false }; // true when the CoM tie-break changed the choice
    std::string summary_en;
    std::string summary_ko;
};

/**
 * Headless auto-orientation wrapper around orientation::orient(). Computes the support-optimal
 * orientation and applies a center-of-mass tie-break (via StabilityAnalyzer) to avoid choosing
 * a tip-prone orientation when a near-equivalent, more stable one exists. No wxWidgets / GUI.
 */
class OrientationOptimizer
{
public:
    static OrientationResult optimize_mesh(const TriangleMesh& mesh, const OrientationParams& params = {});

    /** Computes the orientation for an instance's object mesh; does NOT modify the instance. */
    static OrientationResult optimize_instance(const ModelInstance* instance,
                                               const DynamicPrintConfig& config,
                                               const OrientationParams& params = {});
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

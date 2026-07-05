#ifndef slic3r_StabilityAnalyzer_hpp_
#define slic3r_StabilityAnalyzer_hpp_

#include "libslic3r/Point.hpp"

#include <string>

namespace Slic3r {

class TriangleMesh;
class ModelInstance;

namespace BambuSmartPrint {

/**
 * Static stability metrics for a single, oriented mesh (in the frame where +Z is up
 * and the object rests near its own minimum Z). All geometric quantities are headless.
 */
struct StabilityMetrics {
    bool   computed{ false };   // false if the input was empty / degenerate
    bool   com_known{ false };  // false for open / non-manifold meshes (CoM unreliable)

    Vec3d  center_of_mass{ Vec3d::Zero() }; // world/target-frame coordinates (mm)
    double base_z_mm{ 0.0 };                // resting plane height (mm)
    double base_contact_area_mm2{ 0.0 };    // XY-union area of the resting facets
    double com_support_margin_mm{ 0.0 };    // signed: + inside base hull, - outside
    double com_height_mm{ 0.0 };            // CoM height above the base plane
    double tip_over_risk{ 0.0 };            // 0 (stable) .. 1 (will topple)
    bool   stable{ true };

    std::string summary_en;
    std::string summary_ko;
};

/**
 * Headless static-stability analysis: center of mass, base contact area (bottom faces
 * projected and unioned in XY), CoM support margin against the convex hull of the base,
 * and a normalized tip-over risk. No wxWidgets / GUI dependencies.
 */
class StabilityAnalyzer
{
public:
    /**
     * Analyze a mesh already expressed in the target frame (+Z up, resting near min Z).
     * @param mesh_watertight  pass the mesh's manifold state; when false the CoM is left
     *                         unknown and the tip-over estimate is skipped.
     */
    static StabilityMetrics analyze_mesh(const TriangleMesh& mesh, bool mesh_watertight);

    /** Analyze an instance in world coordinates (object mesh transformed by the instance). */
    static StabilityMetrics analyze_instance(const ModelInstance* instance);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

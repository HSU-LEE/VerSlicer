#ifndef slic3r_GeometryAssessmentProducer_hpp_
#define slic3r_GeometryAssessmentProducer_hpp_

#include "AutoConfigEngine.hpp"       // GeometryAssessment
#include "BambuSmartPrintTypes.hpp"   // PrinterLearningProfile
#include "OrientationOptimizer.hpp"   // OrientationParams
#include "PrintPlannerTypes.hpp"      // PlateContext

#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <string>
#include <vector>

namespace Slic3r {
namespace BambuSmartPrint {

/**
 * Unifies MeshGeometryAnalyzer + StabilityAnalyzer + SuccessPredictor + PrintReadinessEngine +
 * OrientationOptimizer into a single GeometryAssessment. Fully headless (no wxWidgets / GUI).
 */
class GeometryAssessmentProducer
{
public:
    struct Options {
        std::string       printer_id;
        bool              run_stability{ true };
        bool              run_orientation{ true };
        OrientationParams orientation;
        // User-provided ctor so `const Options& opts = {}` default arguments below do not
        // require evaluating default member initializers within the enclosing class body.
        Options() {}
    };

    /** Full assessment for the objects on a plate (mesh geometry available). */
    static GeometryAssessment produce_for_objects(const std::vector<ModelObject*>& objects,
                                                  const DynamicPrintConfig& config,
                                                  const PrinterLearningProfile& learning,
                                                  const Options& opts = {});

    /**
     * Assemble an assessment from an already-computed PlateContext. Stability and orientation
     * cannot be recomputed here (no mesh in PlateContext) and are left uncomputed.
     */
    static GeometryAssessment produce_from_plate_context(const PlateContext& ctx,
                                                         const Options& opts = {});
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

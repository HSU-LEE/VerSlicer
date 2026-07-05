#include "GeometryAssessmentProducer.hpp"
#include "MeshGeometryAnalyzer.hpp"
#include "StabilityAnalyzer.hpp"
#include "OrientationOptimizer.hpp"
#include "SuccessPredictor.hpp"
#include "PrintReadinessEngine.hpp"

#include "libslic3r/BoundingBox.hpp"

#include <algorithm>

namespace Slic3r {
namespace BambuSmartPrint {

namespace {

// Aggregate stability across every printable instance, keeping the worst (most tip-prone) case
// and summing the base contact area over the plate.
StabilityMetrics aggregate_stability(const std::vector<ModelObject*>& objects)
{
    StabilityMetrics worst;
    bool any = false;
    bool any_known = false;
    double total_contact = 0.0;
    double max_risk = -1.0;

    for (const ModelObject* obj : objects) {
        if (!obj)
            continue;
        for (const ModelInstance* inst : obj->instances) {
            if (!inst)
                continue;
            const StabilityMetrics sm = StabilityAnalyzer::analyze_instance(inst);
            if (!sm.computed)
                continue;
            any = true;
            total_contact += sm.base_contact_area_mm2;
            if (sm.com_known)
                any_known = true;
            if (sm.tip_over_risk > max_risk) {
                max_risk = sm.tip_over_risk;
                worst    = sm;
            }
        }
    }

    if (!any)
        return StabilityMetrics{}; // computed == false

    // Report the plate-wide contact area while keeping the worst instance's risk/CoM.
    worst.base_contact_area_mm2 = total_contact;
    worst.com_known = any_known && worst.com_known;
    return worst;
}

// Pick the dominant object (largest bounding-box volume) for orientation guidance.
const ModelInstance* pick_representative_instance(const std::vector<ModelObject*>& objects)
{
    const ModelInstance* best = nullptr;
    double best_vol = -1.0;
    for (const ModelObject* obj : objects) {
        if (!obj || obj->instances.empty())
            continue;
        const BoundingBoxf3 bb = obj->bounding_box_exact();
        if (!bb.defined)
            continue;
        const Vec3d s = bb.size();
        const double vol = s.x() * s.y() * s.z();
        if (vol > best_vol) {
            best_vol = vol;
            best     = obj->instances.front();
        }
    }
    return best;
}

} // namespace

GeometryAssessment GeometryAssessmentProducer::produce_for_objects(
    const std::vector<ModelObject*>& objects, const DynamicPrintConfig& config,
    const PrinterLearningProfile& learning, const Options& opts)
{
    GeometryAssessment a;
    a.has_slice = false;

    // 1. Mesh geometry analysis (reuses existing overhang/contact logic).
    a.mesh = MeshGeometryAnalyzer::analyze_objects(objects, config);

    // 2. Static stability (aggregated, worst-case instance).
    if (opts.run_stability) {
        a.stability = aggregate_stability(objects);
        if (a.stability.computed) {
            a.mesh.stability_known       = a.stability.com_known;
            a.mesh.tip_over_risk         = a.stability.tip_over_risk;
            a.mesh.base_contact_area_mm2 = a.stability.base_contact_area_mm2;
        }
    }

    // 3. Auto-orientation guidance (computed only, never applied here).
    if (opts.run_orientation) {
        if (const ModelInstance* inst = pick_representative_instance(objects))
            a.orientation = OrientationOptimizer::optimize_instance(inst, config, opts.orientation);
    }

    // 4. Success prediction & readiness (now stability-aware via a.mesh mirror fields).
    a.prediction = SuccessPredictor::predict(opts.printer_id, a.mesh, config, learning, nullptr);
    a.readiness  = PrintReadinessEngine::evaluate(a.mesh, config, learning, a.prediction, nullptr, 0);

    // 5. Orientation hint compatibility string.
    if (a.orientation.computed && !a.orientation.summary_en.empty())
        a.orientation_hint = a.orientation.summary_en;
    else
        a.orientation_hint = a.mesh.suggested_orientation_hint;

    return a;
}

GeometryAssessment GeometryAssessmentProducer::produce_from_plate_context(const PlateContext& ctx,
                                                                          const Options& /*opts*/)
{
    GeometryAssessment a;
    a.mesh       = ctx.mesh;
    a.slice      = ctx.slice;
    a.has_slice  = ctx.has_slice;
    a.readiness  = ctx.readiness;
    a.prediction = ctx.prediction;
    // No mesh geometry available from PlateContext: stability & orientation remain uncomputed.
    a.orientation_hint = ctx.mesh.suggested_orientation_hint;
    return a;
}

} // namespace BambuSmartPrint
} // namespace Slic3r

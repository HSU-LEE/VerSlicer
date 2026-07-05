#include "OrientationOptimizer.hpp"
#include "StabilityAnalyzer.hpp"
#include "ConfigOptionRead.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Orient.hpp"
#include "libslic3r/Geometry.hpp"

#include <Eigen/Geometry>

#include <algorithm>
#include <array>
#include <cmath>

namespace Slic3r {
namespace BambuSmartPrint {

namespace {

// Weights for the (proxy) objective used only for the CoM tie-break and reporting.
// orientation::orient() does not expose its internal per-candidate cost, so we derive our own
// lightweight comparison metric here (documented deviation from spec).
static constexpr double W_OVERHANG = 1.0;
static constexpr double W_TIPOVER  = 0.6;
// A candidate may only win the tie-break if it does not regress overhang by more than this,
// and improves tip-over risk by at least this delta.
static constexpr double TIEBREAK_MAX_OVERHANG_REGRESSION = 0.05;
static constexpr double TIEBREAK_MIN_RISK_DELTA          = 0.15;
static constexpr double LOW_BASE_CONTACT_PENALTY         = 0.5;
static constexpr double MIN_BASE_CONTACT_MM2             = 4.0;

struct Candidate {
    Matrix3d rotation{ Matrix3d::Identity() };
    double   overhang_frac{ 0.0 };
    double   tip_over_risk{ 0.0 };
    double   base_contact_mm2{ 0.0 };
    double   objective{ 0.0 };
};

// Fraction of surface area that faces downward steeply enough to require support.
double overhang_area_fraction(const TriangleMesh& mesh, double overhang_angle_deg)
{
    const indexed_triangle_set& its = mesh.its;
    // Faces whose (unit) normal Z is below this need support.
    const double support_cos = std::cos(Geometry::deg2rad(180.0 - overhang_angle_deg));
    double total_area = 0.0;
    double support_area = 0.0;
    for (const stl_triangle_vertex_indices& tri : its.indices) {
        const Vec3f v0 = its.vertices[tri[0]];
        const Vec3f v1 = its.vertices[tri[1]];
        const Vec3f v2 = its.vertices[tri[2]];
        const Vec3f cross = (v1 - v0).cross(v2 - v0);
        const double area = 0.5 * double(cross.norm());
        if (area <= 0.0)
            continue;
        total_area += area;
        const double nz = double(cross.normalized().z());
        if (nz < support_cos)
            support_area += area;
    }
    return total_area > 0.0 ? support_area / total_area : 0.0;
}

Candidate evaluate_candidate(const TriangleMesh& base_mesh, const Matrix3d& rotation,
                             bool watertight, double overhang_angle_deg)
{
    Candidate c;
    c.rotation = rotation;
    TriangleMesh rotated = base_mesh;
    rotated.transform(rotation, true /*fix_left_handed*/);

    c.overhang_frac = overhang_area_fraction(rotated, overhang_angle_deg);
    const StabilityMetrics sm = StabilityAnalyzer::analyze_mesh(rotated, watertight);
    c.tip_over_risk    = sm.tip_over_risk;
    c.base_contact_mm2 = sm.base_contact_area_mm2;
    c.objective = W_OVERHANG * c.overhang_frac + W_TIPOVER * c.tip_over_risk;
    if (c.base_contact_mm2 < MIN_BASE_CONTACT_MM2)
        c.objective += LOW_BASE_CONTACT_PENALTY;
    return c;
}

// The six axis-aligned resting orientations (each bounding-box face down).
std::array<Matrix3d, 6> axis_aligned_rotations()
{
    using Eigen::AngleAxisd;
    const Vec3d ux(1, 0, 0), uy(0, 1, 0);
    std::array<Matrix3d, 6> r;
    r[0] = Matrix3d::Identity();
    r[1] = AngleAxisd(M_PI, ux).toRotationMatrix();
    r[2] = AngleAxisd(M_PI / 2.0, ux).toRotationMatrix();
    r[3] = AngleAxisd(-M_PI / 2.0, ux).toRotationMatrix();
    r[4] = AngleAxisd(M_PI / 2.0, uy).toRotationMatrix();
    r[5] = AngleAxisd(-M_PI / 2.0, uy).toRotationMatrix();
    return r;
}

} // namespace

OrientationResult OrientationOptimizer::optimize_mesh(const TriangleMesh& mesh,
                                                      const OrientationParams& params)
{
    OrientationResult result;
    if (mesh.empty())
        return result;

    const bool watertight = mesh.stats().open_edges == 0;

    // 1. Support-optimal orientation from the real optimizer.
    orientation::OrientMeshs items(1);
    items[0].mesh           = mesh;
    items[0].overhang_angle = params.overhang_angle_deg;
    items[0].name           = "smartprint_orient";

    orientation::OrientParams op;
    op.overhang_angle = float(params.overhang_angle_deg);
    op.parallel       = false; // deterministic single-mesh run
    // orientation::_orient() invokes progressind unconditionally; supply a no-op so an empty
    // std::function is never called (which would throw std::bad_function_call).
    op.progressind    = [](unsigned, std::string) {};
    op.stopcondition  = []() { return false; };

    const orientation::OrientMeshs excludes;
    orientation::orient(items, excludes, op);

    Matrix3d chosen_rotation = items[0].rotation_matrix;
    // Guard against an uninitialized/degenerate matrix from the optimizer.
    if (!chosen_rotation.allFinite() || std::abs(chosen_rotation.determinant()) < 1e-6)
        chosen_rotation = Matrix3d::Identity();

    result.computed = true;

    // 2. Evaluate the primary orientation.
    Candidate primary = evaluate_candidate(mesh, chosen_rotation, watertight, params.overhang_angle_deg);
    Candidate best = primary;

    // 3. CoM tie-break: only nudge toward a more stable, near-equivalent orientation.
    if (params.com_tiebreak && primary.tip_over_risk > 0.0) {
        const auto rotations = axis_aligned_rotations();
        const int limit = std::clamp(params.top_candidates, 0, int(rotations.size()));
        for (int i = 0; i < limit; ++i) {
            const Candidate cand = evaluate_candidate(mesh, rotations[i], watertight,
                                                      params.overhang_angle_deg);
            const bool overhang_ok =
                cand.overhang_frac <= primary.overhang_frac + TIEBREAK_MAX_OVERHANG_REGRESSION;
            const bool risk_better =
                cand.tip_over_risk + TIEBREAK_MIN_RISK_DELTA < best.tip_over_risk;
            if (overhang_ok && risk_better) {
                best = cand;
                result.stability_improved = true;
            }
        }
    }

    result.rotation_matrix = best.rotation;
    const Vec3d euler_rad = Geometry::extract_euler_angles(best.rotation);
    result.euler_deg      = Vec3d(Geometry::rad2deg(euler_rad.x()),
                                  Geometry::rad2deg(euler_rad.y()),
                                  Geometry::rad2deg(euler_rad.z()));
    result.objective_cost = best.objective;

    const int risk_pct = int(std::round(best.tip_over_risk * 100.0));
    const bool is_identity = best.rotation.isApprox(Matrix3d::Identity(), 1e-6);
    if (is_identity && !result.stability_improved) {
        result.summary_en = "Current orientation is already support-optimal";
        result.summary_ko = "현재 방향이 이미 서포트에 최적입니다";
    } else if (result.stability_improved) {
        result.summary_en = "Reorient for stability (tip-over risk " + std::to_string(risk_pct)
            + "% after reorientation)";
        result.summary_ko = "안정성을 위해 재배치 권장 (재배치 후 전도 위험 "
            + std::to_string(risk_pct) + "%)";
    } else {
        result.summary_en = "Reorient to reduce supports and overhangs";
        result.summary_ko = "서포트와 오버행을 줄이도록 재배치 권장";
    }
    return result;
}

OrientationResult OrientationOptimizer::optimize_instance(const ModelInstance* instance,
                                                          const DynamicPrintConfig& config,
                                                          const OrientationParams& params)
{
    if (!instance || !instance->get_object())
        return OrientationResult{};

    OrientationParams p = params;
    if (config.has("support_threshold_angle")) {
        const int v = config_get_int(config, "support_threshold_angle", 0);
        if (v > 0)
            p.overhang_angle_deg = double(v);
    }

    // Orientation is computed on the object mesh in its local frame, matching
    // orientation::orient(ModelInstance*); the caller composes it onto the instance.
    const TriangleMesh mesh = instance->get_object()->mesh();
    return optimize_mesh(mesh, p);
}

} // namespace BambuSmartPrint
} // namespace Slic3r

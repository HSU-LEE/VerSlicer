#include "StabilityAnalyzer.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Slic3r {
namespace BambuSmartPrint {

namespace {

// A facet is treated as part of the resting base when its highest vertex is within
// this margin of the mesh bottom plane.
static constexpr double BASE_FACE_Z_EPS_MM = 0.20;
// Base contact smaller than this is effectively a point/edge contact (unstable).
static constexpr double MIN_BASE_CONTACT_MM2 = 4.0;
// Toppling angle (deg) at/above which the object is considered fully stable.
static constexpr double TIPOVER_SAFE_ANGLE_DEG = 22.0;
// Tip-over risk above this flags the object as not stable.
static constexpr double STABLE_RISK_THRESHOLD = 0.5;
// Signed volume below this magnitude means the CoM cannot be trusted.
static constexpr double COM_VOLUME_EPS = 1e-6;

// Volume-weighted centroid (center of mass, uniform density) via tetrahedron summation,
// following the same divergence-theorem pattern as its_volume().
bool compute_center_of_mass(const indexed_triangle_set& its, Vec3d& com_out)
{
    if (its.indices.empty() || its.vertices.empty())
        return false;

    const Vec3d p0 = its.vertices.front().cast<double>();
    double total_volume = 0.0;
    Vec3d  weighted_centroid = Vec3d::Zero();

    for (const stl_triangle_vertex_indices& tri : its.indices) {
        const Vec3d a = its.vertices[tri[0]].cast<double>() - p0;
        const Vec3d b = its.vertices[tri[1]].cast<double>() - p0;
        const Vec3d c = its.vertices[tri[2]].cast<double>() - p0;
        // Signed volume of the tetrahedron (origin, a, b, c) * 6.
        const double dv = a.dot(b.cross(c));
        total_volume += dv;
        // Centroid of that tetrahedron is (a + b + c) / 4 (origin contributes 0).
        weighted_centroid += (a + b + c) * dv;
    }

    if (std::abs(total_volume) < COM_VOLUME_EPS)
        return false;

    // weighted_centroid currently sums dv*(a+b+c); divide by (4 * total_volume) then shift by p0.
    com_out = p0 + weighted_centroid / (4.0 * total_volume);
    return true;
}

// Minimum distance (mm) from a point to the boundary of a polygon (scaled coordinates).
double distance_to_polygon_boundary_mm(const Polygon& poly, const Vec2d& pt_mm)
{
    double best = std::numeric_limits<double>::max();
    const size_t n = poly.points.size();
    if (n < 2)
        return 0.0;
    for (size_t i = 0; i < n; ++i) {
        const Vec2d a = unscale(poly.points[i]);
        const Vec2d b = unscale(poly.points[(i + 1) % n]);
        const Vec2d ab = b - a;
        const double len2 = ab.squaredNorm();
        double t = 0.0;
        if (len2 > 0.0)
            t = std::clamp((pt_mm - a).dot(ab) / len2, 0.0, 1.0);
        const Vec2d proj = a + ab * t;
        best = std::min(best, (pt_mm - proj).norm());
    }
    return best == std::numeric_limits<double>::max() ? 0.0 : best;
}

} // namespace

StabilityMetrics StabilityAnalyzer::analyze_mesh(const TriangleMesh& mesh, bool mesh_watertight)
{
    StabilityMetrics m;
    if (mesh.empty())
        return m;
    m.computed = true;

    const indexed_triangle_set& its = mesh.its;
    const BoundingBoxf3 bb = mesh.bounding_box();
    m.base_z_mm = bb.min.z();

    // --- Base contact: resting facets projected & unioned in XY ---
    Polygons bottom_tris;
    Points   base_pts;
    bottom_tris.reserve(its.indices.size() / 8 + 1);
    for (const stl_triangle_vertex_indices& tri : its.indices) {
        const Vec3f v0 = its.vertices[tri[0]];
        const Vec3f v1 = its.vertices[tri[1]];
        const Vec3f v2 = its.vertices[tri[2]];
        const double zmax = std::max({ double(v0.z()), double(v1.z()), double(v2.z()) });
        if (zmax > m.base_z_mm + BASE_FACE_Z_EPS_MM)
            continue;

        Polygon t;
        t.points = { Point::new_scale(v0.x(), v0.y()),
                     Point::new_scale(v1.x(), v1.y()),
                     Point::new_scale(v2.x(), v2.y()) };
        const double a = t.area();
        if (std::abs(a) <= 0.0)
            continue;
        if (a < 0.0)
            std::reverse(t.points.begin(), t.points.end());
        bottom_tris.emplace_back(std::move(t));
        base_pts.push_back(Point::new_scale(v0.x(), v0.y()));
        base_pts.push_back(Point::new_scale(v1.x(), v1.y()));
        base_pts.push_back(Point::new_scale(v2.x(), v2.y()));
    }

    Polygon base_hull;
    if (!bottom_tris.empty()) {
        const ExPolygons unioned = union_ex(bottom_tris);
        double area_scaled = 0.0;
        for (const ExPolygon& ep : unioned)
            area_scaled += ep.area();
        m.base_contact_area_mm2 = unscale<double>(unscale<double>(area_scaled));
        base_hull = Geometry::convex_hull(base_pts);
    }

    // --- Center of mass (needs a watertight mesh to be meaningful) ---
    Vec3d com;
    if (mesh_watertight && compute_center_of_mass(its, com)) {
        m.com_known       = true;
        m.center_of_mass  = com;
        m.com_height_mm   = std::max(0.0, com.z() - m.base_z_mm);
    }

    // --- Support margin & tip-over risk ---
    if (m.com_known && base_hull.points.size() >= 3) {
        const Vec2d com_xy(m.center_of_mass.x(), m.center_of_mass.y());
        const double edge_dist = distance_to_polygon_boundary_mm(base_hull, com_xy);
        const bool inside = base_hull.contains(Point::new_scale(com_xy.x(), com_xy.y()));
        m.com_support_margin_mm = inside ? edge_dist : -edge_dist;

        const double height = std::max(m.com_height_mm, 1e-3);
        const double topple_angle_deg = Geometry::rad2deg(std::atan2(m.com_support_margin_mm, height));
        double risk = (TIPOVER_SAFE_ANGLE_DEG - topple_angle_deg) / TIPOVER_SAFE_ANGLE_DEG;
        risk = std::clamp(risk, 0.0, 1.0);
        if (m.base_contact_area_mm2 < MIN_BASE_CONTACT_MM2)
            risk = std::max(risk, 0.85);
        m.tip_over_risk = risk;
        m.stable = risk < STABLE_RISK_THRESHOLD && m.base_contact_area_mm2 >= MIN_BASE_CONTACT_MM2;
    } else {
        // Without a trustworthy CoM we cannot judge toppling; fall back to contact area only.
        m.stable = m.base_contact_area_mm2 >= MIN_BASE_CONTACT_MM2;
        m.tip_over_risk = m.stable ? 0.0 : 0.6;
    }

    // --- Summaries (headless strings) ---
    const int risk_pct = int(std::round(m.tip_over_risk * 100.0));
    if (!m.com_known) {
        m.summary_en = "Stability estimate limited (mesh not watertight); base contact "
            + std::to_string(int(std::round(m.base_contact_area_mm2))) + " mm2";
        m.summary_ko = "메시가 완전 밀폐가 아니어서 안정성 추정이 제한됩니다 (바닥 접촉 "
            + std::to_string(int(std::round(m.base_contact_area_mm2))) + " mm2)";
    } else if (m.stable) {
        m.summary_en = "Stable: tip-over risk " + std::to_string(risk_pct) + "%, base contact "
            + std::to_string(int(std::round(m.base_contact_area_mm2))) + " mm2";
        m.summary_ko = "안정적: 전도 위험 " + std::to_string(risk_pct) + "%, 바닥 접촉 "
            + std::to_string(int(std::round(m.base_contact_area_mm2))) + " mm2";
    } else {
        m.summary_en = "Tip-over risk " + std::to_string(risk_pct)
            + "% — consider reorienting or adding a brim/support";
        m.summary_ko = "전도 위험 " + std::to_string(risk_pct)
            + "% — 재배치하거나 브림/서포트 추가를 고려하세요";
    }
    return m;
}

StabilityMetrics StabilityAnalyzer::analyze_instance(const ModelInstance* instance)
{
    StabilityMetrics m;
    if (!instance || !instance->get_object())
        return m;

    TriangleMesh mesh = instance->get_object()->mesh();
    if (mesh.empty())
        return m;
    // World coordinates (rotation, scale, mirror and offset applied).
    instance->transform_mesh(&mesh);
    const bool watertight = mesh.stats().open_edges == 0;
    return analyze_mesh(mesh, watertight);
}

} // namespace BambuSmartPrint
} // namespace Slic3r

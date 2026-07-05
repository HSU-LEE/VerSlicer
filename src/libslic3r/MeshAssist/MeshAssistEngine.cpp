#include "MeshAssistEngine.hpp"

#include "libslic3r/MeshBoolean.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/AABBTreeIndirect.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Slic3r {
namespace MeshAssist {

namespace {

// A component whose lowest point sits this far above the global bottom is treated as floating.
static constexpr double FLOATING_COMPONENT_Z_EPS_MM = 0.30;
// Ray origin is nudged this far into the solid to avoid re-hitting the originating facet.
static constexpr double THIN_WALL_RAY_ORIGIN_EPS_MM = 0.03;
// Thin-wall hits closer than this are ignored as numerical noise.
static constexpr double THIN_WALL_MIN_VALID_MM = 0.01;

TriangleMesh make_cylinder_at(const Vec3d& center, double radius_mm, double height_mm)
{
    TriangleMesh cyl = make_cylinder(radius_mm, height_mm);
    const Vec3d    half(0, 0, height_mm * 0.5);
    translate_vertices(cyl, center - half);
    return cyl;
}

TriangleMesh make_box_at(const Vec3d& center, const Vec3d& size_mm)
{
    TriangleMesh box = make_cube(size_mm.x(), size_mm.y(), size_mm.z());
    translate_vertices(box, center - size_mm * 0.5);
    return box;
}

void add_defect(MeshHealthReport& r, MeshDefectKind kind, MeshDefectSeverity sev, int count,
                double metric, std::string detail)
{
    MeshDefect d;
    d.kind     = kind;
    d.severity = sev;
    d.count    = count;
    d.metric   = metric;
    d.detail   = std::move(detail);
    r.defects.push_back(std::move(d));
}

// Count components (patches) whose lowest Z sits above the global mesh bottom.
int count_floating_components(const indexed_triangle_set& its, double global_min_z)
{
    int floating = 0;
    const std::vector<indexed_triangle_set> parts = its_split(its);
    if (parts.size() <= 1)
        return 0;
    for (const indexed_triangle_set& part : parts) {
        if (part.vertices.empty())
            continue;
        const BoundingBoxf3 pbb = bounding_box(part);
        if (pbb.min.z() > global_min_z + FLOATING_COMPONENT_Z_EPS_MM)
            ++floating;
    }
    return floating;
}

// Minimum wall thickness via inward ray sampling. Returns 0.0 if nothing measured.
double measure_min_wall_thickness(const indexed_triangle_set& its, double threshold_mm, int sample_limit)
{
    if (its.indices.empty() || its.vertices.empty())
        return 0.0;

    auto tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(its.vertices, its.indices);
    if (tree.empty())
        return 0.0;

    const size_t face_count = its.indices.size();
    const size_t stride     = sample_limit > 0 ? std::max<size_t>(1, face_count / size_t(sample_limit)) : 1;
    // Search window: only care about thicknesses at or below the threshold (plus headroom).
    const double search_max = threshold_mm * 4.0;

    double min_thickness = std::numeric_limits<double>::max();
    for (size_t fi = 0; fi < face_count; fi += stride) {
        const stl_triangle_vertex_indices& tri = its.indices[fi];
        const Vec3f v0 = its.vertices[tri[0]];
        const Vec3f v1 = its.vertices[tri[1]];
        const Vec3f v2 = its.vertices[tri[2]];
        const Vec3f cross = (v1 - v0).cross(v2 - v0);
        const float area2 = cross.norm();
        if (area2 <= 0.f)
            continue;
        const Vec3d normal   = cross.normalized().cast<double>();
        if (!normal.allFinite())
            continue;
        const Vec3d centroid = ((v0 + v1 + v2).cast<double>()) / 3.0;
        // Shoot into the solid (opposite the outward normal).
        const Vec3d dir    = -normal;
        const Vec3d origin = centroid + dir * THIN_WALL_RAY_ORIGIN_EPS_MM;

        igl::Hit<float> hit;
        if (!AABBTreeIndirect::intersect_ray_first_hit(its.vertices, its.indices, tree, origin, dir, hit))
            continue;
        const double dist = double(hit.t) + THIN_WALL_RAY_ORIGIN_EPS_MM;
        if (dist < THIN_WALL_MIN_VALID_MM || dist > search_max)
            continue;
        if (hit.id < 0 || size_t(hit.id) >= face_count)
            continue;
        // The opposite wall should face back toward the origin.
        const Vec3d hit_normal = its_face_normal(its, int(hit.id)).cast<double>();
        if (hit_normal.dot(dir) >= 0.0)
            continue;
        min_thickness = std::min(min_thickness, dist);
    }

    if (min_thickness == std::numeric_limits<double>::max())
        return 0.0;
    return min_thickness;
}

} // namespace

MeshHealthReport analyze(const TriangleMesh& mesh)
{
    // Back-compat entry point: keep it light-weight (no CGAL self-intersection test and no
    // thin-wall ray sampling) so existing callers do not pay for the heavier detections.
    MeshAnalysisParams basic;
    basic.detect_self_intersection = false;
    basic.detect_thin_walls        = false;
    return analyze(mesh, basic);
}

MeshHealthReport analyze(const TriangleMesh& mesh, const MeshAnalysisParams& params)
{
    MeshHealthReport r;
    if (mesh.empty()) {
        r.summary = "Empty mesh";
        return r;
    }
    const TriangleMeshStats& st = mesh.stats();
    r.valid                 = true;
    r.manifold              = st.manifold();
    r.open_edges            = st.open_edges;
    r.degenerate_facets     = st.repaired_errors.degenerate_facets;
    r.volume_mm3            = std::abs(st.volume);
    const BoundingBoxf3 bb  = mesh.bounding_box();
    r.size_mm               = bb.size();

    // Connected components (patches).
    r.component_count       = int(its_number_of_patches(mesh.its));
    const int stats_parts   = st.number_of_parts > 1 ? int(st.number_of_parts - 1) : 0;
    r.disconnected_facets   = std::max(stats_parts, r.component_count > 1 ? r.component_count - 1 : 0);

    // Floating components (parts not resting on the global bottom).
    if (r.component_count > 1) {
        r.floating_component_count = count_floating_components(mesh.its, bb.min.z());
    }

    // Inverted normals: negative signed volume.
    const float signed_volume = st.volume >= 0.f ? st.volume : its_volume(mesh.its);
    const bool inverted_normals = signed_volume < 0.f;

    // Self-intersection (CGAL).
    if (params.detect_self_intersection) {
        r.self_intersects = MeshBoolean::cgal::does_self_intersect(mesh);
    }

    // Thin walls (inward ray sampling), threshold from line width.
    const double thin_threshold = std::max(0.01, params.line_width_mm * params.thin_wall_factor);
    if (params.detect_thin_walls) {
        const double min_wall = measure_min_wall_thickness(mesh.its, thin_threshold,
                                                            params.thin_wall_sample_limit);
        if (min_wall > 0.0 && min_wall < thin_threshold)
            r.thin_wall_min_mm = min_wall;
    }

    // --- Build the defect list & policy flags ---
    if (!r.manifold) {
        add_defect(r, MeshDefectKind::NonManifoldEdges, MeshDefectSeverity::Critical, r.open_edges, 0.0,
                   "Mesh has " + std::to_string(r.open_edges) + " non-manifold/open edge(s)");
        r.needs_repair_flag = true;
    }
    if (r.degenerate_facets > 0) {
        add_defect(r, MeshDefectKind::DegenerateFacets, MeshDefectSeverity::Warning, r.degenerate_facets,
                   0.0, "Mesh has " + std::to_string(r.degenerate_facets) + " degenerate facet(s)");
        r.needs_repair_flag = true;
    }
    if (r.component_count > 1) {
        add_defect(r, MeshDefectKind::DisconnectedComponents, MeshDefectSeverity::Warning,
                   r.component_count, 0.0,
                   "Mesh is split into " + std::to_string(r.component_count) + " disconnected component(s)");
    }
    if (r.floating_component_count > 0) {
        add_defect(r, MeshDefectKind::FloatingComponents, MeshDefectSeverity::Critical,
                   r.floating_component_count, 0.0,
                   std::to_string(r.floating_component_count)
                       + " component(s) float above the base and will not print without support");
        r.needs_user_action = true;
    }
    if (inverted_normals) {
        add_defect(r, MeshDefectKind::InvertedNormals, MeshDefectSeverity::Critical, 0, double(signed_volume),
                   "Mesh normals appear inverted (negative volume)");
        r.needs_repair_flag = true;
    }
    if (r.self_intersects) {
        add_defect(r, MeshDefectKind::SelfIntersection, MeshDefectSeverity::Warning, 0, 0.0,
                   "Mesh self-intersects");
        r.needs_repair_flag = true;
    }
    if (r.thin_wall_min_mm > 0.0) {
        add_defect(r, MeshDefectKind::ThinWalls, MeshDefectSeverity::Warning, 0, r.thin_wall_min_mm,
                   "Thin walls detected (min ~" + std::to_string(r.thin_wall_min_mm)
                       + " mm, below one line width)");
        r.needs_user_action = true;
    }

    // Summary line.
    if (r.defects.empty()) {
        r.summary = "Mesh is manifold and healthy";
    } else if (!r.manifold) {
        r.summary = "Mesh has " + std::to_string(r.open_edges) + " non-manifold edge(s)";
    } else {
        r.summary = std::to_string(r.defects.size()) + " geometry issue(s) detected";
    }
    return r;
}

bool repair(TriangleMesh& mesh, std::string* error)
{
    if (mesh.empty()) {
        if (error)
            *error = "Empty mesh";
        return false;
    }

    std::string cgal_err;
    if (!MeshBoolean::cgal::repair(mesh, nullptr, &cgal_err)) {
        if (error)
            *error = cgal_err.empty() ? "CGAL repair failed" : cgal_err;
        return false;
    }
    return !mesh.empty();
}

bool repair_with_report(TriangleMesh& mesh, const MeshAnalysisParams& params,
                        MeshHealthReport* before, MeshHealthReport* after, std::string* error)
{
    if (before)
        *before = analyze(mesh, params);

    if (mesh.empty()) {
        if (error)
            *error = "Empty mesh";
        return false;
    }

    if (!repair(mesh, error))
        return false;

    if (after)
        *after = analyze(mesh, params);
    return true;
}

bool mirror(TriangleMesh& mesh, MirrorAxis axis)
{
    if (mesh.empty())
        return false;
    indexed_triangle_set& its = mesh.its;
    const Vec3d           c = mesh.bounding_box().center();
    for (stl_vertex& v : its.vertices) {
        Vec3d p = v.cast<double>() - c;
        switch (axis) {
        case MirrorAxis::X: p.x() = -p.x(); break;
        case MirrorAxis::Y: p.y() = -p.y(); break;
        case MirrorAxis::Z: p.z() = -p.z(); break;
        }
        v = (p + c).cast<float>();
    }
    its_flip_triangles(its);
    return true;
}

bool subtract_cylinder(TriangleMesh& mesh, const Vec3d& center, double radius_mm, double height_mm,
                       std::string* error)
{
    if (mesh.empty() || radius_mm <= 0 || height_mm <= 0) {
        if (error)
            *error = "Invalid mesh or cylinder dimensions";
        return false;
    }
    try {
        TriangleMesh cutter = make_cylinder_at(center, radius_mm, height_mm);
        MeshBoolean::cgal::minus(mesh, cutter);
        return !mesh.empty();
    } catch (const std::exception& ex) {
        if (error)
            *error = ex.what();
        return false;
    }
}

bool union_box(TriangleMesh& mesh, const Vec3d& center, const Vec3d& size_mm, std::string* error)
{
    if (size_mm.x() <= 0 || size_mm.y() <= 0 || size_mm.z() <= 0) {
        if (error)
            *error = "Invalid box size";
        return false;
    }
    try {
        TriangleMesh addon = make_box_at(center, size_mm);
        if (mesh.empty()) {
            mesh = std::move(addon);
            return true;
        }
        MeshBoolean::cgal::plus(mesh, addon);
        return !mesh.empty();
    } catch (const std::exception& ex) {
        if (error)
            *error = ex.what();
        return false;
    }
}

void scale_uniform(TriangleMesh& mesh, double factor)
{
    if (mesh.empty() || factor <= 0)
        return;
    const Vec3d c = mesh.bounding_box().center();
    for (stl_vertex& v : mesh.its.vertices) {
        Vec3d p = v.cast<double>() - c;
        p *= factor;
        v = (p + c).cast<float>();
    }
}

void translate_vertices(TriangleMesh& mesh, const Vec3d& delta)
{
    if (mesh.empty())
        return;
    for (stl_vertex& v : mesh.its.vertices)
        v += delta.cast<float>();
}

} // namespace MeshAssist
} // namespace Slic3r

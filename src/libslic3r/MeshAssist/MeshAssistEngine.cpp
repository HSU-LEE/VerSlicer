#include "MeshAssistEngine.hpp"

#include "libslic3r/MeshBoolean.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <cmath>

namespace Slic3r {
namespace MeshAssist {

namespace {

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

} // namespace

MeshHealthReport analyze(const TriangleMesh& mesh)
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
    r.disconnected_facets   = st.number_of_parts > 1 ? static_cast<int>(st.number_of_parts - 1) : 0;
    r.volume_mm3            = std::abs(st.volume);
    const BoundingBoxf3 bb  = mesh.bounding_box();
    r.size_mm               = bb.size();
    if (r.manifold && st.repaired())
        r.summary = "Mesh is manifold";
    else if (r.manifold)
        r.summary = "Mesh is manifold";
    else
        r.summary = "Mesh has " + std::to_string(r.open_edges) + " non-manifold edge(s)";
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

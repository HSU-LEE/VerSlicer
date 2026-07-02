#ifndef slic3r_MeshAssistEngine_hpp_
#define slic3r_MeshAssistEngine_hpp_

#include "libslic3r/TriangleMesh.hpp"

#include <string>

namespace Slic3r {
namespace MeshAssist {

struct MeshHealthReport {
    bool        valid{ false };
    bool        manifold{ false };
    int         open_edges{ 0 };
    int         degenerate_facets{ 0 };
    int         disconnected_facets{ 0 };
    double      volume_mm3{ 0.0 };
    Vec3d       size_mm{ Vec3d::Zero() };
    std::string summary;
};

/** True when mesh has defects that repair() may fix before slicing. */
inline bool needs_repair(const MeshHealthReport& r)
{
    return r.valid && (!r.manifold || r.open_edges > 0 || r.degenerate_facets > 0 || r.disconnected_facets > 0);
}

MeshHealthReport analyze(const TriangleMesh& mesh);

/** CGAL-based repair (non-manifold, holes, normals). Returns false on hard failure. */
bool repair(TriangleMesh& mesh, std::string* error = nullptr);

enum class MirrorAxis { X, Y, Z };
bool mirror(TriangleMesh& mesh, MirrorAxis axis);

/** Subtract a cylinder aligned to +Z, centered at `center` (mesh coordinates). */
bool subtract_cylinder(TriangleMesh& mesh, const Vec3d& center, double radius_mm, double height_mm,
                       std::string* error = nullptr);

/** Union a box primitive (axis-aligned) at center with given size. */
bool union_box(TriangleMesh& mesh, const Vec3d& center, const Vec3d& size_mm, std::string* error = nullptr);

/** Uniform scale of mesh vertices about origin. */
void scale_uniform(TriangleMesh& mesh, double factor);

void translate_vertices(TriangleMesh& mesh, const Vec3d& delta);

} // namespace MeshAssist
} // namespace Slic3r

#endif

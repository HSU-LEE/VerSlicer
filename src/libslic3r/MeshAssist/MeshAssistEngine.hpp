#ifndef slic3r_MeshAssistEngine_hpp_
#define slic3r_MeshAssistEngine_hpp_

#include "libslic3r/TriangleMesh.hpp"

#include <string>
#include <vector>

namespace Slic3r {
namespace MeshAssist {

/** Category of a detected mesh defect. */
enum class MeshDefectKind : int {
    None = 0,
    NonManifoldEdges,
    OpenEdges,
    DegenerateFacets,
    DisconnectedComponents,
    FloatingComponents,
    InvertedNormals,
    SelfIntersection,
    ThinWalls,
};

/** How strongly a defect should be surfaced to the user / repair pipeline. */
enum class MeshDefectSeverity : int {
    Info = 0,
    Warning,
    Critical,
};

/** A single detected defect with a headless (English) explanation. */
struct MeshDefect {
    MeshDefectKind     kind{ MeshDefectKind::None };
    MeshDefectSeverity severity{ MeshDefectSeverity::Info };
    int                count{ 0 };    // affected primitives (edges / facets / components)
    double             metric{ 0.0 }; // kind-specific value (e.g. thin-wall min thickness in mm)
    std::string        detail;        // English, headless (no wx / translation)
};

/** Tuning knobs for analyze(). Thresholds derive from the active print config. */
struct MeshAnalysisParams {
    double line_width_mm{ 0.42 };        // nominal extrusion width (from config)
    double thin_wall_factor{ 1.0 };      // thin-wall threshold = line_width_mm * factor
    bool   detect_thin_walls{ true };
    bool   detect_self_intersection{ true };
    int    thin_wall_sample_limit{ 20000 }; // cap ray samples for large meshes
};

struct MeshHealthReport {
    // --- Original fields (kept for back-compat) ---
    bool        valid{ false };
    bool        manifold{ false };
    int         open_edges{ 0 };
    int         degenerate_facets{ 0 };
    int         disconnected_facets{ 0 };
    double      volume_mm3{ 0.0 };
    Vec3d       size_mm{ Vec3d::Zero() };
    std::string summary;

    // --- Phase 5 additive fields ---
    std::vector<MeshDefect> defects;
    int    component_count{ 0 };            // number of disconnected patches
    int    floating_component_count{ 0 };   // components not resting on the global bottom
    double thin_wall_min_mm{ 0.0 };         // 0.0 = not measured / no thin wall found
    bool   self_intersects{ false };
    bool   needs_repair_flag{ false };      // repair() is expected to help
    bool   needs_user_action{ false };      // manual intervention advised (floating parts, thin walls)
};

/** True when the mesh has defects that repair() may fix before slicing. */
inline bool needs_repair(const MeshHealthReport& r)
{
    if (!r.valid)
        return false;
    if (r.needs_repair_flag)
        return true;
    return !r.manifold || r.open_edges > 0 || r.degenerate_facets > 0
        || r.disconnected_facets > 0 || r.self_intersects;
}

/** Basic analysis with default parameters (back-compat entry point). */
MeshHealthReport analyze(const TriangleMesh& mesh);

/** Full analysis: components, floating parts, inverted normals, self-intersection, thin walls. */
MeshHealthReport analyze(const TriangleMesh& mesh, const MeshAnalysisParams& params);

/** CGAL-based repair (non-manifold, holes, normals). Returns false on hard failure. */
bool repair(TriangleMesh& mesh, std::string* error = nullptr);

/**
 * Repair while capturing before/after health snapshots. Any of the out-params may be null.
 * Returns false on hard failure (mesh left unchanged on failure where possible).
 */
bool repair_with_report(TriangleMesh& mesh, const MeshAnalysisParams& params,
                        MeshHealthReport* before = nullptr, MeshHealthReport* after = nullptr,
                        std::string* error = nullptr);

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

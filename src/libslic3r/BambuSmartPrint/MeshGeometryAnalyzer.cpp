#include "MeshGeometryAnalyzer.hpp"
#include "ConfigOptionRead.hpp"
#include "MaterialAdvisor.hpp"

#include "libslic3r/Geometry.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/BoundingBox.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Slic3r {
namespace BambuSmartPrint {

ModelAnalysis MeshGeometryAnalyzer::analyze(const Model& model, const DynamicPrintConfig& config)
{
    return analyze_objects(model.objects, config);
}

ModelAnalysis MeshGeometryAnalyzer::analyze_objects(const std::vector<ModelObject*>& objects,
                                                    const DynamicPrintConfig& config)
{
    ModelAnalysis a;

    int threshold_angle = 50;
    if (config.has("support_threshold_angle")) {
        const int v = config_get_int(config, "support_threshold_angle", 50);
        threshold_angle = v > 0 ? v : 50;
    }
    const double cos_thresh = std::cos(Geometry::deg2rad(double(threshold_angle)));
    const double cos_up     = std::cos(Geometry::deg2rad(12.0)); // ~flat upward face

    double total_face_area  = 0.0;
    double overhang_area    = 0.0;
    double upward_area      = 0.0;
    double max_tilt_deg     = 0.0;
    double bottom_area      = 0.0;
    double top_near_bed_z   = std::numeric_limits<double>::max();

    BoundingBoxf3 bb;
    for (const ModelObject* obj : objects) {
        if (!obj) continue;
        bb.merge(obj->bounding_box_exact());
    }
    if (!bb.defined)
        return a;

    const double global_min_z = bb.min.z();
    const double global_max_z = bb.max.z();
    const double bottom_eps   = 0.08;
    const double top_band     = std::max(0.5, (global_max_z - global_min_z) * 0.04);

    for (const ModelObject* obj : objects) {
        if (!obj) continue;

        for (const ModelVolume* vol : obj->volumes) {
            if (!vol || !vol->is_model_part()) continue;
            const TriangleMesh mesh = vol->mesh();
            a.volume_mm3 += double(mesh.stats().volume);
            const indexed_triangle_set& its = mesh.its;
            const double vol_min_z = mesh.bounding_box().min.z();
            const double vol_max_z = mesh.bounding_box().max.z();

            const size_t vertex_count = its.vertices.size();
            for (size_t fi = 0; fi < its.indices.size(); ++fi) {
                const stl_triangle_vertex_indices& tri = its.indices[fi];
                if (vertex_count == 0)
                    break;
                auto valid = [vertex_count](int idx) {
                    return idx >= 0 && size_t(idx) < vertex_count;
                };
                if (!valid(tri[0]) || !valid(tri[1]) || !valid(tri[2]))
                    continue;
                const Vec3f v0 = its.vertices[tri[0]];
                const Vec3f v1 = its.vertices[tri[1]];
                const Vec3f v2 = its.vertices[tri[2]];

                const Vec3f cross = (v1 - v0).cross(v2 - v0);
                const double area = 0.5 * double(cross.norm());
                if (area <= 0.0)
                    continue;

                Vec3f n = cross.normalized();
                total_face_area += area;

                const double tilt = std::acos(std::clamp(double(n.z()), -1.0, 1.0)) * 180.0 / M_PI;
                max_tilt_deg = std::max(max_tilt_deg, tilt);

                if (double(n.z()) < cos_thresh)
                    overhang_area += area;

                if (n.z() > cos_up) {
                    upward_area += area;
                    const double tri_max_z = std::max({ double(v0.z()), double(v1.z()), double(v2.z()) });
                    if (tri_max_z >= vol_max_z - top_band)
                        top_near_bed_z = std::min(top_near_bed_z, tri_max_z);
                }

                const double tri_min_z = std::min({ double(v0.z()), double(v1.z()), double(v2.z()) });
                if (tri_min_z <= vol_min_z + bottom_eps && n.z() > 0.55)
                    bottom_area += area;
            }
        }
    }

    const Vec3d size = bb.size();
    a.height_mm  = size.z();
    a.max_xy_mm  = std::max(size.x(), size.y());
    const double footprint = std::max(1.0, size.x() * size.y());
    a.min_xy_footprint_mm2 = footprint;

    const double slenderness = a.max_xy_mm > 1.0 ? a.height_mm / a.max_xy_mm : 0.0;
    a.aspect_ratio = slenderness;
    a.tall_narrow  = a.height_mm > 70.0 && a.max_xy_mm < 45.0 && slenderness > 2.2;

    if (total_face_area > 0.0) {
        a.overhang_face_ratio = overhang_area / total_face_area;
        a.overhang_ratio      = a.overhang_face_ratio;
    }
    a.max_overhang_angle_deg = max_tilt_deg;

    a.first_layer_contact_ratio = std::min(1.0, bottom_area / footprint);

    const double upward_ratio = total_face_area > 0.0 ? upward_area / total_face_area : 0.0;
    const bool large_flat_top = upward_ratio > 0.12 && a.height_mm < 35.0 && a.max_xy_mm > 80.0;

    a.needs_brim = a.overhang_face_ratio > 0.10 || a.tall_narrow || a.max_xy_mm > 110.0
                || a.first_layer_contact_ratio < 0.38 || slenderness > 3.5;

    a.is_small_part = a.volume_mm3 > 0.0 && a.volume_mm3 < 12000.0;
    a.thin_feature_risk = a.max_xy_mm < 22.0 && a.height_mm > 30.0 && a.volume_mm3 < 35000.0;

    int complexity = 0;
    complexity += int(std::min(38.0, a.overhang_face_ratio * 130.0));
    if (a.tall_narrow)
        complexity += 16;
    if (a.thin_feature_risk)
        complexity += 14;
    if (a.first_layer_contact_ratio > 0.0 && a.first_layer_contact_ratio < 0.32)
        complexity += 18;
    if (slenderness > 4.0)
        complexity += 12;
    if (a.height_mm > 140.0)
        complexity += 10;
    if (a.max_xy_mm > 170.0)
        complexity += 8;
    if (large_flat_top)
        complexity += 6;
    a.complexity_score = std::min(100, complexity);

    const std::string active = MaterialAdvisor::detect_filament_family(config);
    auto suggest = [&](const char* mat) { a.suggested_material = mat; };

    if (!active.empty())
        a.suggested_material = active;
    else if (a.height_mm > 160.0 || (a.max_xy_mm > 150.0 && a.height_mm > 60.0))
        suggest("ABS");
    else if (a.overhang_face_ratio > 0.28 || large_flat_top)
        suggest("PETG");
    else if (a.is_small_part || a.max_xy_mm < 55.0)
        suggest("PLA");
    else if (a.needs_brim && a.height_mm < 40.0)
        suggest("PLA");
    else
        suggest("PLA");

    if (!active.empty() && MaterialAdvisor::families_compatible(active, a.suggested_material))
        a.suggested_material = active;

    if (config.has("printable_area")) {
        const ConfigOptionPoints* bed_opt = config.opt<ConfigOptionPoints>("printable_area");
        if (bed_opt && !bed_opt->values.empty()) {
            BoundingBoxf bed_bb;
            for (const Vec2d& p : bed_opt->values)
                bed_bb.merge(p);
            const Vec2d bed_size = bed_bb.size();
            const double margin  = 5.0;
            const double avail_x = std::max(1.0, bed_size.x() - 2.0 * margin);
            const double avail_y = std::max(1.0, bed_size.y() - 2.0 * margin);
            const double scale_x = avail_x / std::max(0.1, size.x());
            const double scale_y = avail_y / std::max(0.1, size.y());
            a.bed_scale_factor   = std::min({ 1.0, scale_x, scale_y });
            a.fits_bed           = a.bed_scale_factor >= 0.995;
            if (!a.fits_bed)
                a.suggested_orientation_hint = "Model exceeds the printable area — use Scale to fit";
        }
    }

    if (a.suggested_orientation_hint.empty() && a.tall_narrow)
        a.suggested_orientation_hint = "Lay the model flat for better bed adhesion";

    return a;
}

} // namespace BambuSmartPrint
} // namespace Slic3r

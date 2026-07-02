#include "OllamaMeshOps.hpp"

#include "../GUI_App.hpp"
#include "../GUI_ObjectList.hpp"
#include "../Plater.hpp"
#include "../GLCanvas3D.hpp"
#include "../Selection.hpp"
#include "OllamaActionExecutor.hpp"

#include "libslic3r/MeshAssist/MeshAssistEngine.hpp"
#include "libslic3r/Model.hpp"

#include <boost/format.hpp>

namespace Slic3r { namespace GUI {

namespace {

struct VolumeTarget {
    int obj_idx{ -1 };
    int vol_idx{ 0 };
};

static GLCanvas3D* canvas_for_ops(Plater* plater)
{
    if (!plater)
        return nullptr;
    return plater->get_view3D_canvas3D();
}

static bool resolve_volume_target(Plater* plater, const nlohmann::json& action, VolumeTarget& out)
{
    if (!plater)
        return false;
    GLCanvas3D* canvas = canvas_for_ops(plater);
    if (!canvas)
        return false;

    Selection& sel = canvas->get_selection();
    if (action.contains("object_id") && action["object_id"].is_number_integer()) {
        out.obj_idx = action["object_id"].get<int>();
    } else if (action.contains("object_index") && action["object_index"].is_number_integer()) {
        out.obj_idx = action["object_index"].get<int>();
    } else if (!sel.is_empty()) {
        out.obj_idx = sel.get_object_idx();
    } else if (!plater->model().objects.empty()) {
        out.obj_idx = 0;
    }

    if (out.obj_idx < 0 || static_cast<size_t>(out.obj_idx) >= plater->model().objects.size())
        return false;

    if (action.contains("volume_index") && action["volume_index"].is_number_integer())
        out.vol_idx = action["volume_index"].get<int>();
    else
        out.vol_idx = 0;

    ModelObject* obj = plater->model().objects[out.obj_idx];
    if (!obj || out.vol_idx < 0 || static_cast<size_t>(out.vol_idx) >= obj->volumes.size())
        return false;
    return true;
}

static OllamaActionResult commit_mesh_change(Plater* plater, int obj_idx, ModelVolume* vol, const std::string& msg)
{
    OllamaActionResult result;
    if (!plater || !vol) {
        result.message = "Plater not available";
        return result;
    }
    ModelObject* obj = plater->model().objects[obj_idx];
    if (!obj) {
        result.message = "Object not found";
        return result;
    }

    vol->calculate_convex_hull();
    vol->invalidate_convex_hull_2d();
    vol->set_new_unique_id();
    obj->invalidate_bounding_box();
    obj->ensure_on_bed();

    plater->changed_mesh(obj_idx);
    if (ObjectList* list = wxGetApp().obj_list())
        list->update_item_error_icon(obj_idx, -1);
    if (GLCanvas3D* canvas = canvas_for_ops(plater))
        canvas->reload_scene(false, false);
    OllamaActionExecutor::notify_plater_context_changed(true);

    result.success          = true;
    result.effective_change = true;
    result.message          = msg;
    return result;
}

static bool write_mesh_to_volume(ModelVolume* vol, TriangleMesh mesh)
{
    if (mesh.empty())
        return false;
    vol->set_mesh(std::move(mesh));
    return true;
}

bool health_entry_needs_repair(const nlohmann::json& entry)
{
    if (!entry.is_object())
        return false;
    if (entry.value("needs_repair", false))
        return true;
    if (!entry.value("valid", true))
        return false;
    return !entry.value("manifold", true) || entry.value("open_edges", 0) > 0
        || entry.value("degenerate_facets", 0) > 0 || entry.value("disconnected_facets", 0) > 0;
}

} // namespace

nlohmann::json OllamaMeshOps::summarize_mesh_health(const nlohmann::json& mesh_health)
{
    nlohmann::json summary = nlohmann::json::object();
    if (!mesh_health.is_array() || mesh_health.empty())
        return summary;

    int         volume_count     = 0;
    int         needing_repair   = 0;
    int         total_open_edges = 0;
    bool        any_manifold     = false;
    std::string worst_summary;

    for (const auto& entry : mesh_health) {
        if (!entry.is_object())
            continue;
        ++volume_count;
        if (health_entry_needs_repair(entry))
            ++needing_repair;
        total_open_edges += entry.value("open_edges", 0);
        if (entry.value("manifold", false))
            any_manifold = true;
        if (health_entry_needs_repair(entry) && worst_summary.empty())
            worst_summary = entry.value("summary", std::string{});
    }

    summary["volume_count"]       = volume_count;
    summary["needing_repair"]     = needing_repair;
    summary["plate_needs_repair"] = needing_repair > 0;
    summary["total_open_edges"]   = total_open_edges;
    summary["any_manifold"]       = any_manifold;
    if (!worst_summary.empty())
        summary["worst_summary"] = worst_summary;
    return summary;
}

nlohmann::json OllamaMeshOps::mesh_health_for_plate(Plater* plater)
{
    nlohmann::json arr = nlohmann::json::array();
    if (!plater)
        return arr;
    const Model& model = plater->model();
    for (size_t oi = 0; oi < model.objects.size(); ++oi) {
        const ModelObject* obj = model.objects[oi];
        if (!obj)
            continue;
        for (size_t vi = 0; vi < obj->volumes.size(); ++vi) {
            const MeshAssist::MeshHealthReport h = MeshAssist::analyze(obj->volumes[vi]->mesh());
            arr.push_back({
                {"object_index", static_cast<int>(oi)},
                {"volume_index", static_cast<int>(vi)},
                {"name", obj->name},
                {"valid", h.valid},
                {"manifold", h.manifold},
                {"open_edges", h.open_edges},
                {"degenerate_facets", h.degenerate_facets},
                {"disconnected_facets", h.disconnected_facets},
                {"volume_mm3", h.volume_mm3},
                {"size_mm", {h.size_mm.x(), h.size_mm.y(), h.size_mm.z()}},
                {"needs_repair", MeshAssist::needs_repair(h)},
                {"summary", h.summary},
            });
        }
    }
    return arr;
}

OllamaActionResult OllamaMeshOps::apply_repair_mesh(const nlohmann::json& action)
{
    Plater* plater = wxGetApp().plater();
    VolumeTarget target;
    if (!resolve_volume_target(plater, action, target)) {
        OllamaActionResult r;
        r.message = "Load a model on the plate or select one, then try again.";
        return r;
    }

    ModelObject* obj = plater->model().objects[target.obj_idx];
    ModelVolume* vol = obj->volumes[target.vol_idx];

    const MeshAssist::MeshHealthReport before = MeshAssist::analyze(vol->mesh());
    TriangleMesh                      mesh    = vol->mesh();
    std::string                       err;
    if (!MeshAssist::repair(mesh, &err)) {
        OllamaActionResult r;
        r.message = err.empty() ? "Mesh repair failed" : err;
        return r;
    }
    if (!write_mesh_to_volume(vol, std::move(mesh))) {
        OllamaActionResult r;
        r.message = "Repair produced an empty mesh";
        return r;
    }

    const MeshAssist::MeshHealthReport after = MeshAssist::analyze(vol->mesh());
    return commit_mesh_change(
        plater, target.obj_idx, vol,
        (boost::format("Repaired mesh (%1% → %2% open edges)") % before.open_edges % after.open_edges).str());
}

OllamaActionResult OllamaMeshOps::apply_mirror_mesh(const nlohmann::json& action)
{
    Plater* plater = wxGetApp().plater();
    VolumeTarget target;
    if (!resolve_volume_target(plater, action, target)) {
        OllamaActionResult r;
        r.message = "Load a model on the plate or select one, then try again.";
        return r;
    }

    std::string axis = action.value("axis", "x");
    MeshAssist::MirrorAxis ma = MeshAssist::MirrorAxis::X;
    if (axis == "y" || axis == "Y")
        ma = MeshAssist::MirrorAxis::Y;
    else if (axis == "z" || axis == "Z")
        ma = MeshAssist::MirrorAxis::Z;

    ModelObject* obj = plater->model().objects[target.obj_idx];
    ModelVolume* vol = obj->volumes[target.vol_idx];
    TriangleMesh mesh = vol->mesh();
    if (!MeshAssist::mirror(mesh, ma)) {
        OllamaActionResult r;
        r.message = "Mirror failed";
        return r;
    }
    if (!write_mesh_to_volume(vol, std::move(mesh))) {
        OllamaActionResult r;
        r.message = "Mirror produced an empty mesh";
        return r;
    }

    return commit_mesh_change(plater, target.obj_idx, vol, "Mirrored mesh on axis " + axis);
}

OllamaActionResult OllamaMeshOps::apply_mesh_boolean(const nlohmann::json& action)
{
    Plater* plater = wxGetApp().plater();
    VolumeTarget target;
    if (!resolve_volume_target(plater, action, target)) {
        OllamaActionResult r;
        r.message = "Load a model on the plate or select one, then try again.";
        return r;
    }

    const std::string op  = action.value("operation", "");
    ModelObject*      obj = plater->model().objects[target.obj_idx];
    ModelVolume*      vol = obj->volumes[target.vol_idx];
    TriangleMesh      mesh = vol->mesh();
    const BoundingBoxf3 bb = mesh.bounding_box();
    const Vec3d         center =
        action.contains("center") && action["center"].is_array() && action["center"].size() >= 3
            ? Vec3d(action["center"][0].get<double>(), action["center"][1].get<double>(),
                    action["center"][2].get<double>())
            : bb.center();

    std::string err;
    bool        ok = false;
    if (op == "subtract_cylinder" || op == "drill_hole") {
        const double r = action.value("radius_mm", action.value("radius", 2.0));
        const double h = action.value("height_mm", action.value("height", bb.size().z() + 2.0));
        ok             = MeshAssist::subtract_cylinder(mesh, center, r, h, &err);
    } else if (op == "union_box" || op == "add_handle" || op == "add_rib") {
        Vec3d size(action.value("width_mm", 10.0), action.value("depth_mm", 10.0), action.value("height_mm", 20.0));
        if (action.contains("size_mm") && action["size_mm"].is_array() && action["size_mm"].size() >= 3)
            size = Vec3d(action["size_mm"][0].get<double>(), action["size_mm"][1].get<double>(),
                         action["size_mm"][2].get<double>());
        if (op == "add_rib" && !action.contains("size_mm")) {
            size = Vec3d(std::max(4.0, bb.size().x() * 0.15), std::max(2.0, bb.size().y() * 0.08),
                         std::max(2.0, bb.size().z() * 0.6));
        }
        Vec3d rib_center = center;
        if (op == "add_rib" && !action.contains("center"))
            rib_center = Vec3d(bb.center().x(), bb.min.y() - size.y() * 0.5, bb.center().z());
        ok = MeshAssist::union_box(mesh, rib_center, size, &err);
    } else {
        OllamaActionResult r;
        r.message = "Unknown mesh_boolean operation: " + op;
        return r;
    }

    if (!ok) {
        OllamaActionResult r;
        r.message = err.empty() ? "Boolean operation failed" : err;
        return r;
    }
    if (!write_mesh_to_volume(vol, std::move(mesh))) {
        OllamaActionResult r;
        r.message = "Boolean operation produced an empty mesh";
        return r;
    }

    return commit_mesh_change(plater, target.obj_idx, vol, "Applied mesh operation: " + op);
}

}} // namespace

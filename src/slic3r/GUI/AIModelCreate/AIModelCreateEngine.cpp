#include "AIModelCreateEngine.hpp"

#include "../OllamaAssistant/OllamaActionJsonExtract.hpp"
#include "../OllamaAssistant/OllamaClient.hpp"
#include "../OllamaAssistant/OllamaConfig.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <algorithm>
#include <cmath>
#include <thread>

namespace Slic3r { namespace GUI {

namespace {

constexpr double kDefaultBedMm   = 80.0;
constexpr double kMaxDimMm       = 200.0;
constexpr double kMinPrimitiveMm = 0.5;

double clamp_dim(double v)
{
    if (!std::isfinite(v))
        return kMinPrimitiveMm;
    return std::clamp(v, kMinPrimitiveMm, kMaxDimMm);
}

void translate_mesh(TriangleMesh& mesh, const Vec3d& delta)
{
    for (stl_vertex& v : mesh.its.vertices)
        v = (v.cast<double>() + delta).cast<float>();
}

TriangleMesh mesh_from_primitive_json(const nlohmann::json& prim)
{
    if (!prim.is_object())
        return {};

    const std::string type = prim.value("type", "");
    auto read_vec3 = [](const nlohmann::json& j, const char* key, Vec3d fallback) -> Vec3d {
        if (!j.contains(key) || !j[key].is_array() || j[key].size() < 3)
            return fallback;
        return Vec3d(j[key][0].get<double>(), j[key][1].get<double>(), j[key][2].get<double>());
    };

    const Vec3d center = read_vec3(prim, "center_mm", Vec3d::Zero());

    if (boost::iequals(type, "box")) {
        Vec3d size = read_vec3(prim, "size_mm", Vec3d(20, 20, 20));
        size       = Vec3d(clamp_dim(size.x()), clamp_dim(size.y()), clamp_dim(size.z()));
        TriangleMesh box = make_cube(size.x(), size.y(), size.z());
        translate_mesh(box, center - size * 0.5);
        return box;
    }
    if (boost::iequals(type, "cylinder")) {
        const double radius = clamp_dim(prim.value("radius_mm", 10.0));
        const double height = clamp_dim(prim.value("height_mm", 20.0));
        TriangleMesh cyl    = make_cylinder(radius, height);
        translate_mesh(cyl, center - Vec3d(0, 0, height * 0.5));
        return cyl;
    }
    if (boost::iequals(type, "sphere")) {
        const double radius = clamp_dim(prim.value("radius_mm", 10.0));
        TriangleMesh sph    = make_sphere(radius, M_PI / 36.0);
        translate_mesh(sph, center - Vec3d(0, 0, radius));
        return sph;
    }
    return {};
}

std::string sketch_summary(const std::vector<std::vector<Vec2d>>& strokes_norm, double bed_mm)
{
    if (strokes_norm.empty())
        return "No sketch strokes.";

    std::string out = "Sketch bounding boxes on a " + std::to_string(int(bed_mm)) + "mm bed (mm):\n";
    int           idx = 0;
    for (const auto& stroke : strokes_norm) {
        if (stroke.size() < 2)
            continue;
        BoundingBoxf bbox;
        for (const Vec2d& p : stroke)
            bbox.merge(Vec2d(p.x() * bed_mm - bed_mm * 0.5, p.y() * bed_mm - bed_mm * 0.5));
        if (!bbox.defined)
            continue;
        const Vec2d c = bbox.center();
        const Vec2d s = bbox.size();
        out += "  stroke" + std::to_string(++idx) + ": center=(" + std::to_string(c.x()) + "," + std::to_string(c.y())
            + ") size=(" + std::to_string(s.x()) + "," + std::to_string(s.y()) + ")\n";
    }
    return out;
}

TriangleMesh mesh_from_primitives_json(const nlohmann::json& root)
{
    TriangleMesh merged;
    if (!root.contains("primitives") || !root["primitives"].is_array())
        return merged;

    for (const auto& prim : root["primitives"]) {
        TriangleMesh part = mesh_from_primitive_json(prim);
        if (part.empty())
            continue;
        if (merged.empty())
            merged = std::move(part);
        else
            merged.merge(part);
    }
    return merged;
}

} // namespace

TriangleMesh ai_model_create_mesh_from_sketch(const std::vector<std::vector<Vec2d>>& strokes_norm,
                                              double bed_mm, double default_height_mm)
{
    TriangleMesh result;
    for (const auto& stroke : strokes_norm) {
        if (stroke.size() < 2)
            continue;
        BoundingBoxf bbox;
        for (const Vec2d& p : stroke)
            bbox.merge(Vec2d(p.x() * bed_mm - bed_mm * 0.5, p.y() * bed_mm - bed_mm * 0.5));
        if (!bbox.defined)
            continue;
        Vec2d size = bbox.size();
        size.x()   = std::max(size.x(), 2.0);
        size.y()   = std::max(size.y(), 2.0);
        const Vec3d center(bbox.center().x(), bbox.center().y(), default_height_mm * 0.5);
        const Vec3d box_size(size.x(), size.y(), default_height_mm);
        TriangleMesh box = make_cube(box_size.x(), box_size.y(), box_size.z());
        translate_mesh(box, center - box_size * 0.5);
        if (result.empty())
            result = std::move(box);
        else
            result.merge(box);
    }
    return result;
}

bool ai_model_create_validate_mesh(const TriangleMesh& mesh, std::string* error)
{
    if (mesh.empty()) {
        if (error)
            *error = "Empty mesh";
        return false;
    }
    const TriangleMeshStats& st   = mesh.stats();
    const Vec3d                sz = mesh.bounding_box().size();
    if (sz.x() > kMaxDimMm || sz.y() > kMaxDimMm || sz.z() > kMaxDimMm) {
        if (error)
            *error = "Model exceeds printable size limits";
        return false;
    }
    if (st.number_of_facets == 0 || st.volume <= 0.f) {
        if (error)
            *error = "Invalid mesh geometry";
        return false;
    }
    return true;
}

void ai_model_create_generate_async(const std::string& user_text,
                                    const std::vector<std::vector<Vec2d>>& strokes_norm,
                                    std::function<void(AIModelCreateResult)> callback)
{
    if (!callback)
        return;

    std::thread([user_text, strokes_norm, callback = std::move(callback)]() {
        AIModelCreateResult out;
        TriangleMesh        sketch_mesh;
        if (!strokes_norm.empty())
            sketch_mesh = ai_model_create_mesh_from_sketch(strokes_norm, kDefaultBedMm, 20.0);

        const std::string model = ollama_model_from_config();
        OllamaClient      client(ollama_host_from_config());

        const std::string system =
            "You generate simple 3D printable models as JSON only. "
            "Reply with a single JSON object: {\"primitives\":[{\"type\":\"box|cylinder|sphere\","
            "\"center_mm\":[x,y,z],\"size_mm\":[w,d,h] or \"radius_mm\":r,\"height_mm\":h}]}. "
            "Units are millimeters. Z=0 is the build plate; keep height positive. "
            "Fit within 80x80x80 mm. No markdown, no explanation.";

        const std::string user = std::string("Description:\n") + user_text + "\n\n" + sketch_summary(strokes_norm, kDefaultBedMm);

        std::vector<OllamaMessage> messages{
            {"system", system},
            {"user", user},
        };

        bool done = false;
        client.chat(model, messages, [&](const std::string& text, const std::string& err) {
            if (done)
                return;
            done = true;

            if (!err.empty()) {
                if (!sketch_mesh.empty()) {
                    out.mesh    = std::move(sketch_mesh);
                    out.success = ai_model_create_validate_mesh(out.mesh, &out.error);
                    out.summary = out.success ? "Used sketch geometry (AI unavailable)." : "";
                    if (!out.success && out.error.empty())
                        out.error = err;
                } else {
                    out.success = false;
                    out.error   = err;
                    out.summary = "AI service unavailable. Start Ollama or draw a sketch.";
                }
                callback(std::move(out));
                return;
            }

            try {
                const nlohmann::json root     = extract_ollama_action_json_with_repair(text);
                TriangleMesh         llm_mesh   = mesh_from_primitives_json(root);
                if (!llm_mesh.empty()) {
                    out.mesh = std::move(llm_mesh);
                    if (!sketch_mesh.empty())
                        out.mesh.merge(sketch_mesh);
                } else {
                    out.mesh = std::move(sketch_mesh);
                }
                if (out.mesh.empty())
                    out.mesh = ai_model_create_mesh_from_sketch(strokes_norm);

                its_merge_vertices(out.mesh.its, true);
                out.success = ai_model_create_validate_mesh(out.mesh, &out.error);
                out.summary = out.success ? "Model generated." : "Generation produced an invalid mesh.";
            } catch (const std::exception& ex) {
                if (!sketch_mesh.empty()) {
                    out.mesh    = std::move(sketch_mesh);
                    its_merge_vertices(out.mesh.its, true);
                    out.success = ai_model_create_validate_mesh(out.mesh, &out.error);
                    out.summary = out.success ? "Used sketch geometry (could not parse AI response)." : "";
                }
                if (!out.success && out.error.empty())
                    out.error = ex.what();
            }
            callback(std::move(out));
        });
    }).detach();
}

}} // namespace

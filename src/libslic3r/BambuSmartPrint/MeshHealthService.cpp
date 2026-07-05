#include "MeshHealthService.hpp"
#include "ConfigOptionRead.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {
namespace BambuSmartPrint {

namespace {

const char* defect_kind_key(MeshAssist::MeshDefectKind k)
{
    using K = MeshAssist::MeshDefectKind;
    switch (k) {
    case K::NonManifoldEdges:        return "non_manifold_edges";
    case K::OpenEdges:               return "open_edges";
    case K::DegenerateFacets:        return "degenerate_facets";
    case K::DisconnectedComponents:  return "disconnected_components";
    case K::FloatingComponents:      return "floating_components";
    case K::InvertedNormals:         return "inverted_normals";
    case K::SelfIntersection:        return "self_intersection";
    case K::ThinWalls:               return "thin_walls";
    default:                         return "none";
    }
}

const char* defect_severity_key(MeshAssist::MeshDefectSeverity s)
{
    using S = MeshAssist::MeshDefectSeverity;
    switch (s) {
    case S::Critical: return "critical";
    case S::Warning:  return "warning";
    default:          return "info";
    }
}

nlohmann::json report_to_json(const MeshAssist::MeshHealthReport& r)
{
    nlohmann::json j;
    j["valid"]                    = r.valid;
    j["manifold"]                 = r.manifold;
    j["open_edges"]               = r.open_edges;
    j["degenerate_facets"]        = r.degenerate_facets;
    j["component_count"]          = r.component_count;
    j["floating_component_count"] = r.floating_component_count;
    j["self_intersects"]          = r.self_intersects;
    j["thin_wall_min_mm"]         = r.thin_wall_min_mm;
    j["volume_mm3"]               = r.volume_mm3;
    j["needs_repair"]             = MeshAssist::needs_repair(r);
    j["needs_user_action"]        = r.needs_user_action;
    j["summary"]                  = r.summary;
    j["defects"]                  = nlohmann::json::array();
    for (const MeshAssist::MeshDefect& d : r.defects) {
        j["defects"].push_back(nlohmann::json{
            { "kind",     defect_kind_key(d.kind) },
            { "severity", defect_severity_key(d.severity) },
            { "count",    d.count },
            { "metric",   d.metric },
            { "detail",   d.detail },
        });
    }
    return j;
}

} // namespace

MeshAssist::MeshAnalysisParams MeshHealthService::params_from_config(const DynamicPrintConfig& config)
{
    MeshAssist::MeshAnalysisParams params;

    const double nozzle = double(config_get_float(config, "nozzle_diameter", 0.4f));

    double line_width = 0.0;
    if (config.has("line_width")) {
        if (const auto* fop = dynamic_cast<const ConfigOptionFloatOrPercent*>(config.option("line_width"))) {
            line_width = fop->percent ? (fop->value / 100.0) * nozzle : fop->value;
        } else {
            line_width = double(config_get_float(config, "line_width", 0.f));
        }
    }
    if (line_width <= 0.0)
        line_width = nozzle > 0.0 ? nozzle * 1.05 : 0.42;

    params.line_width_mm = line_width;
    return params;
}

MeshHealthVolumeReport MeshHealthService::analyze_mesh(const TriangleMesh& mesh, const std::string& name,
                                                       const MeshAssist::MeshAnalysisParams& params)
{
    MeshHealthVolumeReport v;
    v.volume_name = name;
    v.report      = MeshAssist::analyze(mesh, params);
    return v;
}

MeshHealthObjectReport MeshHealthService::analyze_object(const ModelObject& obj,
                                                         const MeshAssist::MeshAnalysisParams& params)
{
    MeshHealthObjectReport o;
    o.object_name = obj.name;

    for (const ModelVolume* vol : obj.volumes) {
        if (!vol || !vol->is_model_part())
            continue;
        MeshHealthVolumeReport v = analyze_mesh(vol->mesh(), vol->name, params);
        o.total_volume_mm3 += v.report.volume_mm3;
        if (MeshAssist::needs_repair(v.report))
            o.any_needs_repair = true;
        if (v.report.needs_user_action)
            o.any_needs_user_action = true;
        o.volumes.push_back(std::move(v));
    }
    return o;
}

MeshHealthPlateReport MeshHealthService::analyze_objects(const std::vector<ModelObject*>& objects,
                                                         const MeshAssist::MeshAnalysisParams& params)
{
    MeshHealthPlateReport plate;
    for (const ModelObject* obj : objects) {
        if (!obj)
            continue;
        MeshHealthObjectReport o = analyze_object(*obj, params);
        plate.total_volumes += int(o.volumes.size());
        for (const MeshHealthVolumeReport& v : o.volumes) {
            if (MeshAssist::needs_repair(v.report))
                ++plate.volumes_needing_repair;
            if (v.report.needs_user_action)
                ++plate.volumes_needing_user_action;
        }
        plate.total_volume_mm3 += o.total_volume_mm3;
        plate.any_needs_repair      = plate.any_needs_repair || o.any_needs_repair;
        plate.any_needs_user_action = plate.any_needs_user_action || o.any_needs_user_action;
        plate.objects.push_back(std::move(o));
    }

    if (plate.total_volumes == 0) {
        plate.summary_en = "No printable volumes to check";
        plate.summary_ko = "검사할 출력 가능한 볼륨이 없습니다";
    } else if (!plate.any_needs_repair && !plate.any_needs_user_action) {
        plate.summary_en = "All " + std::to_string(plate.total_volumes) + " volume(s) look healthy";
        plate.summary_ko = "모든 볼륨(" + std::to_string(plate.total_volumes) + "개)이 정상입니다";
    } else {
        plate.summary_en = std::to_string(plate.volumes_needing_repair) + " volume(s) need repair, "
            + std::to_string(plate.volumes_needing_user_action) + " need attention";
        plate.summary_ko = "복구가 필요한 볼륨 " + std::to_string(plate.volumes_needing_repair)
            + "개, 확인이 필요한 볼륨 " + std::to_string(plate.volumes_needing_user_action) + "개";
    }
    return plate;
}

nlohmann::json MeshHealthService::to_json(const MeshHealthPlateReport& report)
{
    nlohmann::json j;
    j["total_volumes"]               = report.total_volumes;
    j["volumes_needing_repair"]      = report.volumes_needing_repair;
    j["volumes_needing_user_action"] = report.volumes_needing_user_action;
    j["any_needs_repair"]            = report.any_needs_repair;
    j["any_needs_user_action"]       = report.any_needs_user_action;
    j["total_volume_mm3"]            = report.total_volume_mm3;
    j["summary_en"]                  = report.summary_en;
    j["summary_ko"]                  = report.summary_ko;
    j["objects"]                     = nlohmann::json::array();
    for (const MeshHealthObjectReport& o : report.objects) {
        nlohmann::json oj;
        oj["object_name"]           = o.object_name;
        oj["any_needs_repair"]      = o.any_needs_repair;
        oj["any_needs_user_action"] = o.any_needs_user_action;
        oj["total_volume_mm3"]      = o.total_volume_mm3;
        oj["volumes"]               = nlohmann::json::array();
        for (const MeshHealthVolumeReport& v : o.volumes) {
            nlohmann::json vj = report_to_json(v.report);
            vj["volume_name"] = v.volume_name;
            oj["volumes"].push_back(std::move(vj));
        }
        j["objects"].push_back(std::move(oj));
    }
    return j;
}

} // namespace BambuSmartPrint
} // namespace Slic3r

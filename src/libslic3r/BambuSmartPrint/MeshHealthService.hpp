#ifndef slic3r_MeshHealthService_hpp_
#define slic3r_MeshHealthService_hpp_

#include "libslic3r/MeshAssist/MeshAssistEngine.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Slic3r {

class TriangleMesh;
class ModelObject;

namespace BambuSmartPrint {

/** Health of one model volume. */
struct MeshHealthVolumeReport {
    std::string                   volume_name;
    MeshAssist::MeshHealthReport  report;
};

/** Aggregated health of all part-volumes of one object. */
struct MeshHealthObjectReport {
    std::string                          object_name;
    std::vector<MeshHealthVolumeReport>  volumes;
    bool   any_needs_repair{ false };
    bool   any_needs_user_action{ false };
    double total_volume_mm3{ 0.0 };
};

/** Plate-wide roll-up across every printable object. */
struct MeshHealthPlateReport {
    std::vector<MeshHealthObjectReport>  objects;
    int    total_volumes{ 0 };
    int    volumes_needing_repair{ 0 };
    int    volumes_needing_user_action{ 0 };
    bool   any_needs_repair{ false };
    bool   any_needs_user_action{ false };
    double total_volume_mm3{ 0.0 };
    std::string summary_en;
    std::string summary_ko;
};

/**
 * Headless mesh-health aggregation over the plate/volumes, deriving analysis parameters from the
 * active print config and emitting JSON for context injection. Depends on MeshAssistEngine.
 */
class MeshHealthService
{
public:
    /** Derive thin-wall threshold (line width) and detection toggles from the config. */
    static MeshAssist::MeshAnalysisParams params_from_config(const DynamicPrintConfig& config);

    static MeshHealthVolumeReport analyze_mesh(const TriangleMesh& mesh, const std::string& name,
                                               const MeshAssist::MeshAnalysisParams& params);

    static MeshHealthObjectReport analyze_object(const ModelObject& obj,
                                                 const MeshAssist::MeshAnalysisParams& params);

    static MeshHealthPlateReport analyze_objects(const std::vector<ModelObject*>& objects,
                                                 const MeshAssist::MeshAnalysisParams& params);

    static nlohmann::json to_json(const MeshHealthPlateReport& report);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

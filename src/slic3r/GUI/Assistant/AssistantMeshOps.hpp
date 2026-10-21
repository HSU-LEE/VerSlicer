#ifndef slic3r_AssistantMeshOps_hpp_
#define slic3r_AssistantMeshOps_hpp_

#include "AssistantActionExecutor.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI {

class Plater;

/** Apply mesh-level operations to the selected (or targeted) model volume. */
class AssistantMeshOps
{
public:
    static AssistantActionResult apply_repair_mesh(const nlohmann::json& action);
    static AssistantActionResult apply_mirror_mesh(const nlohmann::json& action);

    static nlohmann::json mesh_health_for_plate(Plater* plater);

    /** Aggregate per-volume mesh_health: counts, plate_needs_repair, worst summary. */
    static nlohmann::json summarize_mesh_health(const nlohmann::json& mesh_health);
};

}} // namespace

#endif

#ifndef slic3r_OllamaMeshOps_hpp_
#define slic3r_OllamaMeshOps_hpp_

#include "OllamaActionExecutor.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI {

class Plater;

/** Apply mesh-level operations to the selected (or targeted) model volume. */
class OllamaMeshOps
{
public:
    static OllamaActionResult apply_repair_mesh(const nlohmann::json& action);
    static OllamaActionResult apply_mirror_mesh(const nlohmann::json& action);
    static OllamaActionResult apply_mesh_boolean(const nlohmann::json& action);

    static nlohmann::json mesh_health_for_plate(Plater* plater);

    /** Aggregate per-volume mesh_health: counts, plate_needs_repair, worst summary. */
    static nlohmann::json summarize_mesh_health(const nlohmann::json& mesh_health);
};

}} // namespace

#endif

#include "PlateBatchPlanner.hpp"

#include "AutoSettingsEngine.hpp"
#include "ConfigSnapshot.hpp"
#include "MaterialAdvisor.hpp"
#include "MeshGeometryAnalyzer.hpp"
#include "PrintReadinessEngine.hpp"
#include "PrinterLearningStore.hpp"
#include "SuccessPredictor.hpp"

#include "libslic3r/Model.hpp"

namespace Slic3r {
namespace BambuSmartPrint {

PlateWorkflowResult PlateBatchPlanner::evaluate_plate(const PlateWorkflowInput& input,
                                                      const PrinterLearningProfile& learning)
{
    PlateWorkflowResult out;
    if (input.objects.empty())
        return out;

    out.mesh = MeshGeometryAnalyzer::analyze_objects(input.objects, input.base_config);
    out.filament_name = MaterialAdvisor::detect_filament_family(input.base_config);
    DynamicPrintConfig cfg = input.base_config;
    PrinterLearningStore::instance().apply_learning_to_config(input.printer_id, cfg);

    out.auto_result = AutoSettingsEngine::suggest_settings_for_objects(
        input.objects, cfg, &learning, input.slice, input.plate_index);
    out.proposed    = out.auto_result.config_delta;
    out.prediction  = SuccessPredictor::predict(
        input.printer_id, out.mesh, out.proposed, learning, input.slice);
    out.change_count = ConfigSnapshot::diff(input.base_config, out.proposed).size();
    out.readiness    = PrintReadinessEngine::evaluate(
        out.mesh, out.proposed, learning, out.prediction, input.slice, out.change_count);
    return out;
}

PlateBatchSummary PlateBatchPlanner::analyze_all_plates(
    int plate_count,
    const std::function<PlateWorkflowInput(int plate_index)>& plate_input_provider)
{
    PlateBatchSummary summary;
    if (!plate_input_provider || plate_count <= 0)
        return summary;

    float readiness_sum = 0.f;
    int   readiness_count = 0;
    float lowest_score    = 101.f;
    float highest_score   = -1.f;
    size_t best_changes   = 0;
    std::string first_filament_family;
    bool filament_conflict = false;

    for (int i = 0; i < plate_count; ++i) {
        PlateWorkflowInput input = plate_input_provider(i);

        PlateBatchEntry entry;
        entry.plate_index = i;

        if (input.objects.empty()) {
            entry.empty = true;
            summary.plates.push_back(entry);
            continue;
        }

        const PrinterLearningProfile learning =
            PrinterLearningStore::instance().get_profile(input.printer_id);
        const PlateWorkflowResult result = evaluate_plate(input, learning);

        entry.empty               = false;
        entry.readiness_score     = result.readiness.score;
        entry.change_count        = result.change_count;
        entry.suggested_material  = result.mesh.suggested_material;
        entry.active_filament_family = result.filament_name;
        entry.complexity_score    = result.mesh.complexity_score;
        entry.priority_score      = result.readiness.score
            - float(result.change_count) * 0.35f
            + result.prediction.success_rate * 0.15f;
        if (!entry.active_filament_family.empty()) {
            if (first_filament_family.empty())
                first_filament_family = entry.active_filament_family;
            else if (entry.active_filament_family != first_filament_family
                     && !MaterialAdvisor::families_compatible(first_filament_family, entry.active_filament_family))
                filament_conflict = true;
        }
        summary.plates.push_back(entry);
        ++summary.plates_with_models;
        summary.total_suggested_changes += result.change_count;

        readiness_sum += entry.readiness_score;
        ++readiness_count;

        if (entry.readiness_score < lowest_score) {
            lowest_score = entry.readiness_score;
            summary.lowest_readiness_plate = i;
        }
        if (entry.readiness_score > highest_score) {
            highest_score = entry.readiness_score;
            summary.highest_readiness_plate = i;
        }
        if (entry.priority_score >= 0.f) {
            const float best_pri = (summary.best_plate_index >= 0 && summary.best_plate_index < int(summary.plates.size()))
                ? summary.plates[size_t(summary.best_plate_index)].priority_score : -1.f;
            if (summary.best_plate_index < 0 || entry.priority_score > best_pri) {
                best_changes = result.change_count;
                summary.best_plate_index = i;
            }
        } else if (result.change_count >= best_changes) {
            best_changes = result.change_count;
            summary.best_plate_index = i;
        }
    }

    if (readiness_count > 0)
        summary.average_readiness = readiness_sum / float(readiness_count);
    if (filament_conflict)
        summary.filament_conflict_note =
            "Plates use different filament families — verify AMS mapping before batch print";
    return summary;
}

} // namespace BambuSmartPrint
} // namespace Slic3r

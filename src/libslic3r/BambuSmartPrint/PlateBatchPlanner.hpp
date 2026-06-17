#ifndef slic3r_PlateBatchPlanner_hpp_
#define slic3r_PlateBatchPlanner_hpp_

#include "BambuSmartPrintTypes.hpp"
#include <functional>

namespace Slic3r {

class ModelObject;
class DynamicPrintConfig;

namespace BambuSmartPrint {

struct PlateWorkflowInput {
    std::vector<ModelObject*> objects;
    DynamicPrintConfig        base_config;
    std::string               printer_id;
    int                       plate_index{ -1 };
    const SliceAnalysis*      slice{ nullptr };
};

struct PlateWorkflowResult {
    ModelAnalysis      mesh;
    AutoSettingsResult auto_result;
    SuccessPrediction  prediction;
    ReadinessReport    readiness;
    DynamicPrintConfig proposed;
    size_t             change_count{ 0 };
    std::string        filament_name;
};

class PlateBatchPlanner
{
public:
    static PlateWorkflowResult evaluate_plate(const PlateWorkflowInput& input,
                                              const PrinterLearningProfile& learning);

    static PlateBatchSummary analyze_all_plates(
        int plate_count,
        const std::function<PlateWorkflowInput(int plate_index)>& plate_input_provider);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

#ifndef slic3r_PrintReadinessEngine_hpp_
#define slic3r_PrintReadinessEngine_hpp_

#include "BambuSmartPrintTypes.hpp"

namespace Slic3r {
namespace BambuSmartPrint {

class PrintReadinessEngine
{
public:
    static ReadinessTier tier_from_score(float score);

    static ReadinessReport evaluate(const ModelAnalysis& model,
                                    const DynamicPrintConfig& config,
                                    const PrinterLearningProfile& learning,
                                    const SuccessPrediction& prediction,
                                    const SliceAnalysis* slice = nullptr,
                                    size_t pending_setting_changes = 0);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

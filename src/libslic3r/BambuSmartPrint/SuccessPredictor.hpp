#ifndef slic3r_SuccessPredictor_hpp_
#define slic3r_SuccessPredictor_hpp_

#include "BambuSmartPrintTypes.hpp"

namespace Slic3r {
namespace BambuSmartPrint {

class SuccessPredictor
{
public:
    static SuccessPrediction predict(const std::string& printer_id, const ModelAnalysis& model,
                                     const DynamicPrintConfig& config,
                                     const PrinterLearningProfile& learning,
                                     const SliceAnalysis* slice = nullptr);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

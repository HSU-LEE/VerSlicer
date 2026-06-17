#ifndef slic3r_PredictionConfidence_hpp_
#define slic3r_PredictionConfidence_hpp_

#include "BambuSmartPrintTypes.hpp"

namespace Slic3r {
namespace BambuSmartPrint {

void annotate_prediction_confidence(SuccessPrediction& prediction,
                                    const PrinterLearningProfile& learning,
                                    const SliceAnalysis* slice,
                                    bool mesh_analyzed);

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

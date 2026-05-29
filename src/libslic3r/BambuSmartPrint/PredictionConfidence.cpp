#include "PredictionConfidence.hpp"

namespace Slic3r {
namespace BambuSmartPrint {

void annotate_prediction_confidence(SuccessPrediction& prediction,
                                    const PrinterLearningProfile& learning,
                                    const SliceAnalysis* slice,
                                    bool mesh_analyzed)
{
    if (!slice || !slice->valid) {
        prediction.confidence = PredictionConfidence::Low;
        if (mesh_analyzed && learning.total_prints >= 2)
            prediction.confidence = PredictionConfidence::Medium;
        if (learning.total_prints < 10)
            prediction.confidence = PredictionConfidence::Low;
        return;
    }

    int score = 2;
    if (mesh_analyzed)
        score += 1;
    if (learning.total_prints >= 5)
        score += 2;
    else if (learning.total_prints >= 2)
        score += 1;

    if (learning.total_prints < 10)
        score = std::min(score, 3);

    if (score >= 4)
        prediction.confidence = PredictionConfidence::High;
    else if (score >= 2)
        prediction.confidence = PredictionConfidence::Medium;
    else
        prediction.confidence = PredictionConfidence::Low;
}

} // namespace BambuSmartPrint
} // namespace Slic3r

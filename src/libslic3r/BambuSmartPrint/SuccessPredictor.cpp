#include "SuccessPredictor.hpp"
#include "ConfigOptionRead.hpp"
#include "FailureDatabase.hpp"
#include "PrintReadinessEngine.hpp"
#include "PredictionConfidence.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {
namespace BambuSmartPrint {

SuccessPrediction SuccessPredictor::predict(const std::string& printer_id, const ModelAnalysis& model,
                                            const DynamicPrintConfig& config,
                                            const PrinterLearningProfile& learning,
                                            const SliceAnalysis* slice)
{
    SuccessPrediction p;
    float score = 90.f;

    if (learning.total_prints > 0) {
        const float rate = float(learning.successful_prints) / float(learning.total_prints);
        score = 42.f + rate * 56.f;
    }

    const double overhang = std::max(model.overhang_face_ratio, model.overhang_ratio);
    const bool slice_overhang_signal = slice && slice->valid && slice->overhang_area_ratio > 0.10f;

    if (!slice_overhang_signal) {
        if (overhang > 0.35) {
            score -= 14.f;
            p.risk_factors.push_back("High overhang complexity (mesh)");
        } else if (overhang > 0.22) {
            score -= 7.f;
            p.risk_factors.push_back("Moderate overhang faces on mesh");
        }
    }

    if (model.tall_narrow) {
        score -= 9.f;
        p.risk_factors.push_back("Tall narrow geometry — stability risk");
    }

    if (model.thin_feature_risk) {
        score -= 6.f;
        p.risk_factors.push_back("Thin vertical features detected");
    }

    if (model.complexity_score >= 75) {
        score -= 5.f;
        p.risk_factors.push_back("High model complexity score");
    }

    if (model.first_layer_contact_ratio > 0.0 && model.first_layer_contact_ratio < 0.35) {
        score -= 11.f;
        p.risk_factors.push_back("Small first-layer contact footprint");
    }

    if (model.needs_brim && config.has("brim_type")) {
        if (const ConfigOption* opt = config.option("brim_type")) {
            if (opt->type() == coEnum && opt->is_scalar() && opt->getInt() == int(BrimType::btNoBrim)) {
                score -= 16.f;
                p.risk_factors.push_back("Brim recommended but not enabled");
            } else if (opt->getInt() != int(BrimType::btNoBrim)) {
                score += 4.f;
            }
        }
    }

    if (config.has("enable_support") && config_get_bool(config, "enable_support") && overhang > 0.12) {
        if (config.has("support_type")) {
            score += 3.f;
        }
    }

    if (config.has("raft_layers") && config_get_int(config, "raft_layers", 0) > 0) {
        score -= 5.f;
        p.risk_factors.push_back("Raft enabled — Verslicer recommends brim + tree supports instead");
    }

    if (slice && slice->valid) {
        if (slice->overhang_area_ratio > 0.12f) {
            score -= std::min(18.f, slice->overhang_area_ratio * 45.f);
            p.risk_factors.push_back("Unsupported area in slice");
        }
        if (slice->unsupported_islands_count >= 8) {
            score -= 6.f;
            p.risk_factors.push_back("Many unsupported islands in slice");
        }
        if (slice->bridge_length_max_mm > 18.f) {
            score -= 5.f;
            p.risk_factors.push_back("Very long bridge spans in slice");
        }
        for (const std::string& note : slice->risk_notes)
            p.risk_factors.push_back(note);
    }

    const int recent_failures = FailureDatabase::instance().count_failures_recent(printer_id);
    if (recent_failures >= 3) {
        score -= std::min(22.f, float(recent_failures) * 3.5f);
        p.risk_factors.push_back("Multiple recent failures on this printer");
    } else if (recent_failures == 2) {
        score -= 5.f;
        p.risk_factors.push_back("Two recent failures on this printer");
    }

    const auto recent_cats = FailureDatabase::instance().count_failures_recent_by_category(printer_id);
    for (const auto& kv : recent_cats) {
        if (kv.second < 2)
            continue;
        if (kv.first == "adhesion") {
            score -= std::min(12.f, float(kv.second) * 4.f);
            p.risk_factors.push_back("Recent adhesion failures on this printer");
        } else if (kv.first == "filament") {
            score -= std::min(10.f, float(kv.second) * 3.f);
            p.risk_factors.push_back("Recent filament issues on this printer");
        } else if (kv.first == "temperature") {
            score -= std::min(8.f, float(kv.second) * 2.5f);
            p.risk_factors.push_back("Recent temperature-related failures");
        }
    }

    if (learning.last_failure_ms > learning.last_success_ms && learning.last_failure_ms > 0) {
        score -= 6.f;
        p.risk_factors.push_back("Most recent job on this printer failed");
    }

    if (config.has("enable_support")) {
        if (const ConfigOptionBool* sup = config.option<ConfigOptionBool>("enable_support")) {
            if (!sup->value && overhang > 0.25) {
                score -= 12.f;
                p.risk_factors.push_back("Support disabled for challenging geometry");
            }
        }
    }

    if (model.height_mm > 180.0) {
        score -= 4.f;
        p.risk_factors.push_back("Very tall print — warping and adhesion matter");
    }

    p.success_rate = std::max(5.f, std::min(99.f, score));
    p.summary = "Estimated success rate: " + std::to_string(int(std::round(p.success_rate))) + "%";

    ReadinessReport readiness = PrintReadinessEngine::evaluate(
        model, config, learning, p, slice, 0);
    if (!readiness.headline.empty())
        p.summary += " · " + readiness.headline;

    if (p.risk_factors.empty())
        p.risk_factors.push_back("No major risk factors detected");

    annotate_prediction_confidence(p, learning, slice, model.height_mm > 0.0 || model.volume_mm3 > 0.0);

    return p;
}

} // namespace BambuSmartPrint
} // namespace Slic3r

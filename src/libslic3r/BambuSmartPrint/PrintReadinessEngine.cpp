#include "PrintReadinessEngine.hpp"
#include "MaterialAdvisor.hpp"

#include "libslic3r/PrintConfig.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {
namespace BambuSmartPrint {

ReadinessTier PrintReadinessEngine::tier_from_score(float score)
{
    if (score >= 85.f)
        return ReadinessTier::Excellent;
    if (score >= 70.f)
        return ReadinessTier::Good;
    if (score >= 50.f)
        return ReadinessTier::Fair;
    return ReadinessTier::Risky;
}

namespace {

static const char* tier_label(ReadinessTier t)
{
    switch (t) {
    case ReadinessTier::Excellent: return "Excellent";
    case ReadinessTier::Good:      return "Good";
    case ReadinessTier::Fair:      return "Fair";
    default:                       return "Needs attention";
    }
}

static void add_insight(ReadinessReport& r, const std::string& label, const std::string& detail,
                        RiskSeverity sev = RiskSeverity::Info)
{
    PrintInsight ins;
    ins.label    = label;
    ins.detail   = detail;
    ins.severity = sev;
    r.insights.push_back(std::move(ins));
}

} // namespace

ReadinessReport PrintReadinessEngine::evaluate(const ModelAnalysis& model,
                                               const DynamicPrintConfig& config,
                                               const PrinterLearningProfile& learning,
                                               const SuccessPrediction& prediction,
                                               const SliceAnalysis* slice,
                                               size_t pending_setting_changes)
{
    ReadinessReport r;
    r.success_rate = prediction.success_rate;
    r.score        = prediction.success_rate;

    if (pending_setting_changes > 0) {
        const float boost = std::min(12.f, float(pending_setting_changes) * 2.2f);
        r.score = std::min(99.f, r.score + boost);
        if (pending_setting_changes >= 4)
            add_insight(r, "Settings", "Several Smart Print adjustments are pending — review before printing",
                        RiskSeverity::Low);
    }

    if (slice && slice->valid && slice->overhang_area_ratio < 0.06f)
        r.score = std::min(99.f, r.score + 4.f);

    if (learning.total_prints >= 5) {
        const float hist = float(learning.successful_prints) / float(learning.total_prints);
        r.score = r.score * 0.75f + (40.f + hist * 55.f) * 0.25f;
    }

    r.score = std::max(5.f, std::min(99.f, r.score));
    r.tier  = PrintReadinessEngine::tier_from_score(r.score);
    // Short tier label only; UI layers add score to avoid duplicate "91%" and mojibake from UTF-8 punctuation.
    r.headline = tier_label(r.tier);

    const double overhang = std::max(model.overhang_face_ratio, model.overhang_ratio);

    add_insight(r, "Material", model.suggested_material + " recommended for this geometry");
    add_insight(r, "Size",
        "H " + std::to_string(int(std::round(model.height_mm))) + " mm, footprint "
        + std::to_string(int(std::round(model.max_xy_mm))) + " mm",
        RiskSeverity::Info);

    if (overhang > 0.2)
        add_insight(r, "Overhangs",
            std::to_string(int(std::round(overhang * 100.0))) + "% of mesh faces are steep",
            overhang > 0.35 ? RiskSeverity::High : RiskSeverity::Medium);

    if (model.first_layer_contact_ratio > 0.0 && model.first_layer_contact_ratio < 0.4)
        add_insight(r, "Bed contact",
            "Limited first-layer footprint — use outer brim and tree supports (no raft)",
            RiskSeverity::High);

    if (model.tall_narrow)
        add_insight(r, "Stability", "Tall and narrow — supports and slow first layers help", RiskSeverity::Medium);

    if (model.thin_feature_risk)
        add_insight(r, "Thin features", "Small XY extent with height — watch cooling and speed", RiskSeverity::Medium);

    if (model.complexity_score >= 70)
        add_insight(r, "Complexity", "High geometric complexity for automatic slicing", RiskSeverity::Medium);

    if (slice && slice->valid) {
        if (slice->overhang_area_ratio > 0.1f)
            add_insight(r, "Slice check",
                "Unsupported area in last slice — enable tree supports if not already",
                slice->overhang_area_ratio > 0.2f ? RiskSeverity::High : RiskSeverity::Medium);
        if (slice->bridge_length_max_mm > 12.f)
            add_insight(r, "Bridges",
                "Long bridge span (~" + std::to_string(int(slice->bridge_length_max_mm)) + " mm) detected",
                RiskSeverity::Medium);
    }

    if (learning.failed_prints > 0 && learning.total_prints > 0) {
        const float fail_rate = float(learning.failed_prints) / float(learning.total_prints);
        if (fail_rate > 0.25f)
            add_insight(r, "Printer history",
                std::to_string(learning.failed_prints) + " failures recorded on this printer",
                RiskSeverity::Medium);
    }

    for (const std::string& risk : prediction.risk_factors) {
        if (risk.find("No major") != std::string::npos)
            continue;
        r.action_items.push_back(risk);
    }

    if (pending_setting_changes > 0)
        r.action_items.insert(r.action_items.begin(),
            std::to_string(pending_setting_changes) + " setting adjustment(s) ready to apply");

    if (model.needs_brim && config.has("brim_type")) {
        if (const ConfigOption* opt = config.option("brim_type")) {
            if (opt->type() == coEnum && opt->is_scalar() && opt->getInt() == int(BrimType::btNoBrim))
                r.action_items.push_back("Enable brim for better bed adhesion");
        }
    }

    if (!model.fits_bed)
        r.action_items.insert(r.action_items.begin(), "Scale model to fit the build plate");
    else if (!model.suggested_orientation_hint.empty())
        r.action_items.push_back(model.suggested_orientation_hint);

    if (r.filament_mismatch && !model.suggested_material.empty())
        r.action_items.insert(r.action_items.begin(),
            "Switch filament to " + model.suggested_material + " or load it in AMS");

    if (r.action_items.empty())
        r.action_items.push_back("Geometry looks manageable — review speed and material before printing");

    MaterialAdvisor::annotate_filament_readiness(model, config, r);

    return r;
}

} // namespace BambuSmartPrint
} // namespace Slic3r

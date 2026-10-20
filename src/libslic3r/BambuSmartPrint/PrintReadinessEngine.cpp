#include "PrintReadinessEngine.hpp"
#include "MaterialAdvisor.hpp"
#include "BambuErrorCatalog.hpp"
#include "ConfigOptionRead.hpp"

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/I18N.hpp"

#include <boost/format.hpp>

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

static std::string tier_label(ReadinessTier t)
{
    switch (t) {
    case ReadinessTier::Excellent: return _u8L("Excellent");
    case ReadinessTier::Good:      return _u8L("Good");
    case ReadinessTier::Fair:      return _u8L("Fair");
    default:                       return _u8L("Needs attention");
    }
}

static std::string fmt_tr(const char* english, const std::string& arg)
{
    return (boost::format(_u8L(english)) % arg).str();
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

static float cap_score_for_overhang(float score, double overhang, bool support_enabled)
{
    if (overhang >= 0.35)
        return std::min(score, support_enabled ? 78.f : 64.f);
    if (overhang >= 0.25)
        return std::min(score, support_enabled ? 82.f : 72.f);
    if (overhang >= 0.20)
        return std::min(score, 84.f);
    return score;
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

    const double overhang = std::max(model.overhang_face_ratio, model.overhang_ratio);
    const bool support_enabled =
        config.has("enable_support") && config_get_bool(config, "enable_support");

    if (pending_setting_changes > 0 && overhang < 0.25) {
        const float boost = std::min(12.f, float(pending_setting_changes) * 2.2f);
        r.score = std::min(99.f, r.score + boost);
        if (pending_setting_changes >= 4)
            add_insight(r, _u8L("Settings"),
                        _u8L("Several Smart Print adjustments are pending — review before printing"),
                        RiskSeverity::Low);
    }

    if (slice && slice->valid && slice->overhang_area_ratio < 0.06f && overhang < 0.20)
        r.score = std::min(99.f, r.score + 4.f);

    if (learning.total_prints >= 5) {
        const float hist = float(learning.successful_prints) / float(learning.total_prints);
        r.score = r.score * 0.75f + (40.f + hist * 55.f) * 0.25f;
    }

    r.score = cap_score_for_overhang(r.score, overhang, support_enabled);
    r.score = std::max(5.f, std::min(99.f, r.score));
    r.tier  = PrintReadinessEngine::tier_from_score(r.score);
    r.headline = tier_label(r.tier);

    add_insight(r, _u8L("Material"), fmt_tr("%s recommended for this geometry", model.suggested_material));
    add_insight(r, _u8L("Size"),
        (boost::format(_u8L("H %1% mm, footprint %2% mm")) % int(std::round(model.height_mm))
            % int(std::round(model.max_xy_mm))).str(),
        RiskSeverity::Info);

    if (overhang > 0.2)
        add_insight(r, _u8L("Overhangs"),
            fmt_tr("%s%% of mesh faces are steep", std::to_string(int(std::round(overhang * 100.0)))),
            overhang > 0.35 ? RiskSeverity::High : RiskSeverity::Medium);

    if (model.first_layer_contact_ratio > 0.0 && model.first_layer_contact_ratio < 0.4)
        add_insight(r, _u8L("Bed contact"),
            _u8L("Limited first-layer footprint — use outer brim and tree supports (no raft)"),
            RiskSeverity::High);

    if (model.tall_narrow)
        add_insight(r, _u8L("Stability"),
            _u8L("Tall and narrow — supports and slow first layers help"),
            RiskSeverity::Medium);

    if (model.thin_feature_risk)
        add_insight(r, _u8L("Thin features"),
            _u8L("Small XY extent with height — watch cooling and speed"),
            RiskSeverity::Medium);

    if (model.complexity_score >= 70)
        add_insight(r, _u8L("Complexity"),
            _u8L("High geometric complexity for automatic slicing"),
            RiskSeverity::Medium);

    if (slice && slice->valid) {
        if (slice->overhang_area_ratio > 0.1f)
            add_insight(r, _u8L("Slice check"),
                _u8L("Unsupported area in last slice — enable tree supports if not already"),
                slice->overhang_area_ratio > 0.2f ? RiskSeverity::High : RiskSeverity::Medium);
        if (slice->bridge_length_max_mm > 12.f)
            add_insight(r, _u8L("Bridges"),
                fmt_tr("Long bridge span (~%s mm) detected", std::to_string(int(slice->bridge_length_max_mm))),
                RiskSeverity::Medium);
    }

    if (learning.failed_prints > 0 && learning.total_prints > 0) {
        const float fail_rate = float(learning.failed_prints) / float(learning.total_prints);
        if (fail_rate > 0.25f)
            add_insight(r, _u8L("Printer history"),
                fmt_tr("%s failures recorded on this printer", std::to_string(learning.failed_prints)),
                RiskSeverity::Medium);
    }

    for (const std::string& risk : prediction.risk_factors) {
        if (risk.find("No major") != std::string::npos)
            continue;
        r.action_items.push_back(risk);
    }

    if (pending_setting_changes > 0)
        r.action_items.insert(r.action_items.begin(),
            fmt_tr("%s setting adjustment(s) ready to apply", std::to_string(pending_setting_changes)));

    if (model.needs_brim && config.has("brim_type")) {
        if (const ConfigOption* opt = config.option("brim_type")) {
            if (opt->type() == coEnum && opt->is_scalar() && opt->getInt() == int(BrimType::btNoBrim))
                r.action_items.push_back(_u8L("Enable brim for better bed adhesion"));
        }
    }

    if (!model.fits_bed)
        r.action_items.insert(r.action_items.begin(), _u8L("Scale model to fit the build plate"));
    else if (!model.suggested_orientation_hint.empty())
        r.action_items.push_back(model.suggested_orientation_hint);

    if (r.filament_mismatch && !model.suggested_material.empty())
        r.action_items.insert(r.action_items.begin(),
            fmt_tr("Switch filament to %s or load it in AMS", model.suggested_material));

    if (r.action_items.empty())
        r.action_items.push_back(_u8L("Geometry looks manageable — review speed and material before printing"));

    MaterialAdvisor::annotate_filament_readiness(model, config, r);

    return r;
}

} // namespace BambuSmartPrint
} // namespace Slic3r

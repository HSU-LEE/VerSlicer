#include "PrintReadinessEngine.hpp"
#include "MaterialAdvisor.hpp"
#include "BambuErrorCatalog.hpp"
#include "ConfigOptionRead.hpp"

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

static const char* tier_label(ReadinessTier t, bool ko)
{
    switch (t) {
    case ReadinessTier::Excellent: return ko ? "우수" : "Excellent";
    case ReadinessTier::Good:      return ko ? "양호" : "Good";
    case ReadinessTier::Fair:      return ko ? "보통" : "Fair";
    default:                       return ko ? "주의 필요" : "Needs attention";
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

    const bool ko = BambuErrorCatalog::prefer_korean_ui();
    const double overhang = std::max(model.overhang_face_ratio, model.overhang_ratio);
    const bool support_enabled =
        config.has("enable_support") && config_get_bool(config, "enable_support");

    if (pending_setting_changes > 0 && overhang < 0.25) {
        const float boost = std::min(12.f, float(pending_setting_changes) * 2.2f);
        r.score = std::min(99.f, r.score + boost);
        if (pending_setting_changes >= 4)
            add_insight(r, ko ? "설정" : "Settings",
                        ko ? "적용 대기 중인 스마트 프린트 조정이 여러 건 있습니다 — 출력 전 검토하세요."
                           : "Several Smart Print adjustments are pending — review before printing",
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
    r.headline = tier_label(r.tier, ko);

    add_insight(r, ko ? "재료" : "Material",
                (ko ? "이 형상에는 " : "") + model.suggested_material
                    + (ko ? " 필라멘트를 권장합니다." : " recommended for this geometry"));
    add_insight(r, ko ? "크기" : "Size",
        (ko ? "높이 " : "H ") + std::to_string(int(std::round(model.height_mm)))
            + (ko ? " mm, 바닥면 " : " mm, footprint ")
            + std::to_string(int(std::round(model.max_xy_mm)))
            + (ko ? " mm" : " mm"),
        RiskSeverity::Info);

    if (overhang > 0.2)
        add_insight(r, ko ? "오버행" : "Overhangs",
            (ko ? "메시 면의 약 " : "")
                + std::to_string(int(std::round(overhang * 100.0)))
                + (ko ? "%가 가파른 오버행입니다." : "% of mesh faces are steep"),
            overhang > 0.35 ? RiskSeverity::High : RiskSeverity::Medium);

    if (model.first_layer_contact_ratio > 0.0 && model.first_layer_contact_ratio < 0.4)
        add_insight(r, ko ? "베드 접촉" : "Bed contact",
            ko ? "첫 층 접촉 면적이 작습니다 — 바깥 브림과 트리 서포트를 사용하세요(래프트 없음)."
               : "Limited first-layer footprint — use outer brim and tree supports (no raft)",
            RiskSeverity::High);

    if (model.tall_narrow)
        add_insight(r, ko ? "안정성" : "Stability",
            ko ? "키가 크고 좁습니다 — 서포트와 느린 첫 층이 도움이 됩니다."
               : "Tall and narrow — supports and slow first layers help",
            RiskSeverity::Medium);

    if (model.thin_feature_risk)
        add_insight(r, ko ? "얇은 형상" : "Thin features",
            ko ? "XY 폭이 작고 높이가 큽니다 — 냉각과 속도에 주의하세요."
               : "Small XY extent with height — watch cooling and speed",
            RiskSeverity::Medium);

    if (model.complexity_score >= 70)
        add_insight(r, ko ? "복잡도" : "Complexity",
            ko ? "자동 슬라이싱에 비해 형상이 복잡합니다." : "High geometric complexity for automatic slicing",
            RiskSeverity::Medium);

    if (slice && slice->valid) {
        if (slice->overhang_area_ratio > 0.1f)
            add_insight(r, ko ? "슬라이스 검사" : "Slice check",
                ko ? "마지막 슬라이스에 지지되지 않은 영역이 있습니다 — 트리 서포트를 켜세요."
                   : "Unsupported area in last slice — enable tree supports if not already",
                slice->overhang_area_ratio > 0.2f ? RiskSeverity::High : RiskSeverity::Medium);
        if (slice->bridge_length_max_mm > 12.f)
            add_insight(r, ko ? "브릿지" : "Bridges",
                (ko ? "긴 브릿지(약 " : "Long bridge span (~")
                    + std::to_string(int(slice->bridge_length_max_mm))
                    + (ko ? " mm)가 감지되었습니다." : " mm) detected"),
                RiskSeverity::Medium);
    }

    if (learning.failed_prints > 0 && learning.total_prints > 0) {
        const float fail_rate = float(learning.failed_prints) / float(learning.total_prints);
        if (fail_rate > 0.25f)
            add_insight(r, ko ? "프린터 이력" : "Printer history",
                (ko ? "이 프린터에 실패 기록이 " : "")
                    + std::to_string(learning.failed_prints)
                    + (ko ? "건 있습니다." : " failures recorded on this printer"),
                RiskSeverity::Medium);
    }

    for (const std::string& risk : prediction.risk_factors) {
        if (risk.find("No major") != std::string::npos)
            continue;
        r.action_items.push_back(risk);
    }

    if (pending_setting_changes > 0)
        r.action_items.insert(r.action_items.begin(),
            std::to_string(pending_setting_changes)
                + (ko ? "건의 설정 조정을 적용할 수 있습니다." : " setting adjustment(s) ready to apply"));

    if (model.needs_brim && config.has("brim_type")) {
        if (const ConfigOption* opt = config.option("brim_type")) {
            if (opt->type() == coEnum && opt->is_scalar() && opt->getInt() == int(BrimType::btNoBrim))
                r.action_items.push_back(ko ? "베드 접착을 위해 브림을 켜세요."
                                          : "Enable brim for better bed adhesion");
        }
    }

    if (!model.fits_bed)
        r.action_items.insert(r.action_items.begin(),
            ko ? "빌드 플레이트에 맞게 모델 크기를 조정하세요." : "Scale model to fit the build plate");
    else if (!model.suggested_orientation_hint.empty())
        r.action_items.push_back(model.suggested_orientation_hint);

    if (r.filament_mismatch && !model.suggested_material.empty())
        r.action_items.insert(r.action_items.begin(),
            ko ? "필라멘트를 " + model.suggested_material + "(으)로 바꾸거나 AMS에 로드하세요."
               : "Switch filament to " + model.suggested_material + " or load it in AMS");

    if (r.action_items.empty())
        r.action_items.push_back(ko ? "형상은 무난해 보입니다 — 출력 전 속도와 재료를 확인하세요."
                                    : "Geometry looks manageable — review speed and material before printing");

    MaterialAdvisor::annotate_filament_readiness(model, config, r);

    return r;
}

} // namespace BambuSmartPrint
} // namespace Slic3r

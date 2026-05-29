#include "AutoSettingsEngine.hpp"
#include "AutoSettingsRules.hpp"
#include "ConfigOptionRead.hpp"
#include "ConfigSnapshot.hpp"
#include "MaterialAdvisor.hpp"
#include "MeshGeometryAnalyzer.hpp"
#include "MeshAnalysisCache.hpp"
#include "PrinterLearningStore.hpp"
#include "SafeModeGuard.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {
namespace BambuSmartPrint {

namespace {

static void apply_change(AutoSettingsResult& result, const DynamicPrintConfig& base,
                         DynamicPrintConfig& delta, const std::string& key,
                         const std::string& value, const std::string& reason)
{
    if (key.empty() || !delta.has(key))
        return;
    SettingChange ch;
    ch.key    = key;
    ch.reason = reason;
    if (const ConfigOption* opt = base.option(key))
        ch.old_value = opt->serialize();
    ch.new_value = value;
    delta.set_deserialize_strict(key, value);
    result.changes.push_back(std::move(ch));
}

static void strip_raft(DynamicPrintConfig& delta, AutoSettingsResult& result,
                       const DynamicPrintConfig& base)
{
    if (!delta.has("raft_layers"))
        return;
    if (config_get_int(delta, "raft_layers", 0) > 0)
        apply_change(result, base, delta, "raft_layers", "0",
                     "SlicePilot: brim and tree supports — no raft (Bambu Studio style)");
}

static void enable_tree_support(AutoSettingsResult& result, const DynamicPrintConfig& base,
                                DynamicPrintConfig& delta, const char* reason)
{
    apply_change(result, base, delta, "enable_support", "1", reason);
    if (delta.has("support_type"))
        apply_change(result, base, delta, "support_type", "tree(auto)",
                     "Bambu-style tree supports for overhangs");
    if (delta.has("support_on_build_plate_only"))
        apply_change(result, base, delta, "support_on_build_plate_only", "1",
                     "Tree supports anchored to build plate");
}

} // namespace

ModelAnalysis AutoSettingsEngine::analyze_model(const Model& model)
{
    return analyze_model(model, DynamicPrintConfig());
}

ModelAnalysis AutoSettingsEngine::analyze_model(const Model& model, const DynamicPrintConfig& config)
{
    const std::string key = MeshAnalysisCache::instance().cache_key_for_objects(model.objects, config);
    ModelAnalysis cached;
    if (MeshAnalysisCache::instance().lookup(key, &cached))
        return cached;
    ModelAnalysis a = MeshGeometryAnalyzer::analyze(model, config);
    MeshAnalysisCache::instance().store(key, a);
    return a;
}

AutoSettingsResult AutoSettingsEngine::suggest_settings(const Model& model, const DynamicPrintConfig& base_config,
                                                      const PrinterLearningProfile* learning,
                                                      const SliceAnalysis* slice)
{
    return suggest_settings_for_objects(model.objects, base_config, learning, slice);
}

AutoSettingsResult AutoSettingsEngine::suggest_settings_for_objects(const std::vector<ModelObject*>& objects,
                                                                    const DynamicPrintConfig& base_config,
                                                                    const PrinterLearningProfile* learning,
                                                                    const SliceAnalysis* slice,
                                                                    int plate_index)
{
    AutoSettingsResult result;
    const AutoSettingsThresholds& rules = auto_settings_thresholds();
    const std::string cache_key = MeshAnalysisCache::instance().cache_key_for_objects(
        objects, base_config, plate_index);
    ModelAnalysis analysis;
    if (!MeshAnalysisCache::instance().lookup(cache_key, &analysis)) {
        analysis = MeshGeometryAnalyzer::analyze_objects(objects, base_config);
        MeshAnalysisCache::instance().store(cache_key, analysis);
    }
    DynamicPrintConfig delta = base_config;

    strip_raft(delta, result, base_config);

    const double overhang = std::max(analysis.overhang_face_ratio, analysis.overhang_ratio);
    const std::string active_filament = MaterialAdvisor::detect_filament_family(base_config);

    if (overhang > rules.overhang_moderate && delta.has("support_threshold_angle")) {
        const int cur = config_get_int(delta, "support_threshold_angle", 50);
        if (cur > 52)
            apply_change(result, base_config, delta, "support_threshold_angle", "50",
                         "Bambu-aligned overhang detection threshold");
    }

    if (active_filament == "TPU" || analysis.suggested_material == "TPU") {
        if (delta.has("outer_wall_speed")) {
            float speed = config_get_float(base_config, "outer_wall_speed");
            if (speed > 40.f)
                apply_change(result, base_config, delta, "outer_wall_speed", "35",
                             "Flexible filament — slow outer walls");
        }
        if (delta.has("initial_layer_speed")) {
            float speed = config_get_float(base_config, "initial_layer_speed");
            if (speed > 30.f)
                apply_change(result, base_config, delta, "initial_layer_speed", "25",
                             "TPU — slower first layer");
        }
    }

    if (active_filament == "ASA" || analysis.suggested_material == "ABS") {
        if (delta.has("bed_temperature")) {
            float bed = config_get_float(base_config, "bed_temperature");
            if (bed < 95.f && analysis.height_mm > 80.0)
                apply_change(result, base_config, delta, "bed_temperature", "100",
                             "ABS/ASA — higher bed for tall prints");
        }
    }

    if (analysis.needs_brim) {
        apply_change(result, base_config, delta, "brim_type", "outer_only", "Model geometry benefits from brim");
        if (!delta.has("brim_width") || config_get_float(delta, "brim_width") < 3.0f)
            apply_change(result, base_config, delta, "brim_width", "5", "Wider brim for adhesion");
    }

    if (analysis.tall_narrow)
        enable_tree_support(result, base_config, delta, "Tall narrow model — stability supports");
    else if (overhang > rules.overhang_steep)
        enable_tree_support(result, base_config, delta, "Steep overhang faces — tree supports");
    else if (overhang > 0.10 && analysis.first_layer_contact_ratio < 0.35)
        enable_tree_support(result, base_config, delta, "Low bed contact with overhangs");

    if (slice && slice->valid) {
        if (slice->overhang_area_ratio > rules.slice_overhang_support
            && delta.has("enable_support") && !config_get_bool(delta, "enable_support"))
            enable_tree_support(result, base_config, delta, "Slice shows unsupported regions");

        if (slice->unsupported_islands_count >= rules.slice_islands_moderate && delta.has("support_type"))
            apply_change(result, base_config, delta, "support_type", "tree(auto)",
                         "Slice islands — tree support");

        if (slice->overhang_area_ratio > rules.slice_overhang_steep && delta.has("support_threshold_angle")) {
            const int cur = config_get_int(delta, "support_threshold_angle", 50);
            if (cur > 48)
                apply_change(result, base_config, delta, "support_threshold_angle", "45",
                             "Steep unsupported slice area — lower support angle");
        }

        if (slice->bridge_length_max_mm > rules.slice_bridge_slow_mm && delta.has("outer_wall_speed")) {
            float speed = config_get_float(base_config, "outer_wall_speed");
            const float cut = slice->bridge_length_max_mm > rules.slice_bridge_high_mm ? 15.f : 10.f;
            speed = std::max(20.f, speed - cut);
            apply_change(result, base_config, delta, "outer_wall_speed", std::to_string(int(speed)),
                         "Long bridges in slice — slower outer walls");
        }

        if (slice->overhang_area_ratio > rules.slice_overhang_moderate && delta.has("fan_max_speed")) {
            float fan = config_get_float(base_config, "fan_max_speed");
            fan = std::min(100.f, fan + 10.f);
            apply_change(result, base_config, delta, "fan_max_speed", std::to_string(int(fan)),
                         "Slice overhangs — increase cooling");
        }
    }

    if (learning && learning->failures_by_category.count("adhesion")
        && learning->failures_by_category.at("adhesion") >= 2
        && analysis.first_layer_contact_ratio < rules.first_layer_brim + 0.05) {
        apply_change(result, base_config, delta, "brim_type", "outer_only",
                     "Learning: adhesion history — prefer brim");
    }

    if (analysis.height_mm > rules.tall_height_mm) {
        apply_change(result, base_config, delta, "slow_down_layers", "4", "Slow down on first layers for tall prints");
    }

    if (analysis.first_layer_contact_ratio > 0.0
        && analysis.first_layer_contact_ratio < rules.first_layer_contact_low
        && delta.has("elefant_foot_compensation")) {
        apply_change(result, base_config, delta, "elefant_foot_compensation", "0.15",
                     "Low bed contact — compensate elephant foot");
    }

    if (analysis.needs_brim && analysis.first_layer_contact_ratio < rules.first_layer_brim) {
        apply_change(result, base_config, delta, "brim_type", "outer_only",
                     "Low bed contact — outer brim (no raft)");
        if (!delta.has("brim_width") || config_get_float(delta, "brim_width") < 5.0f)
            apply_change(result, base_config, delta, "brim_width", "8",
                         "Wider brim for first-layer adhesion");
    }

    if (overhang > rules.overhang_fan && delta.has("fan_max_speed")) {
        float fan = config_get_float(base_config, "fan_max_speed");
        fan = std::min(100.f, fan + 15.f);
        apply_change(result, base_config, delta, "fan_max_speed", std::to_string(int(fan)),
                     "Steep overhangs — increase part cooling");
    }

    if (overhang > rules.overhang_high && delta.has("outer_wall_speed")) {
        float speed = config_get_float(base_config, "outer_wall_speed");
        speed = std::max(25.f, speed - 15.f);
        apply_change(result, base_config, delta, "outer_wall_speed", std::to_string(int(speed)),
                     "High overhang load — slower outer walls");
    }

    if (analysis.is_small_part && delta.has("initial_layer_height")) {
        float lh = config_get_float(base_config, "initial_layer_height");
        if (lh > 0.22f)
            apply_change(result, base_config, delta, "initial_layer_height", "0.2",
                         "Small part — finer first layer for detail");
    }

    if (analysis.complexity_score >= rules.complexity_thin_wall && delta.has("detect_thin_wall")) {
        apply_change(result, base_config, delta, "detect_thin_wall", "1",
                     "Complex geometry — detect thin walls");
    }

    if (analysis.max_xy_mm > rules.large_flat_xy_mm && analysis.height_mm < 25.0 && delta.has("ironing_type")) {
        apply_change(result, base_config, delta, "ironing_type", "top",
                     "Large flat top surface — enable top ironing");
    }

    if (analysis.suggested_material == "ABS" && delta.has("bed_temperature")) {
        float bed = config_get_float(base_config, "bed_temperature");
        if (bed < 90.f)
            apply_change(result, base_config, delta, "bed_temperature", "100",
                         "Large / tall ABS-friendly geometry — higher bed temp");
    } else if (analysis.suggested_material == "PETG" && delta.has("outer_wall_speed")) {
        float speed = config_get_float(base_config, "outer_wall_speed");
        if (speed > 120.f)
            apply_change(result, base_config, delta, "outer_wall_speed", "100",
                         "PETG recommended — moderate outer wall speed");
    }

    if (analysis.height_mm > rules.tall_height_mm * 0.9 && delta.has("slow_down_layers")) {
        const int layers = config_get_int(delta, "slow_down_layers", 0);
        if (layers < 4)
            apply_change(result, base_config, delta, "slow_down_layers", "4",
                         "Tall print — slow first layers (Bambu adhesion profile)");
    }

    if ((active_filament == "ABS" || active_filament == "ASA" || analysis.suggested_material == "ABS")
        && analysis.height_mm > 100.0 && delta.has("chamber_temperature")) {
        const float chamber = config_get_float(delta, "chamber_temperature", 0.f);
        if (chamber < 45.f)
            apply_change(result, base_config, delta, "chamber_temperature", "55",
                         "Tall ABS/ASA — heated chamber reduces warping");
    }

    if (analysis.volume_mm3 > rules.large_volume_mm3 && delta.has("sparse_infill_density")) {
        float infill = config_get_float(base_config, "sparse_infill_density");
        if (infill > 25.f)
            apply_change(result, base_config, delta, "sparse_infill_density", "15",
                         "Large solid model — reduce infill to save time");
    } else if (analysis.is_small_part && delta.has("sparse_infill_density")) {
        float infill = config_get_float(base_config, "sparse_infill_density");
        if (infill < 18.f)
            apply_change(result, base_config, delta, "sparse_infill_density", "20",
                         "Small part — slightly higher infill for strength");
    }

    if (learning) {
        auto apply_learned_float = [&](const char* key, const char* reason, float lo, float hi) {
            auto it = learning->setting_adjustments.find(key);
            if (it == learning->setting_adjustments.end() || !delta.has(key))
                return;
            float v = config_get_float(base_config, key) + it->second;
            v = std::max(lo, std::min(hi, v));
            apply_change(result, base_config, delta, key, std::to_string(int(v)), reason);
        };
        apply_learned_float("initial_layer_speed", "Learned: slower first layer after adhesion issues", 15.f, 200.f);
        apply_learned_float("bed_temperature", "Learned: bed temperature bias from history", 0.f, 120.f);
        apply_learned_float("outer_wall_speed", "Learned: outer wall speed bias", 20.f, 300.f);
        apply_learned_float("retraction_length", "Learned: retraction bias after filament issues", 0.f, 10.f);
        apply_learned_float("fan_min_speed", "Learned: minimum fan bias", 0.f, 100.f);
        if (learning->setting_adjustments.count("brim_width") && delta.has("brim_width")) {
            float w = std::max(config_get_float(base_config, "brim_width"),
                               learning->setting_adjustments.at("brim_width"));
            apply_change(result, base_config, delta, "brim_width", std::to_string(int(w)),
                         "Learned: wider brim from adhesion history");
        }
    }

    strip_raft(delta, result, base_config);
    result.config_delta = std::move(delta);
    result.summary = "SlicePilot tuned for Bambu Lab (" + analysis.suggested_material + "): "
                     + std::to_string(result.changes.size()) + " adjustments — tree supports, no raft.";
    return apply_safe_mode_to_suggestions(base_config, std::move(result));
}

std::string AutoSettingsEngine::suggested_orientation_hint(const ModelAnalysis& analysis)
{
    if (!analysis.suggested_orientation_hint.empty())
        return analysis.suggested_orientation_hint;
    if (analysis.tall_narrow)
        return "lay_flat_for_stability";
    if (analysis.overhang_face_ratio > 0.25)
        return "rotate_to_minimize_overhang";
    return {};
}

AutoSettingsResult AutoSettingsEngine::apply_safe_mode_to_suggestions(const DynamicPrintConfig& base_config,
                                                                      AutoSettingsResult result)
{
    if (!SafeModeGuard::is_enabled() || result.changes.empty())
        return result;

    const SafeModeResult safe = SafeModeGuard::apply(base_config, result.config_delta);
    result.config_delta   = safe.config;
    result.blocked_changes = safe.blocked_changes;

    std::vector<SettingChange> kept;
    const std::vector<SettingChange> diff = ConfigSnapshot::diff(base_config, result.config_delta);
    for (const SettingChange& ch : result.changes) {
        bool still_present = false;
        for (const SettingChange& d : diff) {
            if (d.key == ch.key) {
                still_present = true;
                break;
            }
        }
        if (still_present)
            kept.push_back(ch);
    }
    result.changes = std::move(kept);
    if (safe.had_blocks && !result.blocked_changes.empty())
        result.summary += " (" + std::to_string(result.blocked_changes.size()) + " blocked by safe mode)";
    return result;
}

std::string AutoSettingsEngine::suggest_filament_preset_name(const PresetBundle& bundle, const ModelAnalysis& analysis)
{
    const std::string needle = analysis.suggested_material;
    std::string best;
    for (const Preset& p : bundle.filaments) {
        if (!p.is_visible) continue;
        const std::string& n = p.name;
        if (n.find(needle) != std::string::npos) {
            best = p.name;
            break;
        }
    }
    if (best.empty() && bundle.filaments.get_selected_idx() < bundle.filaments.size())
        best = bundle.filaments.get_selected_preset().name;
    return best;
}

} // namespace BambuSmartPrint
} // namespace Slic3r

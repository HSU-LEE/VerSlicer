#include "SettingsOptimizer.hpp"
#include "AutoSettingsEngine.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <cstdlib>
#include <unordered_set>

namespace Slic3r {
namespace BambuSmartPrint {

static bool apply_setting_change(DynamicPrintConfig& cfg, const SettingChange& fix, SettingChange& applied_out)
{
    if (fix.key.empty() || !cfg.has(fix.key))
        return false;

    std::string new_val = fix.new_value;
    if (new_val.empty())
        return false;

    applied_out.key       = fix.key;
    applied_out.old_value = cfg.option(fix.key)->serialize();
    applied_out.reason    = fix.reason;

    if (!new_val.empty() && new_val[0] == '+') {
        const float delta = float(std::atof(new_val.c_str() + 1));
        if (cfg.option(fix.key)->is_scalar()) {
            switch (cfg.option(fix.key)->type()) {
            case coFloat:
            case coPercent: {
                float v = float(cfg.opt_float(fix.key)) + delta;
                cfg.set_key_value(fix.key, new ConfigOptionFloat(v));
                applied_out.new_value = std::to_string(v);
                return true;
            }
            case coInt: {
                int v = cfg.opt_int(fix.key) + int(delta);
                cfg.set_key_value(fix.key, new ConfigOptionInt(v));
                applied_out.new_value = std::to_string(v);
                return true;
            }
            default:
                break;
            }
        }
    }

    applied_out.new_value = new_val;
    try {
        cfg.set_deserialize_strict(fix.key, new_val);
        return true;
    } catch (...) {
        return false;
    }
}

namespace {

static bool is_conservative_allowed_key(const std::string& key)
{
    static const std::unordered_set<std::string> kAllowed = {
        "brim_type", "brim_width", "enable_support", "support_type", "support_on_build_plate_only",
        "support_threshold_angle", "elephant_foot_compensation", "initial_layer_height",
        "sparse_infill_density", "detect_thin_wall",
    };
    return kAllowed.count(key) > 0;
}

} // namespace

AutoSettingsResult SettingsOptimizer::optimize_from_diagnosis(const DynamicPrintConfig& current,
                                                              const FailureDiagnosis& diagnosis,
                                                              const PrinterLearningProfile* learning,
                                                              bool conservative)
{
    AutoSettingsResult result;
    DynamicPrintConfig cfg = current;

    for (const SettingChange& fix : diagnosis.recommended_fixes) {
        if (conservative && !is_conservative_allowed_key(fix.key))
            continue;
        if (SettingChange applied; apply_setting_change(cfg, fix, applied))
            result.changes.push_back(applied);
    }

    if (learning) {
        const bool adhesion_paused = learning->category_bias_paused.count("adhesion")
            && learning->category_bias_paused.at("adhesion");
        if (!adhesion_paused && diagnosis.category == FailureCategory::Adhesion && cfg.has("initial_layer_speed")) {
            float v = std::max(15.f, float(cfg.opt_float("initial_layer_speed")) - 10.f);
            SettingChange applied;
            applied.key       = "initial_layer_speed";
            const ConfigOption* old_opt = current.option("initial_layer_speed");
            applied.old_value = old_opt ? old_opt->serialize() : "";
            applied.new_value = std::to_string(v);
            applied.reason    = "Learning: slower first layer after adhesion failures";
            cfg.set_key_value("initial_layer_speed", new ConfigOptionFloat(v));
            result.changes.push_back(applied);
        }
    }

    result.config_delta = std::move(cfg);
    result.summary = "Suggested " + std::to_string(result.changes.size()) + " fixes based on: " + diagnosis.title;
    if (conservative && !result.changes.empty())
        result.summary += " (conservative — settings snapshot unverified)";
    return AutoSettingsEngine::apply_safe_mode_to_suggestions(current, std::move(result));
}

} // namespace BambuSmartPrint
} // namespace Slic3r

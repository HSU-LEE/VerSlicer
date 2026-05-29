#include "SafeModeGuard.hpp"
#include "ConfigSnapshot.hpp"
#include "ConfigOptionRead.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace Slic3r {
namespace BambuSmartPrint {

namespace {

static bool s_safe_mode_enabled = true;

static bool is_temp_key(const std::string& key)
{
    return key.find("temperature") != std::string::npos
        || key == "bed_temperature" || key == "nozzle_temperature"
        || key == "chamber_temperature";
}

static bool is_speed_key(const std::string& key)
{
    return key.find("speed") != std::string::npos
        || key == "outer_wall_speed" || key == "inner_wall_speed"
        || key == "initial_layer_speed" || key == "max_volumetric_speed";
}

static float parse_float(const std::string& s, float fallback)
{
    if (s.empty()) return fallback;
    try { return float(std::stof(s)); } catch (...) { return fallback; }
}

static bool within_percent_delta(float old_v, float new_v, float max_pct)
{
    if (old_v <= 0.f) return true;
    const float pct = std::abs(new_v - old_v) / old_v * 100.f;
    return pct <= max_pct;
}

static bool option_is_numeric_scalar(const DynamicPrintConfig& cfg, const std::string& key)
{
    if (!cfg.has(key))
        return false;
    const ConfigOption* opt = cfg.option(key);
    if (!opt)
        return false;
    return dynamic_cast<const ConfigOptionFloat*>(opt) != nullptr
        || dynamic_cast<const ConfigOptionFloats*>(opt) != nullptr
        || dynamic_cast<const ConfigOptionInt*>(opt) != nullptr
        || dynamic_cast<const ConfigOptionInts*>(opt) != nullptr
        || dynamic_cast<const ConfigOptionPercent*>(opt) != nullptr
        || dynamic_cast<const ConfigOptionPercents*>(opt) != nullptr;
}

} // namespace

bool SafeModeGuard::is_enabled()
{
    return s_safe_mode_enabled;
}

void SafeModeGuard::set_enabled(bool on)
{
    s_safe_mode_enabled = on;
}

bool SafeModeGuard::requires_user_approval(const SettingChange& change, const DynamicPrintConfig& before)
{
    if (!is_enabled()) return false;
    const bool temp  = is_temp_key(change.key);
    const bool speed = is_speed_key(change.key);
    if (!temp && !speed)
        return false;

    if (!before.has(change.key))
        return true;

    // Temperature/speed options are often coInts/coFloats — opt_float() would AV on wrong type.
    if (!option_is_numeric_scalar(before, change.key))
        return false;

    const float old_v = config_get_float(before, change.key, 0.f);
    const float new_v = parse_float(change.new_value, old_v);

    if (temp)
        return !within_percent_delta(old_v, new_v, 15.f);
    if (speed) {
        if (new_v > old_v * 1.35f) return true;
        return !within_percent_delta(old_v, new_v, 40.f);
    }
    return false;
}

SafeModeResult SafeModeGuard::apply(const DynamicPrintConfig& before, const DynamicPrintConfig& proposed)
{
    SafeModeResult out;
    out.config = proposed;
    if (!is_enabled())
        return out;

    try {
        const std::vector<SettingChange> changes = ConfigSnapshot::diff(before, proposed);
        for (const SettingChange& ch : changes) {
            if (ch.key.empty())
                continue;
            if (!requires_user_approval(ch, before))
                continue;

            SettingChange blocked = ch;
            blocked.reason = "Safe mode: change exceeds allowed limits";
            out.blocked_changes.push_back(blocked);
            out.had_blocks = true;

            if (before.has(ch.key) && !ch.old_value.empty()) {
                try {
                    out.config.set_deserialize_strict(ch.key, ch.old_value);
                } catch (...) {
                    out.warnings.push_back("Could not restore " + ch.key);
                }
            }
        }
    } catch (const std::exception& ex) {
        out.warnings.push_back(std::string("Safe mode skipped: ") + ex.what());
        out.config = proposed;
        out.blocked_changes.clear();
        out.had_blocks = false;
    } catch (...) {
        out.warnings.push_back("Safe mode skipped due to an internal error");
        out.config = proposed;
        out.blocked_changes.clear();
        out.had_blocks = false;
    }

    if (out.had_blocks)
        out.warnings.push_back("Some aggressive temperature/speed changes were blocked. Disable Safe mode in Smart Print to allow them.");
    return out;
}

} // namespace BambuSmartPrint
} // namespace Slic3r

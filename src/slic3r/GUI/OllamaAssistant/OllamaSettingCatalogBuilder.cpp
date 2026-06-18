#include "OllamaSettingCatalogBuilder.hpp"

#include "libslic3r/Config.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>

namespace Slic3r { namespace GUI {

namespace {

std::string lower_ascii(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool key_has_any_substr(const std::string& key, const std::vector<const char*>& needles)
{
    const std::string lower = lower_ascii(key);
    for (const char* n : needles) {
        if (lower.find(n) != std::string::npos)
            return true;
    }
    return false;
}

OllamaSettingTier classify_tier(const std::string& key, ConfigOptionMode mode)
{
    static const std::vector<const char*> kRestricted = {
        "gcode", "machine_start", "machine_end", "before_layer_change", "layer_change",
        "time_lapse", "change_filament", "print_host", "printhost", "inherits",
        "printable_area", "printable_height", "bed_exclude", "printer_notes",
        "thumbnails", "host_type", "file_start", "wrapping_detection",
    };
    if (key_has_any_substr(key, kRestricted))
        return OllamaSettingTier::Restricted;
    if (mode == comAdvanced)
        return OllamaSettingTier::Advanced;
    return OllamaSettingTier::Simple;
}

std::string value_type_for_def(const ConfigOptionDef& def)
{
    switch (def.type) {
    case coBool:
    case coBools:
        return "bool";
    case coPercent:
    case coPercents:
        return "percent";
    case coEnum:
    case coEnums:
        return "enum";
    case coInt:
    case coInts:
        if (def.sidetext.find('C') != std::string::npos || key_has_any_substr(def.opt_key, {"temp", "temperature"}))
            return "temp";
        return "count";
    case coFloat:
    case coFloats:
    case coFloatOrPercent:
    case coFloatsOrPercents:
        if (!def.sidetext.empty()) {
            if (def.sidetext.find("mm/s") != std::string::npos || def.sidetext.find("mm/s") != std::string::npos)
                return "speed";
            if (def.sidetext.find("mm") != std::string::npos)
                return "mm";
            if (def.sidetext.find('%') != std::string::npos)
                return "percent";
        }
        if (key_has_any_substr(def.opt_key, {"speed", "velocity"}))
            return "speed";
        if (key_has_any_substr(def.opt_key, {"temp", "temperature"}))
            return "temp";
        return "mm";
    default:
        return "string";
    }
}

std::string format_hint_for(const ConfigOptionDef& def, const std::string& value_type)
{
    if (value_type == "bool")
        return "true/false";
    if (value_type == "percent")
        return "string with % suffix, e.g. \"20%\"";
    if (value_type == "enum" && !def.enum_values.empty())
        return "one of: " + def.enum_values.front() + (def.enum_values.size() > 1 ? ", ..." : "");
    if (value_type == "count" || value_type == "temp")
        return "integer number";
    if (value_type == "speed" || value_type == "mm")
        return "number without unit suffix";
    return "match current serialized format";
}

int priority_for(const std::string& key, OllamaSettingTier tier)
{
    static const std::unordered_set<std::string> high = {
        "layer_height", "enable_support", "brim_width", "brim_type", "sparse_infill_density",
        "sparse_infill_pattern", "wall_loops", "initial_layer_print_height", "raft_layers",
        "support_type", "retraction_length", "nozzle_temperature",
    };
    if (high.count(key))
        return 100;
    if (tier == OllamaSettingTier::Simple)
        return 70;
    if (tier == OllamaSettingTier::Advanced)
        return 40;
    return 10;
}

const DynamicPrintConfig* cfg_for_scope(const std::string& scope, const DynamicPrintConfig* print_cfg,
                                        const DynamicPrintConfig* filament_cfg,
                                        const DynamicPrintConfig* printer_cfg)
{
    if (scope == "filament")
        return filament_cfg;
    if (scope == "printer")
        return printer_cfg;
    return print_cfg;
}

std::vector<OllamaAutoSettingSpec> build_auto_specs()
{
    std::unordered_set<std::string> print_keys(Preset::print_options().begin(), Preset::print_options().end());
    std::unordered_set<std::string> filament_keys(Preset::filament_options().begin(), Preset::filament_options().end());
    std::unordered_set<std::string> printer_keys(Preset::printer_options().begin(), Preset::printer_options().end());

    std::vector<OllamaAutoSettingSpec> specs;
    specs.reserve(print_keys.size() + filament_keys.size());

    const auto ingest = [&](const std::string& key, const std::string& scope) {
        if (key.empty())
            return;
        const ConfigOptionDef* def = print_config_def.get(key);
        if (!def)
            return;
        if (def->readonly)
            return;
        switch (def->type) {
        case coPoints:
        case coPoint:
        case coPoint3:
        case coPointsGroups:
        case coIntsGroups:
            return;
        default:
            break;
        }

        OllamaAutoSettingSpec sp;
        sp.key          = key;
        sp.preset_scope = scope;
        sp.value_type   = value_type_for_def(*def);
        sp.unit         = def->sidetext;
        sp.min_v        = def->min > -1e30f ? def->min : 0.0;
        sp.max_v        = def->max < 1e30f ? def->max : 0.0;
        sp.label        = def->label.empty() ? key : def->label;
        sp.category     = def->category;
        sp.tooltip      = def->tooltip;
        sp.format       = format_hint_for(*def, sp.value_type);
        sp.tier         = classify_tier(key, def->mode);
        sp.context_priority = priority_for(key, sp.tier);
        specs.push_back(std::move(sp));
    };

    for (const std::string& key : print_keys)
        ingest(key, "print");
    for (const std::string& key : filament_keys)
        ingest(key, "filament");
    for (const std::string& key : printer_keys)
        ingest(key, "printer");

    std::sort(specs.begin(), specs.end(),
              [](const OllamaAutoSettingSpec& a, const OllamaAutoSettingSpec& b) { return a.key < b.key; });
    specs.erase(std::unique(specs.begin(), specs.end(),
                            [](const OllamaAutoSettingSpec& a, const OllamaAutoSettingSpec& b) {
                                return a.key == b.key;
                            }),
                specs.end());
    return specs;
}

const std::vector<OllamaAutoSettingSpec>& auto_specs()
{
    static const std::vector<OllamaAutoSettingSpec> specs = build_auto_specs();
    return specs;
}

const std::unordered_map<std::string, const OllamaAutoSettingSpec*>& auto_index()
{
    static const std::unordered_map<std::string, const OllamaAutoSettingSpec*> index = [] {
        std::unordered_map<std::string, const OllamaAutoSettingSpec*> map;
        map.reserve(auto_specs().size());
        for (const OllamaAutoSettingSpec& sp : auto_specs())
            map.emplace(sp.key, &sp);
        return map;
    }();
    return index;
}

nlohmann::json spec_to_json(const OllamaAutoSettingSpec& sp, const DynamicPrintConfig* cfg, bool /*ko_ui*/)
{
    nlohmann::json e;
    e["key"]          = sp.key;
    e["value_type"]   = sp.value_type;
    e["unit"]         = sp.unit;
    e["min"]          = sp.min_v;
    e["max"]          = sp.max_v;
    e["label"]        = sp.label;
    e["category"]     = sp.category;
    e["format"]       = sp.format;
    e["preset_scope"] = sp.preset_scope;
    e["ai_tier"]      = static_cast<int>(sp.tier);
    if (!sp.tooltip.empty())
        e["description"] = sp.tooltip;
    if (cfg && cfg->has(sp.key))
        e["current"] = cfg->opt_serialize(sp.key);
    else
        e["current"] = nullptr;
    return e;
}

} // namespace

const std::vector<OllamaAutoSettingSpec>& OllamaSettingCatalogBuilder::all()
{
    return auto_specs();
}

const OllamaAutoSettingSpec* OllamaSettingCatalogBuilder::find(const std::string& key)
{
    const auto it = auto_index().find(key);
    return it == auto_index().end() ? nullptr : it->second;
}

bool OllamaSettingCatalogBuilder::is_restricted_key(const std::string& key)
{
    const OllamaAutoSettingSpec* sp = find(key);
    return sp && sp->tier == OllamaSettingTier::Restricted;
}

OllamaSettingTier OllamaSettingCatalogBuilder::tier_for_key(const std::string& key)
{
    const OllamaAutoSettingSpec* sp = find(key);
    return sp ? sp->tier : OllamaSettingTier::Restricted;
}

nlohmann::json OllamaSettingCatalogBuilder::build_index(int max_tier)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const OllamaAutoSettingSpec& sp : all()) {
        if (static_cast<int>(sp.tier) > max_tier)
            continue;
        nlohmann::json e;
        e["key"]      = sp.key;
        e["label"]    = sp.label;
        e["category"] = sp.category;
        e["ai_tier"]  = static_cast<int>(sp.tier);
        e["scope"]    = sp.preset_scope;
        arr.push_back(std::move(e));
    }
    return arr;
}

nlohmann::json OllamaSettingCatalogBuilder::build_catalog(const DynamicPrintConfig* cfg, bool ko_ui, int max_tier)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const OllamaAutoSettingSpec& sp : all()) {
        if (static_cast<int>(sp.tier) > max_tier || sp.virtual_key)
            continue;
        const DynamicPrintConfig* use_cfg = cfg;
        arr.push_back(spec_to_json(sp, use_cfg && use_cfg->has(sp.key) ? use_cfg : nullptr, ko_ui));
    }
    return arr;
}

nlohmann::json OllamaSettingCatalogBuilder::build_catalog_for_keys(const DynamicPrintConfig* print_cfg, bool ko_ui,
                                                                   const std::vector<std::string>& keys)
{
    const DynamicPrintConfig* filament_cfg = nullptr;
    const DynamicPrintConfig* printer_cfg  = nullptr;
    nlohmann::json            arr          = nlohmann::json::array();
    for (const std::string& key : keys) {
        const OllamaAutoSettingSpec* sp = find(key);
        if (!sp || sp->virtual_key || sp->tier == OllamaSettingTier::Restricted)
            continue;
        const DynamicPrintConfig* cfg =
            cfg_for_scope(sp->preset_scope, print_cfg, filament_cfg, printer_cfg);
        arr.push_back(spec_to_json(*sp, cfg, ko_ui));
    }
    return arr;
}

nlohmann::json OllamaSettingCatalogBuilder::explain_setting(const std::string& key, bool /*ko_ui*/)
{
    const OllamaAutoSettingSpec* sp = find(key);
    if (!sp)
        return nlohmann::json{{"key", key}, {"error", "unknown key"}};
    nlohmann::json out;
    out["key"]    = sp->key;
    out["label"]  = sp->label;
    out["format"] = sp->format;
    if (!sp->tooltip.empty())
        out["description"] = sp->tooltip;
    return out;
}

}} // namespace

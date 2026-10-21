#include "AssistantSettingRegistry.hpp"

#include "AssistantSettingCatalogBuilder.hpp"
#include "AssistantConfig.hpp"
#include "AssistantSettingSearch.hpp"

#ifndef ASSISTANT_HEADLESS_TEST
#include "../GUI_App.hpp"
#include <wx/app.h>
#include <wx/intl.h>
#endif

#include "libslic3r/PrintConfig.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Slic3r { namespace GUI {

namespace {

std::string localized_description(const char* english, bool korean_ui)
{
#ifndef ASSISTANT_HEADLESS_TEST
    if (korean_ui)
        return wxGetTranslation(wxString::FromUTF8(english)).utf8_string();
#else
    (void) korean_ui;
#endif
    return english;
}

const AssistantSettingSpec kSpecs[] = {
    {"layer_height", "mm", "mm", 0.04, 0.6,
     "Height of each printed layer", "number without unit, e.g. 0.2", "layer, layer height"},
    {"initial_layer_print_height", "mm", "mm", 0.04, 0.6,
     "First layer height", "number without unit", "first layer"},
    {"line_width", "mm", "mm", 0.1, 2.0,
     "Extrusion line width", "number without unit", "line width"},
    {"sparse_infill_density", "percent", "%", 0, 100,
     "How solid the inside is", "string with %, e.g. \"20%\"", "infill, density"},
    {"sparse_infill_pattern", "enum", "", 0, 0,
     "Infill pattern name", "string: grid, cubic, gyroid, etc.", "infill pattern"},
    {"wall_loops", "count", "loops", 1, 20,
     "Number of perimeter shells", "integer", "walls, wall"},
    {"top_shell_layers", "count", "layers", 1, 20,
     "Solid top layers", "integer", "top layers"},
    {"bottom_shell_layers", "count", "layers", 1, 20,
     "Solid bottom layers", "integer", "bottom layers"},
    {"enable_support", "bool", "", 0, 1,
     "Build supports for overhangs", "true/false", "support, overhang"},
    {"brim_width", "mm", "mm", 0, 25,
     "Brim width; 0=off", "number mm; 0 off, 3-8 typical", "brim"},
    {"brim_type", "enum", "", 0, 0,
     "Brim style", "outer_only, etc.", "brim type"},
    {"enable_brim", "bool", "", 0, 1,
     "Brim on/off alias", "true/false", "brim enable"},
    {"outer_wall_speed", "speed", "mm/s", 5, 500,
     "Outer wall print speed", "number", "outer wall speed"},
    {"sparse_infill_speed", "speed", "mm/s", 5, 500,
     "Infill print speed", "number", "infill speed"},
    {"infill_anchor", "mm", "mm", 0, 20,
     "Infill anchor length", "number", "infill anchor"},
    {"infill_anchor_max", "mm", "mm", 0, 50,
     "Max infill anchor", "number", "infill anchor max"},
    {"seam_position", "enum", "", 0, 0,
     "Seam placement", "string", "seam"},
    {"ironing_type", "enum", "", 0, 0,
     "Ironing mode", "string", "ironing"},
    {"ironing_flow", "percent", "%", 0, 100,
     "Ironing flow", "number", "ironing flow"},
    {"ironing_spacing", "mm", "mm", 0, 1,
     "Ironing spacing", "number", "ironing spacing"},
    {"elefant_foot_compensation", "mm", "mm", 0, 1,
     "Elephant foot compensation", "number", "elephant foot"},
    {"raft_layers", "count", "layers", 0, 20,
     "Raft layer count", "integer", "raft"},
    {"support_type", "enum", "", 0, 0,
     "Support style", "string", "support type"},
    {"support_on_build_plate_only", "bool", "", 0, 1,
     "Supports only from bed", "true/false", "support on plate"},
    {"support_critical_regions_only", "bool", "", 0, 1,
     "Critical supports only", "true/false", "support critical"},
    {"support_top_z_distance", "mm", "mm", 0, 5,
     "Support top gap", "number", "support top distance"},
    {"support_bottom_z_distance", "mm", "mm", 0, 5,
     "Support bottom gap", "number", "support bottom distance"},
    {"retraction_length", "mm", "mm", 0, 10,
     "Filament retraction distance", "number mm", "retraction, stringing",
     "filament"},
    {"retraction_speed", "speed", "mm/s", 1, 100,
     "Filament retraction speed", "number", "retraction speed",
     "filament"},
    {"nozzle_temperature", "temp", "C", 150, 300,
     "Nozzle temperature", "integer C", "temperature, temp",
     "filament"},
};

bool is_high_priority_key(const char* key)
{
    static const std::unordered_set<std::string> high = {
        "layer_height", "enable_support", "brim_width", "brim_type", "sparse_infill_density",
        "sparse_infill_pattern", "wall_loops", "initial_layer_print_height", "raft_layers",
        "support_type",
    };
    return high.find(key) != high.end();
}

std::vector<AssistantSettingSpec> build_specs()
{
    std::vector<AssistantSettingSpec> specs(std::begin(kSpecs), std::end(kSpecs));
    for (AssistantSettingSpec& sp : specs) {
        if (std::strcmp(sp.key, "enable_brim") == 0)
            sp.virtual_key = true;
        if (std::strcmp(sp.key, "retraction_length") == 0 || std::strcmp(sp.key, "retraction_speed") == 0
            || std::strcmp(sp.key, "nozzle_temperature") == 0)
            sp.preset_scope = "filament";
        if (is_high_priority_key(sp.key))
            sp.context_priority = 100;
    }
    return specs;
}

double clamp_double(double v, double lo, double hi)
{
    return std::max(lo, std::min(hi, v));
}

} // namespace

const std::vector<AssistantSettingSpec>& AssistantSettingRegistry::all()
{
    static const std::vector<AssistantSettingSpec> specs = build_specs();
    return specs;
}

static const std::unordered_map<std::string, const AssistantSettingSpec*>& spec_index()
{
    static const std::unordered_map<std::string, const AssistantSettingSpec*> index = [] {
        std::unordered_map<std::string, const AssistantSettingSpec*> map;
        map.reserve(AssistantSettingRegistry::all().size());
        for (const AssistantSettingSpec& sp : AssistantSettingRegistry::all())
            map.emplace(sp.key, &sp);
        return map;
    }();
    return index;
}

static bool auto_allows_key(const std::string& key, const std::string& preset)
{
    const AssistantAutoSettingSpec* sp = AssistantSettingCatalogBuilder::find(key);
    if (!sp || sp->virtual_key || sp->tier == AssistantSettingTier::Restricted)
        return false;
    if (preset.empty())
        return true;
    return preset == sp->preset_scope;
}

const AssistantSettingSpec* AssistantSettingRegistry::find_spec(const std::string& key)
{
    const auto it = spec_index().find(key);
    if (it != spec_index().end())
        return it->second;
    return nullptr;
}

bool AssistantSettingRegistry::is_virtual_key(const std::string& key)
{
    const AssistantSettingSpec* sp = find_spec(key);
    return sp && sp->virtual_key;
}

bool AssistantSettingRegistry::is_allowed_key(const std::string& key)
{
    if (find_spec(key))
        return true;
    if (assistant_auto_catalog_enabled())
        return auto_allows_key(key, {});
    return false;
}

bool AssistantSettingRegistry::is_allowed_key(const std::string& key, const std::string& preset)
{
    const AssistantSettingSpec* sp = find_spec(key);
    if (sp)
        return preset == sp->preset_scope;
    if (assistant_auto_catalog_enabled())
        return auto_allows_key(key, preset);
    return false;
}

bool AssistantSettingRegistry::clamp_json_value(const std::string& key, nlohmann::json& value)
{
    const AssistantSettingSpec* sp = find_spec(key);
    if (sp) {
        if (sp->virtual_key)
            return false;
        const std::string vt = sp->value_type;
        if (vt == "mm" || vt == "speed") {
            if (!value.is_number())
                return false;
            const double clamped = clamp_double(value.get<double>(), sp->min_v, sp->max_v);
            if (clamped != value.get<double>()) {
                value = clamped;
                return true;
            }
            return false;
        }
        if (vt == "percent" && value.is_number()) {
            const double clamped = clamp_double(value.get<double>(), sp->min_v, sp->max_v);
            value              = std::to_string(static_cast<int>(std::lround(clamped))) + "%";
            return true;
        }
        if (vt == "count" && value.is_number_integer()) {
            const int clamped = static_cast<int>(clamp_double(value.get<int>(), sp->min_v, sp->max_v));
            if (clamped != value.get<int>()) {
                value = clamped;
                return true;
            }
            return false;
        }
        if (vt == "temp" && value.is_number()) {
            const int clamped = static_cast<int>(clamp_double(value.get<double>(), sp->min_v, sp->max_v));
            value             = clamped;
            return true;
        }
        if (vt == "bool" && value.is_number()) {
            value = value.get<double>() != 0.0;
            return true;
        }
        return false;
    }

    if (!assistant_auto_catalog_enabled())
        return false;
    const AssistantAutoSettingSpec* asp = AssistantSettingCatalogBuilder::find(key);
    if (!asp || asp->virtual_key)
        return false;
    const std::string vt = asp->value_type;
    if ((vt == "mm" || vt == "speed" || vt == "temp" || vt == "count") && asp->max_v > asp->min_v) {
        if (!value.is_number())
            return false;
        const double clamped = clamp_double(value.get<double>(), asp->min_v, asp->max_v);
        if (vt == "count" || vt == "temp") {
            const int as_int = static_cast<int>(std::lround(clamped));
            if (value.get<double>() != as_int) {
                value = as_int;
                return true;
            }
            return false;
        }
        if (clamped != value.get<double>()) {
            value = clamped;
            return true;
        }
    }
    if (vt == "percent" && value.is_number() && asp->max_v >= asp->min_v) {
        const double clamped = clamp_double(value.get<double>(), asp->min_v, asp->max_v);
        value                = std::to_string(static_cast<int>(std::lround(clamped))) + "%";
        return true;
    }
    if (vt == "bool" && value.is_number()) {
        value = value.get<double>() != 0.0;
        return true;
    }
    return false;
}

nlohmann::json AssistantSettingRegistry::build_catalog(const DynamicPrintConfig* cfg, bool ko_ui)
{
    if (assistant_auto_catalog_enabled())
        return AssistantSettingCatalogBuilder::build_catalog(cfg, ko_ui, 3);
    nlohmann::json arr = nlohmann::json::array();
    for (const AssistantSettingSpec& sp : all()) {
        if (sp.virtual_key)
            continue;
        nlohmann::json e;
        e["key"]         = sp.key;
        e["value_type"]  = sp.value_type;
        e["unit"]        = sp.unit;
        e["min"]         = sp.min_v;
        e["max"]         = sp.max_v;
        e["description"] = localized_description(sp.desc_en, ko_ui);
        e["format"]      = sp.format;
        e["aliases"]     = sp.aliases;
        e["preset_scope"] = sp.preset_scope;
        if (cfg && cfg->has(sp.key))
            e["current"] = cfg->opt_serialize(sp.key);
        else
            e["current"] = nullptr;
        arr.push_back(std::move(e));
    }
    return arr;
}

nlohmann::json AssistantSettingRegistry::build_priority_catalog(const DynamicPrintConfig* cfg, bool ko_ui, size_t max_entries)
{
    if (assistant_auto_catalog_enabled()) {
        std::vector<const AssistantAutoSettingSpec*> ordered;
        for (const AssistantAutoSettingSpec& sp : AssistantSettingCatalogBuilder::all()) {
            if (sp.virtual_key || sp.tier == AssistantSettingTier::Restricted)
                continue;
            ordered.push_back(&sp);
        }
        std::sort(ordered.begin(), ordered.end(), [](const AssistantAutoSettingSpec* a, const AssistantAutoSettingSpec* b) {
            if (a->context_priority != b->context_priority)
                return a->context_priority > b->context_priority;
            return a->key < b->key;
        });
        nlohmann::json arr = nlohmann::json::array();
        for (const AssistantAutoSettingSpec* sp : ordered) {
            if (max_entries > 0 && arr.size() >= max_entries)
                break;
            nlohmann::json e;
            e["key"]         = sp->key;
            e["value_type"]  = sp->value_type;
            e["unit"]        = sp->unit;
            e["min"]         = sp->min_v;
            e["max"]         = sp->max_v;
            e["description"] = sp->tooltip.empty() ? sp->label : sp->tooltip;
            e["format"]      = sp->format;
            e["preset_scope"] = sp->preset_scope;
            if (cfg && cfg->has(sp->key))
                e["current"] = cfg->opt_serialize(sp->key);
            else
                e["current"] = nullptr;
            (void) ko_ui;
            arr.push_back(std::move(e));
        }
        return arr;
    }
    std::vector<const AssistantSettingSpec*> ordered;
    ordered.reserve(all().size());
    for (const AssistantSettingSpec& sp : all()) {
        if (!sp.virtual_key)
            ordered.push_back(&sp);
    }
    std::sort(ordered.begin(), ordered.end(), [](const AssistantSettingSpec* a, const AssistantSettingSpec* b) {
        if (a->context_priority != b->context_priority)
            return a->context_priority > b->context_priority;
        return std::strcmp(a->key, b->key) < 0;
    });

    nlohmann::json arr = nlohmann::json::array();
    for (const AssistantSettingSpec* sp : ordered) {
        if (max_entries > 0 && arr.size() >= max_entries)
            break;
        nlohmann::json e;
        e["key"]         = sp->key;
        e["value_type"]  = sp->value_type;
        e["unit"]        = sp->unit;
        e["min"]         = sp->min_v;
        e["max"]         = sp->max_v;
        e["description"] = localized_description(sp->desc_en, ko_ui);
        e["format"]      = sp->format;
        e["aliases"]     = sp->aliases;
        if (cfg && cfg->has(sp->key))
            e["current"] = cfg->opt_serialize(sp->key);
        else
            e["current"] = nullptr;
        arr.push_back(std::move(e));
    }
    return arr;
}

nlohmann::json AssistantSettingRegistry::build_setting_index(int max_tier)
{
    if (assistant_auto_catalog_enabled())
        return AssistantSettingCatalogBuilder::build_index(max_tier);
    nlohmann::json arr = nlohmann::json::array();
    for (const AssistantSettingSpec& sp : all()) {
        if (sp.virtual_key)
            continue;
        arr.push_back({{"key", sp.key}, {"label", sp.desc_en}, {"category", "legacy"}, {"ai_tier", 1}});
    }
    return arr;
}

nlohmann::json AssistantSettingRegistry::lookup_catalog_keys(const DynamicPrintConfig* cfg, bool ko_ui,
                                                          const std::vector<std::string>& keys)
{
    if (assistant_auto_catalog_enabled()) {
        const DynamicPrintConfig* filament_cfg = nullptr;
#ifndef ASSISTANT_HEADLESS_TEST
        if (wxTheApp) {
            if (auto* bundle = wxGetApp().preset_bundle)
                filament_cfg = &bundle->filaments.get_edited_preset().config;
        }
#endif
        return AssistantSettingSearch::lookup(keys, cfg, filament_cfg, ko_ui);
    }
    nlohmann::json arr = nlohmann::json::array();
    for (const std::string& key : keys) {
        const AssistantSettingSpec* sp = find_spec(key);
        if (!sp || sp->virtual_key)
            continue;
        nlohmann::json e;
        e["key"]         = sp->key;
        e["value_type"]  = sp->value_type;
        e["unit"]        = sp->unit;
        e["min"]         = sp->min_v;
        e["max"]         = sp->max_v;
        e["description"] = localized_description(sp->desc_en, ko_ui);
        e["format"]      = sp->format;
        e["preset_scope"] = sp->preset_scope;
        if (cfg && cfg->has(sp->key))
            e["current"] = cfg->opt_serialize(sp->key);
        else
            e["current"] = nullptr;
        arr.push_back(std::move(e));
    }
    return arr;
}

nlohmann::json AssistantSettingRegistry::allowed_keys_json()
{
    nlohmann::json arr = nlohmann::json::array();
    if (assistant_auto_catalog_enabled()) {
        for (const AssistantAutoSettingSpec& sp : AssistantSettingCatalogBuilder::all()) {
            if (!sp.virtual_key && sp.tier != AssistantSettingTier::Restricted)
                arr.push_back(sp.key);
        }
        return arr;
    }
    for (const AssistantSettingSpec& sp : all()) {
        if (!sp.virtual_key)
            arr.push_back(sp.key);
    }
    return arr;
}

std::string AssistantSettingRegistry::config_fingerprint(const DynamicPrintConfig* cfg)
{
    if (!cfg)
        return {};
    std::ostringstream oss;
    for (const AssistantSettingSpec& sp : all()) {
        if (sp.virtual_key || !cfg->has(sp.key))
            continue;
        try {
            oss << sp.key << '=' << cfg->opt_serialize(sp.key) << ';';
        } catch (...) {
        }
    }
    return std::to_string(std::hash<std::string>{}(oss.str()));
}

}} // namespace

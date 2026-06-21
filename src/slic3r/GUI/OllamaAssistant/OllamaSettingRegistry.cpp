#include "OllamaSettingRegistry.hpp"

#include "OllamaSettingCatalogBuilder.hpp"
#include "OllamaConfig.hpp"
#include "OllamaSettingSearch.hpp"

#include "../GUI_App.hpp"

#include "libslic3r/PrintConfig.hpp"

#include <wx/app.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Slic3r { namespace GUI {

namespace {

const OllamaSettingSpec kSpecs[] = {
    {"layer_height", "mm", "mm", 0.04, 0.6,
     "Height of each printed layer", "한 층의 높이", "number without unit, e.g. 0.2", "layer, layer height, 층"},
    {"initial_layer_print_height", "mm", "mm", 0.04, 0.6,
     "First layer height", "첫 층 높이", "number without unit", "first layer, 첫층"},
    {"line_width", "mm", "mm", 0.1, 2.0,
     "Extrusion line width", "라인 너비", "number without unit", "line width, 라인"},
    {"sparse_infill_density", "percent", "%", 0, 100,
     "How solid the inside is", "안쪽 채움 밀도", "string with %, e.g. \"20%\"", "infill, density, 채움, infill"},
    {"sparse_infill_pattern", "enum", "", 0, 0,
     "Infill pattern name", "채움 패턴", "string: grid, cubic, gyroid, etc.", "infill pattern"},
    {"wall_loops", "count", "loops", 1, 20,
     "Number of perimeter shells", "겉벽 겹 수", "integer", "walls, wall, 벽"},
    {"top_shell_layers", "count", "layers", 1, 20,
     "Solid top layers", "윗면 껍질 층", "integer", "top layers"},
    {"bottom_shell_layers", "count", "layers", 1, 20,
     "Solid bottom layers", "아랫면 껍질 층", "integer", "bottom layers"},
    {"enable_support", "bool", "", 0, 1,
     "Build supports for overhangs", "서포트(받침) 사용", "true/false", "support, overhang, 서포트"},
    {"brim_width", "mm", "mm", 0, 25,
     "Brim width; 0=off", "브림 너비; 0=끔", "number mm; 0 off, 3-8 typical", "brim, 브림"},
    {"brim_type", "enum", "", 0, 0,
     "Brim style", "브림 종류", "outer_only, etc.", "brim type"},
    {"enable_brim", "bool", "", 0, 1,
     "Brim on/off alias", "브림 켜기", "true/false", "brim enable"},
    {"outer_wall_speed", "speed", "mm/s", 5, 500,
     "Outer wall print speed", "외벽 속도", "number", "outer wall speed"},
    {"sparse_infill_speed", "speed", "mm/s", 5, 500,
     "Infill print speed", "채움 속도", "number", "infill speed"},
    {"infill_anchor", "mm", "mm", 0, 20,
     "Infill anchor length", "채움 앵커", "number", "infill anchor"},
    {"infill_anchor_max", "mm", "mm", 0, 50,
     "Max infill anchor", "채움 앵커 최대", "number", "infill anchor max"},
    {"seam_position", "enum", "", 0, 0,
     "Seam placement", "솔기 위치", "string", "seam"},
    {"ironing_type", "enum", "", 0, 0,
     "Ironing mode", "아이어링", "string", "ironing"},
    {"ironing_flow", "percent", "%", 0, 100,
     "Ironing flow", "아이어링 유량", "number", "ironing flow"},
    {"ironing_spacing", "mm", "mm", 0, 1,
     "Ironing spacing", "아이어링 간격", "number", "ironing spacing"},
    {"elefant_foot_compensation", "mm", "mm", 0, 1,
     "Elephant foot compensation", "코끼리발 보정", "number", "elephant foot"},
    {"raft_layers", "count", "layers", 0, 20,
     "Raft layer count", "뗏목 층 수", "integer", "raft"},
    {"support_type", "enum", "", 0, 0,
     "Support style", "서포트 종류", "string", "support type"},
    {"support_on_build_plate_only", "bool", "", 0, 1,
     "Supports only from bed", "베드에서만 서포트", "true/false", "support on plate"},
    {"support_critical_regions_only", "bool", "", 0, 1,
     "Critical supports only", "필요 구역만 서포트", "true/false", "support critical"},
    {"support_top_z_distance", "mm", "mm", 0, 5,
     "Support top gap", "서포트 상단 간격", "number", "support top distance"},
    {"support_bottom_z_distance", "mm", "mm", 0, 5,
     "Support bottom gap", "서포트 하단 간격", "number", "support bottom distance"},
    {"retraction_length", "mm", "mm", 0, 10,
     "Filament retraction distance", "리트랙션 길이", "number mm", "retraction, stringing, 실",
     "filament"},
    {"retraction_speed", "speed", "mm/s", 1, 100,
     "Filament retraction speed", "리트랙션 속도", "number", "retraction speed",
     "filament"},
    {"nozzle_temperature", "temp", "C", 150, 300,
     "Nozzle temperature", "노즐 온도", "integer C", "temperature, temp, 온도",
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

std::vector<OllamaSettingSpec> build_specs()
{
    std::vector<OllamaSettingSpec> specs(std::begin(kSpecs), std::end(kSpecs));
    for (OllamaSettingSpec& sp : specs) {
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

const std::vector<OllamaSettingSpec>& OllamaSettingRegistry::all()
{
    static const std::vector<OllamaSettingSpec> specs = build_specs();
    return specs;
}

static const std::unordered_map<std::string, const OllamaSettingSpec*>& spec_index()
{
    static const std::unordered_map<std::string, const OllamaSettingSpec*> index = [] {
        std::unordered_map<std::string, const OllamaSettingSpec*> map;
        map.reserve(OllamaSettingRegistry::all().size());
        for (const OllamaSettingSpec& sp : OllamaSettingRegistry::all())
            map.emplace(sp.key, &sp);
        return map;
    }();
    return index;
}

static bool auto_allows_key(const std::string& key, const std::string& preset)
{
    const OllamaAutoSettingSpec* sp = OllamaSettingCatalogBuilder::find(key);
    if (!sp || sp->virtual_key || sp->tier == OllamaSettingTier::Restricted)
        return false;
    if (preset.empty())
        return true;
    return preset == sp->preset_scope;
}

const OllamaSettingSpec* OllamaSettingRegistry::find_spec(const std::string& key)
{
    const auto it = spec_index().find(key);
    if (it != spec_index().end())
        return it->second;
    return nullptr;
}

bool OllamaSettingRegistry::is_virtual_key(const std::string& key)
{
    const OllamaSettingSpec* sp = find_spec(key);
    return sp && sp->virtual_key;
}

bool OllamaSettingRegistry::is_allowed_key(const std::string& key)
{
    if (find_spec(key))
        return true;
    if (ollama_auto_catalog_enabled())
        return auto_allows_key(key, {});
    return false;
}

bool OllamaSettingRegistry::is_allowed_key(const std::string& key, const std::string& preset)
{
    const OllamaSettingSpec* sp = find_spec(key);
    if (sp)
        return preset == sp->preset_scope;
    if (ollama_auto_catalog_enabled())
        return auto_allows_key(key, preset);
    return false;
}

bool OllamaSettingRegistry::clamp_json_value(const std::string& key, nlohmann::json& value)
{
    const OllamaSettingSpec* sp = find_spec(key);
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

    if (!ollama_auto_catalog_enabled())
        return false;
    const OllamaAutoSettingSpec* asp = OllamaSettingCatalogBuilder::find(key);
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

nlohmann::json OllamaSettingRegistry::build_catalog(const DynamicPrintConfig* cfg, bool ko_ui)
{
    if (ollama_auto_catalog_enabled())
        return OllamaSettingCatalogBuilder::build_catalog(cfg, ko_ui, 3);
    nlohmann::json arr = nlohmann::json::array();
    for (const OllamaSettingSpec& sp : all()) {
        if (sp.virtual_key)
            continue;
        nlohmann::json e;
        e["key"]         = sp.key;
        e["value_type"]  = sp.value_type;
        e["unit"]        = sp.unit;
        e["min"]         = sp.min_v;
        e["max"]         = sp.max_v;
        e["description"] = ko_ui ? sp.desc_ko : sp.desc_en;
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

nlohmann::json OllamaSettingRegistry::build_priority_catalog(const DynamicPrintConfig* cfg, bool ko_ui, size_t max_entries)
{
    if (ollama_auto_catalog_enabled()) {
        std::vector<const OllamaAutoSettingSpec*> ordered;
        for (const OllamaAutoSettingSpec& sp : OllamaSettingCatalogBuilder::all()) {
            if (sp.virtual_key || sp.tier == OllamaSettingTier::Restricted)
                continue;
            ordered.push_back(&sp);
        }
        std::sort(ordered.begin(), ordered.end(), [](const OllamaAutoSettingSpec* a, const OllamaAutoSettingSpec* b) {
            if (a->context_priority != b->context_priority)
                return a->context_priority > b->context_priority;
            return a->key < b->key;
        });
        nlohmann::json arr = nlohmann::json::array();
        for (const OllamaAutoSettingSpec* sp : ordered) {
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
    std::vector<const OllamaSettingSpec*> ordered;
    ordered.reserve(all().size());
    for (const OllamaSettingSpec& sp : all()) {
        if (!sp.virtual_key)
            ordered.push_back(&sp);
    }
    std::sort(ordered.begin(), ordered.end(), [](const OllamaSettingSpec* a, const OllamaSettingSpec* b) {
        if (a->context_priority != b->context_priority)
            return a->context_priority > b->context_priority;
        return std::strcmp(a->key, b->key) < 0;
    });

    nlohmann::json arr = nlohmann::json::array();
    for (const OllamaSettingSpec* sp : ordered) {
        if (max_entries > 0 && arr.size() >= max_entries)
            break;
        nlohmann::json e;
        e["key"]         = sp->key;
        e["value_type"]  = sp->value_type;
        e["unit"]        = sp->unit;
        e["min"]         = sp->min_v;
        e["max"]         = sp->max_v;
        e["description"] = ko_ui ? sp->desc_ko : sp->desc_en;
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

nlohmann::json OllamaSettingRegistry::build_setting_index(int max_tier)
{
    if (ollama_auto_catalog_enabled())
        return OllamaSettingCatalogBuilder::build_index(max_tier);
    nlohmann::json arr = nlohmann::json::array();
    for (const OllamaSettingSpec& sp : all()) {
        if (sp.virtual_key)
            continue;
        arr.push_back({{"key", sp.key}, {"label", sp.desc_en}, {"category", "legacy"}, {"ai_tier", 1}});
    }
    return arr;
}

nlohmann::json OllamaSettingRegistry::lookup_catalog_keys(const DynamicPrintConfig* cfg, bool ko_ui,
                                                          const std::vector<std::string>& keys)
{
    if (ollama_auto_catalog_enabled()) {
        const DynamicPrintConfig* filament_cfg = nullptr;
        if (wxTheApp) {
            if (auto* bundle = wxGetApp().preset_bundle)
                filament_cfg = &bundle->filaments.get_edited_preset().config;
        }
        return OllamaSettingSearch::lookup(keys, cfg, filament_cfg, ko_ui);
    }
    nlohmann::json arr = nlohmann::json::array();
    for (const std::string& key : keys) {
        const OllamaSettingSpec* sp = find_spec(key);
        if (!sp || sp->virtual_key)
            continue;
        nlohmann::json e;
        e["key"]         = sp->key;
        e["value_type"]  = sp->value_type;
        e["unit"]        = sp->unit;
        e["min"]         = sp->min_v;
        e["max"]         = sp->max_v;
        e["description"] = ko_ui ? sp->desc_ko : sp->desc_en;
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

nlohmann::json OllamaSettingRegistry::allowed_keys_json()
{
    nlohmann::json arr = nlohmann::json::array();
    if (ollama_auto_catalog_enabled()) {
        for (const OllamaAutoSettingSpec& sp : OllamaSettingCatalogBuilder::all()) {
            if (!sp.virtual_key && sp.tier != OllamaSettingTier::Restricted)
                arr.push_back(sp.key);
        }
        return arr;
    }
    for (const OllamaSettingSpec& sp : all()) {
        if (!sp.virtual_key)
            arr.push_back(sp.key);
    }
    return arr;
}

std::string OllamaSettingRegistry::config_fingerprint(const DynamicPrintConfig* cfg)
{
    if (!cfg)
        return {};
    std::ostringstream oss;
    for (const OllamaSettingSpec& sp : all()) {
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

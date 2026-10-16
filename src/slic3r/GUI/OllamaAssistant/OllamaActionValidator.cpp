#include "OllamaActionValidator.hpp"
#include "OllamaActionExecutor.hpp"
#include "OllamaActionRegistry.hpp"
#include "OllamaIntentContext.hpp"
#include "OllamaSettingRegistry.hpp"
#include "OllamaSettingCatalogBuilder.hpp"
#include "OllamaTelemetry.hpp"
#include "../MakerWorld/MakerWorldUrl.hpp"
#include "../GUI_App.hpp"

#include <boost/algorithm/string.hpp>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <unordered_set>

namespace Slic3r { namespace GUI {

namespace {

double clamp_double(double v, double lo, double hi)
{
    return std::max(lo, std::min(hi, v));
}

double normalize_degrees(double deg)
{
    while (deg > 360.0)
        deg -= 360.0;
    while (deg < -360.0)
        deg += 360.0;
    return deg;
}

bool contains_ci(const std::string& hay, const char* needle)
{
    return boost::ifind_first(hay, needle);
}

bool is_blocked_action_type(const std::string& type)
{
    return OllamaActionRegistry::is_blocked_type(type);
}

bool is_allowed_action_type(const std::string& type)
{
    return OllamaActionRegistry::is_allowed_type(OllamaActionValidator::canonical_action_type(type));
}

bool is_model_shape_action_type(const std::string& type)
{
    return type == "scale" || type == "rotate" || type == "translate" || type == "mesh_boolean"
        || type == "mirror_mesh" || type == "repair_mesh" || type == "split_object" || type == "clone_selection";
}

void warn(OllamaActionSanitizeResult& out, std::string msg);
void block_action(OllamaActionSanitizeResult& out, const std::string& reason, bool quiet);

void prune_set_config_and_slice_for_model_shape_turn(nlohmann::json& root, OllamaActionSanitizeResult& out)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return;

    bool has_shape = false;
    for (const auto& a : root["actions"]) {
        if (!a.is_object())
            continue;
        const std::string type = OllamaActionValidator::canonical_action_type(a.value("type", ""));
        if (is_model_shape_action_type(type)) {
            has_shape = true;
            break;
        }
    }
    if (!has_shape)
        return;

    nlohmann::json kept = nlohmann::json::array();
    for (const auto& a : root["actions"]) {
        if (!a.is_object()) {
            kept.push_back(a);
            continue;
        }
        const std::string type = OllamaActionValidator::canonical_action_type(a.value("type", ""));
        if (type == "set_config" || type == "slice") {
            block_action(out, "Dropped " + type + " — model-shape turn uses geometry/mesh tools only", /*quiet*/ true);
            continue;
        }
        kept.push_back(a);
    }
    root["actions"] = std::move(kept);
}

void warn(OllamaActionSanitizeResult& out, std::string msg)
{
    out.warnings.push_back(std::move(msg));
}

void block_action(OllamaActionSanitizeResult& out, const std::string& reason, bool quiet = false)
{
    ++out.blocked_count;
    if (!quiet)
        warn(out, std::move(reason));
}

bool sanitize_set_config(nlohmann::json& action, OllamaActionSanitizeResult& out)
{
    if (!action.contains("options") || !action["options"].is_object()) {
        block_action(out, "set_config: missing options object");
        return false;
    }

    std::string preset = action.value("preset", "print");
    if (preset != "print" && preset != "filament" && preset != "printer") {
        preset           = "print";
        action["preset"] = preset;
        warn(out, "set_config: invalid preset, defaulted to print");
    }

    if (action.contains("options") && action["options"].is_object()) {
        for (auto it = action["options"].begin(); it != action["options"].end(); ++it) {
            const std::string key = OllamaActionExecutor::normalize_config_key(it.key());
            if (OllamaSettingRegistry::is_allowed_key(key, "filament")
                && !OllamaSettingRegistry::is_allowed_key(key, preset)) {
                preset           = "filament";
                action["preset"] = preset;
                if (!action.contains("filament_index"))
                    action["filament_index"] = 0;
                break;
            }
        }
    }

    if (preset == "filament") {
        int fidx = action.value("filament_index", 0);
        if (auto* bundle = wxGetApp().preset_bundle) {
            const int max_slots = static_cast<int>(bundle->filament_presets.size());
            fidx                     = OllamaIntentContext::clamp_filament_index(fidx, max_slots);
            action["filament_index"] = fidx;
        } else {
            action["filament_index"] = 0;
        }
    } else if (action.contains("filament_index")) {
        action.erase("filament_index");
    }

    OllamaActionExecutor::normalize_set_config_options(action["options"]);

    nlohmann::json filtered = nlohmann::json::object();
    for (auto it = action["options"].begin(); it != action["options"].end(); ++it) {
        const std::string key = OllamaActionExecutor::normalize_config_key(it.key());
        if (OllamaSettingRegistry::is_virtual_key(key)) {
            filtered[it.key()] = it.value();
            continue;
        }
        if (!OllamaSettingRegistry::is_allowed_key(key, preset)) {
            block_action(out, "Blocked config key for preset " + preset + ": " + it.key(), /*quiet*/ true);
            OllamaTelemetry::action_blocked("set_config", "preset_scope:" + key);
            continue;
        }
        if (OllamaSettingCatalogBuilder::is_restricted_key(key)) {
            block_action(out, "Blocked restricted config key: " + key);
            OllamaTelemetry::action_blocked("set_config", "tier3:" + key);
            continue;
        }

        nlohmann::json value = it.value();
        if (key == "sparse_infill_density" && value.is_number()) {
            const double v = clamp_double(value.get<double>(), 0.0, 100.0);
            value          = std::to_string(static_cast<int>(std::lround(v))) + "%";
        } else if (key == "sparse_infill_density" && value.is_string()) {
            std::string s = value.get<std::string>();
            boost::algorithm::trim(s);
            if (s.find('%') == std::string::npos && !s.empty())
                s += "%";
            value = s;
        } else if (OllamaSettingRegistry::clamp_json_value(key, value)) {
            warn(out, key + " clamped to safe range");
        }

        filtered[key] = value;
    }

    if (filtered.empty()) {
        block_action(out, "set_config: no allowed options after filtering");
        return false;
    }

    action["options"] = filtered;

    const OllamaSetConfigDryRunResult dry = OllamaActionExecutor::dry_run_set_config(action);
    if (!dry.ok) {
        block_action(out, "set_config dry-run failed: " + dry.errors);
        OllamaTelemetry::action_blocked("set_config", "dry_run");
        return false;
    }

    return true;
}

bool sanitize_transform(nlohmann::json& action, const std::string& type, OllamaActionSanitizeResult& out)
{
    if (type == "translate") {
        for (const char* axis : {"x", "y", "z"}) {
            if (action.contains(axis) && action[axis].is_number()) {
                const double v = clamp_double(action[axis].get<double>(), -500.0, 500.0);
                if (v != action[axis].get<double>())
                    warn(out, "translate axis clamped to ±500 mm");
                action[axis] = v;
            }
        }
        return true;
    }
    if (type == "rotate") {
        auto read_deg = [](const nlohmann::json& j, const char* key) -> std::optional<double> {
            if (!j.contains(key) || j[key].is_null())
                return std::nullopt;
            if (j[key].is_number())
                return j[key].get<double>();
            if (j[key].is_string()) {
                try {
                    size_t idx = 0;
                    const double v = std::stod(j[key].get<std::string>(), &idx);
                    if (idx == 0)
                        return std::nullopt;
                    return v;
                } catch (...) {
                    return std::nullopt;
                }
            }
            return std::nullopt;
        };
        for (const char* axis : {"x", "y", "z"}) {
            if (const auto v = read_deg(action, axis))
                action[axis] = normalize_degrees(*v);
        }
        const double x = action.value("x", 0.0);
        const double y = action.value("y", 0.0);
        const double z = action.value("z", 0.0);
        if (std::abs(x) < 0.01 && std::abs(y) < 0.01 && std::abs(z) < 0.01) {
            for (const char* key : {"angle", "degrees", "yaw", "rotation"}) {
                if (const auto v = read_deg(action, key)) {
                    action["z"] = normalize_degrees(*v);
                    break;
                }
            }
        }
        return true;
    }
    if (type == "scale") {
        auto read_num = [](const nlohmann::json& j, const char* key) -> std::optional<double> {
            if (!j.contains(key) || j[key].is_null())
                return std::nullopt;
            if (j[key].is_number())
                return j[key].get<double>();
            if (j[key].is_string()) {
                try {
                    std::string s = j[key].get<std::string>();
                    if (!s.empty() && s.back() == '%')
                        s.pop_back();
                    size_t idx = 0;
                    const double v = std::stod(s, &idx);
                    if (idx == 0)
                        return std::nullopt;
                    return v;
                } catch (...) {
                    return std::nullopt;
                }
            }
            return std::nullopt;
        };
        if (!action.contains("factor")) {
            if (const auto percent = read_num(action, "percent"))
                action["factor"] = *percent / 100.0;
            else if (const auto percent = read_num(action, "percentage"))
                action["factor"] = *percent / 100.0;
        }
        auto clamp_factor = [&](const char* key) {
            const auto parsed = read_num(action, key);
            if (!parsed)
                return;
            double v = *parsed;
            if (v <= 0.0) {
                block_action(out, "scale factor must be positive");
                v = 1.0;
            } else {
                if (v > 20.0)
                    v /= 100.0;
                v = clamp_double(v, 0.05, 20.0);
            }
            action[key] = v;
        };
        clamp_factor("factor");
        clamp_factor("x");
        clamp_factor("y");
        clamp_factor("z");
        return true;
    }
    return false;
}

bool sanitize_menu_item(const nlohmann::json& action, const std::string& user, OllamaActionSanitizeResult& out)
{
    const std::string menu = action.value("menu", "");
    const std::string item = action.value("item", "");
    if (menu.empty() || item.empty())
        return false;

    if (contains_ci(item, "g-code") || contains_ci(item, "gcode")) {
        block_action(out, "Blocked G-code export via menu");
        return false;
    }
    if (contains_ci(item, "quit") || contains_ci(item, "exit")) {
        block_action(out, "Blocked application exit");
        return false;
    }

    (void) user;
    (void) menu;
    return true;
}

bool sanitize_one_action(nlohmann::json& action, const std::string& user, OllamaActionSanitizeResult& out)
{
    if (!action.is_object() || !action.contains("type") || !action["type"].is_string())
        return false;

    std::string type = action["type"].get<std::string>();
    type               = OllamaActionValidator::canonical_action_type(type);
    action["type"]     = type;
    if (is_blocked_action_type(type) || !is_allowed_action_type(type)) {
        block_action(out, "Blocked disallowed action: " + type);
        return false;
    }

    if (type == "menu_item" && !sanitize_menu_item(action, user, out))
        return false;
    if (type == "set_config" && !sanitize_set_config(action, out))
        return false;
    if ((type == "translate" || type == "rotate" || type == "scale") && !sanitize_transform(action, type, out))
        return false;
    if (type == "mesh_boolean") {
        const std::string op = action.value("operation", "");
        static const std::unordered_set<std::string> kOps = {
            "subtract_cylinder", "drill_hole", "union_box", "add_handle", "add_rib",
        };
        if (kOps.find(op) == kOps.end()) {
            block_action(out, "mesh_boolean: unknown operation");
            return false;
        }
    }
    if (type == "mirror_mesh") {
        const std::string axis = action.value("axis", "x");
        if (axis != "x" && axis != "y" && axis != "z" && axis != "X" && axis != "Y" && axis != "Z") {
            block_action(out, "mirror_mesh: axis must be x, y, or z");
            return false;
        }
    }

    if (type == "ui_select_tab") {
        static const std::unordered_set<std::string> tabs = {
            "prepare", "editor", "3d", "preview", "monitor", "smart_print", "home",
        };
        const std::string tab = action.value("tab", "");
        if (tabs.find(tab) == tabs.end()) {
            block_action(out, "Blocked unknown tab: " + tab);
            return false;
        }
    }

    if (type == "slice") {
        const std::string scope = action.value("scope", "plate");
        if (scope != "plate" && scope != "all") {
            action["scope"] = "plate";
            warn(out, "slice scope normalized to plate");
        }
    }

    if (type == "makerworld_search") {
        if (!action.contains("query") || !action["query"].is_string() || action["query"].get<std::string>().empty()) {
            block_action(out, "makerworld_search: missing query");
            return false;
        }
        return true;
    }

    if (type == "makerworld_find_and_print") {
        if (!action.contains("query") || !action["query"].is_string() || action["query"].get<std::string>().empty()) {
            block_action(out, "makerworld_find_and_print: missing query");
            return false;
        }
        return true;
    }

    if (type == "import_makerworld") {
        const bool has_id = action.contains("design_id") && action["design_id"].is_string()
            && !action["design_id"].get<std::string>().empty();
        const bool has_url = action.contains("url") && action["url"].is_string()
            && !action["url"].get<std::string>().empty();
        if (!has_id && !has_url) {
            block_action(out, "import_makerworld: need design_id or url");
            return false;
        }
        if (has_url && !is_makerworld_host_url(action["url"].get<std::string>())) {
            std::string u = action["url"].get<std::string>();
            if (u.find(".3mf") == std::string::npos) {
                block_action(out, "import_makerworld: url not allowed");
                return false;
            }
        }
        return true;
    }

    if (type == "menu_item") {
        block_action(out, "Blocked menu shortcut action", /*quiet*/ true);
        return false;
    }

    return true;
}

} // namespace

bool OllamaActionValidator::is_allowed_config_key(const std::string& key)
{
    const std::string normalized = OllamaActionExecutor::normalize_config_key(key);
    if (OllamaSettingRegistry::is_virtual_key(normalized))
        return true;
    return OllamaSettingRegistry::is_allowed_key(normalized);
}

OllamaActionSanitizeResult OllamaActionValidator::sanitize(nlohmann::json& root, const std::string& user_request)
{
    OllamaActionSanitizeResult result;
    if (!root.contains("actions") || !root["actions"].is_array())
        return result;

    nlohmann::json kept = nlohmann::json::array();
    for (auto& action : root["actions"]) {
        if (kept.size() >= kMaxActionsPerTurn) {
            block_action(result, "Action limit reached (" + std::to_string(kMaxActionsPerTurn) + ")");
            break;
        }
        if (sanitize_one_action(action, user_request, result))
            kept.push_back(std::move(action));
    }
    root["actions"] = std::move(kept);
    prune_set_config_and_slice_for_model_shape_turn(root, result);
    return result;
}

}} // namespace

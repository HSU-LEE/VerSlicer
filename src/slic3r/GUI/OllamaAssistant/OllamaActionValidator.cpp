#include "OllamaActionValidator.hpp"
#include "OllamaActionExecutor.hpp"
#include "OllamaActionRegistry.hpp"
#include "OllamaIntentContext.hpp"
#include "OllamaIntentRules.hpp"
#include "OllamaSettingRegistry.hpp"
#include "OllamaTelemetry.hpp"
#include "../MakerWorld/MakerWorldUrl.hpp"
#include "../GUI_App.hpp"

#include <boost/algorithm/string.hpp>
#include <cmath>
#include <cstdlib>
#include <unordered_set>

namespace Slic3r { namespace GUI {

namespace {

using namespace OllamaIntentRules;

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

bool user_wants_import(const std::string& user)
{
    return contains_ci(user, "import") || contains_ci(user, "add model") || contains_ci(user, "load") ||
           contains_ci(user, "open file") || user.find("불러") != std::string::npos || user.find("가져") != std::string::npos ||
           user.find("열어") != std::string::npos || user.find("추가") != std::string::npos;
}

bool user_wants_file_ops(const std::string& user)
{
    return contains_ci(user, "save") || contains_ci(user, "export") || contains_ci(user, "open") ||
           user.find("저장") != std::string::npos || user.find("보내") != std::string::npos;
}

bool is_blocked_action_type(const std::string& type)
{
    return OllamaActionRegistry::is_blocked_type(type);
}

bool is_allowed_action_type(const std::string& type)
{
    return OllamaActionRegistry::is_allowed_type(type);
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
    if (!action.contains("options") || !action["options"].is_object())
        return false;

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
            fidx                  = OllamaIntentContext::clamp_filament_index(fidx, max_slots);
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

    if (filtered.empty())
        return false;

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
        for (const char* axis : {"x", "y", "z"}) {
            if (action.contains(axis) && action[axis].is_number()) {
                const double v = normalize_degrees(action[axis].get<double>());
                action[axis] = v;
            }
        }
        return true;
    }
    if (type == "scale") {
        auto clamp_factor = [&](const char* key) {
            if (!action.contains(key) || !action[key].is_number())
                return;
            double v = action[key].get<double>();
            if (v <= 0.0) {
                block_action(out, "scale factor must be positive");
                v = 1.0;
            } else {
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

    const bool is_file_menu = boost::iequals(menu, "File") || boost::iequals(menu, "파일");
    if (is_file_menu && !user_wants_file_ops(user)) {
        if (contains_ci(item, "save") || contains_ci(item, "export") || contains_ci(item, "저장") ||
            contains_ci(item, "보내")) {
            block_action(out, "Blocked file save/export (not requested)");
            return false;
        }
    }
    return true;
}

bool sanitize_one_action(nlohmann::json& action, const std::string& user, OllamaActionSanitizeResult& out)
{
    if (!action.is_object() || !action.contains("type") || !action["type"].is_string())
        return false;

    const std::string type = action["type"].get<std::string>();
    if (is_blocked_action_type(type) || !is_allowed_action_type(type)) {
        block_action(out, "Blocked disallowed action: " + type);
        return false;
    }

    if (type == "delete_selection" && !user_wants_delete(user)) {
        block_action(out, "Blocked delete (user did not ask to delete)", /*quiet*/ true);
        return false;
    }
    if (type == "add_model" && !user_wants_import(user)) {
        block_action(out, "Blocked add_model (user did not ask to import)", /*quiet*/ true);
        return false;
    }
    if (type == "menu_item" && !sanitize_menu_item(action, user, out))
        return false;
    if (type == "set_config" && !sanitize_set_config(action, out))
        return false;
    if ((type == "translate" || type == "rotate" || type == "scale") && !sanitize_transform(action, type, out))
        return false;

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

    if (type == "arrange" && describes_print_quality_symptom(user) && !user_wants_plate_arrange(user)) {
        block_action(out, "Blocked arrange (user described print quality, not layout)", /*quiet*/ true);
        return false;
    }

    if (type == "makerworld_search") {
        if (!action.contains("query") || !action["query"].is_string() || action["query"].get<std::string>().empty()) {
            block_action(out, "makerworld_search: missing query");
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
    return result;
}

}} // namespace

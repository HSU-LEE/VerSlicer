#include "OllamaActionValidator.hpp"

#include <boost/algorithm/string.hpp>
#include <cmath>
#include <cstdlib>
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

bool user_wants_delete(const std::string& user)
{
    return contains_ci(user, "delete") || contains_ci(user, "remove") || contains_ci(user, "erase") ||
           user.find("삭제") != std::string::npos || user.find("지워") != std::string::npos;
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
    static const std::unordered_set<std::string> blocked = {
        "save_project",
        "export_gcode",
        "quit",
        "exit",
    };
    return blocked.find(type) != blocked.end();
}

bool is_allowed_action_type(const std::string& type)
{
    static const std::unordered_set<std::string> allowed = {
        "set_config",
        "ui_select_tab",
        "slice",
        "delete_selection",
        "clone_selection",
        "arrange",
        "add_model",
        "menu_item",
        "translate",
        "rotate",
        "scale",
    };
    return allowed.find(type) != allowed.end();
}

void warn(OllamaActionSanitizeResult& out, std::string msg)
{
    out.warnings.push_back(std::move(msg));
}

void block_action(OllamaActionSanitizeResult& out, const std::string& reason)
{
    ++out.blocked_count;
    warn(out, reason);
}

bool sanitize_set_config(nlohmann::json& action, OllamaActionSanitizeResult& out)
{
    if (!action.contains("options") || !action["options"].is_object())
        return false;

    const std::string preset = action.value("preset", "print");
    if (preset != "print" && preset != "filament" && preset != "printer") {
        action["preset"] = "print";
        warn(out, "set_config: invalid preset, defaulted to print");
    }

    nlohmann::json filtered = nlohmann::json::object();
    for (auto it = action["options"].begin(); it != action["options"].end(); ++it) {
        const std::string key = it.key();
        if (!OllamaActionValidator::is_allowed_config_key(key)) {
            block_action(out, "Blocked unknown config key: " + key);
            continue;
        }

        if (key == "layer_height" || key == "initial_layer_print_height") {
            if (it.value().is_number()) {
                const double v = clamp_double(it.value().get<double>(), 0.04, 0.6);
                if (v != it.value().get<double>())
                    warn(out, key + " clamped to safe range");
                filtered[key] = v;
            } else {
                filtered[key] = it.value();
            }
        } else if (key == "line_width" || key == "brim_width") {
            if (it.value().is_number()) {
                const double v = clamp_double(it.value().get<double>(), 0.1, 2.0);
                if (v != it.value().get<double>())
                    warn(out, key + " clamped to safe range");
                filtered[key] = v;
            } else {
                filtered[key] = it.value();
            }
        } else if (key == "wall_loops" || key == "top_shell_layers" || key == "bottom_shell_layers") {
            if (it.value().is_number_integer()) {
                const int v = (int)clamp_double(it.value().get<int>(), 1, 20);
                filtered[key] = v;
            } else {
                filtered[key] = it.value();
            }
        } else if (key == "outer_wall_speed" || key == "sparse_infill_speed") {
            if (it.value().is_number()) {
                const double v = clamp_double(it.value().get<double>(), 5.0, 500.0);
                filtered[key] = v;
            } else {
                filtered[key] = it.value();
            }
        } else {
            filtered[key] = it.value();
        }
    }

    if (filtered.empty())
        return false;

    action["options"] = filtered;
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
        block_action(out, "Blocked delete (user did not ask to delete)");
        return false;
    }
    if (type == "add_model" && !user_wants_import(user)) {
        block_action(out, "Blocked add_model (user did not ask to import)");
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

    return true;
}

} // namespace

bool OllamaActionValidator::is_allowed_config_key(const std::string& key)
{
    static const std::unordered_set<std::string> allowed = {
        "layer_height",
        "initial_layer_print_height",
        "line_width",
        "sparse_infill_density",
        "sparse_infill_pattern",
        "wall_loops",
        "top_shell_layers",
        "bottom_shell_layers",
        "enable_support",
        "brim_width",
        "outer_wall_speed",
        "sparse_infill_speed",
        "infill_anchor",
        "infill_anchor_max",
        "seam_position",
        "ironing_type",
        "ironing_flow",
        "ironing_spacing",
        "elefant_foot_compensation",
        "raft_layers",
        "support_type",
        "support_on_build_plate_only",
        "support_critical_regions_only",
        "support_top_z_distance",
        "support_bottom_z_distance",
    };
    return allowed.find(key) != allowed.end();
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

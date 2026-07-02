#include "OllamaResponseNormalizer.hpp"
#include "OllamaUserFlow.hpp"
#include "OllamaActionExecutor.hpp"
#include "OllamaIntentContext.hpp"
#include "OllamaRequestRouter.hpp"
#include "OllamaSettingRegistry.hpp"
#include "OllamaTelemetry.hpp"

#include "../MakerWorld/MakerWorldSearchService.hpp"

#include <boost/algorithm/string.hpp>
#include <regex>

namespace Slic3r { namespace GUI {

namespace {

static std::string extract_first_url_from_text(const std::string& text)
{
    static const std::regex link_re(R"((https?://[^\s]+))", std::regex::icase);
    std::smatch m;
    if (std::regex_search(text, m, link_re))
        return m[1].str();
    return {};
}

static bool user_allows_delete(const std::string& user)
{
    return user.find("delete") != std::string::npos || user.find("remove") != std::string::npos
        || user.find("erase") != std::string::npos || user.find("삭제") != std::string::npos
        || user.find("지워") != std::string::npos || user.find("지우") != std::string::npos;
}

static void normalize_set_config_shape(nlohmann::json& action)
{
    if (!action.is_object() || action.value("type", "") != "set_config")
        return;
    auto merge_alias = [&](const char* alias) {
        if (!action.contains(alias) || !action[alias].is_object() || action[alias].empty())
            return;
        if (!action.contains("options") || !action["options"].is_object())
            action["options"] = nlohmann::json::object();
        for (auto it = action[alias].begin(); it != action[alias].end(); ++it) {
            if (!action["options"].contains(it.key()))
                action["options"][it.key()] = it.value();
        }
        action.erase(alias);
    };
    merge_alias("values");
    merge_alias("settings");
    merge_alias("params");
    if (action.contains("options") && action["options"].is_object() && action["options"].empty())
        action.erase("options");
}

static void prune_geometry_for_setting_only_requests(nlohmann::json& root, const std::string& user_req)
{
    if (!OllamaResponseNormalizer::has_viable_set_config(root))
        return;
    if (OllamaRequestRouter::is_geometry_request(user_req))
        return;
    if (!root.contains("actions") || !root["actions"].is_array())
        return;

    nlohmann::json kept = nlohmann::json::array();
    for (const auto& a : root["actions"]) {
        if (!a.is_object()) {
            kept.push_back(a);
            continue;
        }
        const std::string type = a.value("type", "");
        if (type == "rotate" || type == "translate" || type == "scale" || type == "arrange"
            || type == "clone_selection")
            continue;
        kept.push_back(a);
    }
    root["actions"] = std::move(kept);
}

static void prune_unsafe_actions(nlohmann::json& root, const std::string& user_req)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return;

    nlohmann::json kept = nlohmann::json::array();
    for (const auto& a : root["actions"]) {
        if (!a.is_object()) {
            kept.push_back(a);
            continue;
        }
        const std::string type = a.value("type", "");
        if (type == "delete_selection" && !user_allows_delete(user_req))
            continue;
        if (type == "menu_item")
            continue;
        if (type == "save_project")
            continue;
        if (type == "ui_select_tab" && !a.contains("tab"))
            continue;
        kept.push_back(a);
    }
    root["actions"] = std::move(kept);
    prune_geometry_for_setting_only_requests(root, user_req);
    OllamaUserFlow::prune_navigation_for_config_fixes(root, user_req);
}

} // namespace

void OllamaResponseNormalizer::inject_rule_fallbacks(nlohmann::json& /*root*/, const std::string& /*user_request*/,
                                                   OllamaRuleFallbackScope /*scope*/)
{
}

bool OllamaResponseNormalizer::has_viable_set_config(const nlohmann::json& root)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return false;
    for (const auto& action : root["actions"]) {
        if (!action.is_object() || action.value("type", "") != "set_config")
            continue;
        if (!action.contains("options") || !action["options"].is_object() || action["options"].empty())
            continue;
        const std::string preset = action.value("preset", "print");
        for (auto it = action["options"].begin(); it != action["options"].end(); ++it) {
            const std::string key = OllamaActionExecutor::normalize_config_key(it.key());
            if (OllamaSettingRegistry::is_virtual_key(key))
                return true;
            if (OllamaSettingRegistry::is_allowed_key(key, preset))
                return true;
        }
    }
    return false;
}

void OllamaResponseNormalizer::reconcile_speed_intent_actions(nlohmann::json& /*root*/, const std::string& /*user_req*/)
{
}

void OllamaResponseNormalizer::drop_redundant_slice_actions(nlohmann::json& root)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return;
    bool has_set_config = false;
    for (const auto& a : root["actions"]) {
        if (a.is_object() && a.value("type", "") == "set_config") {
            has_set_config = true;
            break;
        }
    }
    if (!has_set_config)
        return;
    nlohmann::json filtered = nlohmann::json::array();
    for (const auto& a : root["actions"]) {
        if (a.is_object() && a.value("type", "") == "slice")
            continue;
        filtered.push_back(a);
    }
    root["actions"] = std::move(filtered);
}

OllamaNormalizeResult OllamaResponseNormalizer::normalize(nlohmann::json& root, const std::string& user_req,
                                                          bool include_makerworld, bool /*force_user_intent*/)
{
    OllamaNormalizeResult result;
    if (!root.contains("actions") || !root["actions"].is_array())
        root["actions"] = nlohmann::json::array();

    const size_t actions_before = root["actions"].size();
    for (auto& a : root["actions"]) {
        if (a.is_object())
            normalize_set_config_shape(a);
    }

    if (include_makerworld && MakerWorldSearchService::is_pure_makerworld_request(user_req)) {
        bool has_makerworld = false;
        for (const auto& a : root["actions"]) {
            if (a.is_object()) {
                const std::string type = a.value("type", "");
                if (type == "makerworld_search" || type == "import_makerworld")
                    has_makerworld = true;
            }
        }
        if (!has_makerworld) {
            const std::string url = extract_first_url_from_text(user_req);
            if (!url.empty())
                root["actions"].push_back({{"type", "import_makerworld"}, {"url", url}});
            else
                root["actions"].push_back({
                    {"type", "makerworld_search"},
                    {"query", MakerWorldSearchService::normalize_search_query(user_req)},
                });
        }
    }

    if (include_makerworld && root["actions"].is_array()) {
        const std::string norm_q = MakerWorldSearchService::normalize_search_query(user_req);
        for (auto& a : root["actions"]) {
            if (!a.is_object() || a.value("type", "") != "makerworld_search")
                continue;
            const std::string existing = a.value("query", "");
            if (existing.empty() || MakerWorldSearchService::is_pure_makerworld_request(user_req))
                a["query"] = norm_q;
        }
    }

    prune_unsafe_actions(root, user_req);

    for (auto& a : root["actions"]) {
        if (!a.is_object() || a.value("type", "") != "set_config")
            continue;
        if (a.contains("preset") && a["preset"].is_string() && a["preset"].get<std::string>().empty())
            a["preset"] = "print";
        if (a.contains("options") && a["options"].is_object())
            OllamaIntentContext::refine_set_config_options(a["options"], user_req);
    }

    OllamaActionExecutor::augment_geometry_object_targets(root, user_req);
    drop_redundant_slice_actions(root);

    if (root["actions"].size() > actions_before)
        OllamaTelemetry::normalize_injected_action("actions");
    (void) result;
    return result;
}

}} // namespace

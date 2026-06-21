#include "OllamaActionCritic.hpp"

#include "OllamaRequestRouter.hpp"
#include "OllamaSettingCatalogBuilder.hpp"
#include "OllamaSettingSearch.hpp"

#include <nlohmann/json.hpp>

#include <boost/algorithm/string.hpp>
#include <unordered_set>

namespace Slic3r { namespace GUI {

namespace {

size_t count_set_config_option_keys(const nlohmann::json& root)
{
    size_t n = 0;
    if (!root.contains("actions") || !root["actions"].is_array())
        return 0;
    for (const auto& a : root["actions"]) {
        if (!a.is_object() || a.value("type", "") != "set_config" || !a.contains("options"))
            continue;
        if (a["options"].is_object())
            n += a["options"].size();
    }
    return n;
}

bool root_has_set_config(const nlohmann::json& root)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return false;
    for (const auto& a : root["actions"]) {
        if (a.is_object() && a.value("type", "") == "set_config")
            return true;
    }
    return false;
}

bool set_config_has_options(const nlohmann::json& root)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return false;
    for (const auto& a : root["actions"]) {
        if (!a.is_object() || a.value("type", "") != "set_config")
            continue;
        if (a.contains("options") && a["options"].is_object() && !a["options"].empty())
            return true;
    }
    return false;
}

std::unordered_set<std::string> keys_in_root(const nlohmann::json& root)
{
    std::unordered_set<std::string> keys;
    if (!root.contains("actions") || !root["actions"].is_array())
        return keys;
    for (const auto& a : root["actions"]) {
        if (!a.is_object() || a.value("type", "") != "set_config" || !a.contains("options"))
            continue;
        if (!a["options"].is_object())
            continue;
        for (auto it = a["options"].begin(); it != a["options"].end(); ++it)
            keys.insert(it.key());
    }
    return keys;
}

std::string wiki_blob_lower(const nlohmann::json& wiki_context)
{
    if (!wiki_context.is_array())
        return {};
    std::string blob;
    for (const auto& entry : wiki_context) {
        if (entry.is_object() && entry.contains("snippet") && entry["snippet"].is_string())
            blob += entry["snippet"].get<std::string>() + " ";
    }
    boost::algorithm::to_lower(blob);
    return blob;
}

void suggest_key_if_wiki_mentions(OllamaCriticResult& out, const std::string& wiki_lower,
                                  const char* needle, const char* key,
                                  const std::unordered_set<std::string>& existing)
{
    if (existing.count(key))
        return;
    if (wiki_lower.find(needle) == std::string::npos)
        return;
    for (const std::string& k : out.suggested_keys) {
        if (k == key)
            return;
    }
    out.suggested_keys.push_back(key);
}

} // namespace

OllamaCriticResult OllamaActionCritic::review(const nlohmann::json& root, const std::string& user_request,
                                              const nlohmann::json& wiki_context)
{
    OllamaCriticResult out;
    const bool         expects_config =
        OllamaRequestRouter::classify(user_request) != OllamaRequestRoute::Fast;
    const auto         existing_keys = keys_in_root(root);

    if (expects_config && !root_has_set_config(root)) {
        const bool has_transform = root.contains("actions") && root["actions"].is_array();
        bool       only_transform = false;
        if (has_transform) {
            only_transform = true;
            for (const auto& a : root["actions"]) {
                if (!a.is_object())
                    continue;
                const std::string t = a.value("type", "");
                if (t != "rotate" && t != "translate" && t != "scale" && t != "arrange" && t != "delete")
                    only_transform = false;
            }
        }
        if (!has_transform || only_transform) {
            out.verdict = OllamaCriticVerdict::Revise;
            out.message = "Symptom request needs set_config actions informed by wiki_context.";
            for (const std::string& k : OllamaSettingSearch::candidate_keys_for_request(user_request, 2, 6))
                out.suggested_keys.push_back(k);
            return out;
        }
    }

    if (root_has_set_config(root) && !set_config_has_options(root)) {
        out.verdict = OllamaCriticVerdict::Revise;
        out.message = "set_config actions must include non-empty options.";
        for (const std::string& k : OllamaSettingSearch::candidate_keys_for_request(user_request, 2, 6))
            out.suggested_keys.push_back(k);
        return out;
    }

    const size_t option_count = count_set_config_option_keys(root);
    if (option_count > 3) {
        out.verdict = OllamaCriticVerdict::Revise;
        out.message = "Too many settings changed; apply minimum-change (1-2 keys).";
        for (const std::string& k : OllamaSettingSearch::candidate_keys_for_request(user_request, 2, 4))
            out.suggested_keys.push_back(k);
        return out;
    }

    const std::string wiki_lower = wiki_blob_lower(wiki_context);
    if (!wiki_lower.empty()) {
        suggest_key_if_wiki_mentions(out, wiki_lower, "retraction", "retraction_length", existing_keys);
        suggest_key_if_wiki_mentions(out, wiki_lower, "nozzle temperature", "nozzle_temperature", existing_keys);
        suggest_key_if_wiki_mentions(out, wiki_lower, "brim", "brim_width", existing_keys);
        suggest_key_if_wiki_mentions(out, wiki_lower, "support", "enable_support", existing_keys);
        suggest_key_if_wiki_mentions(out, wiki_lower, "infill", "sparse_infill_density", existing_keys);
        suggest_key_if_wiki_mentions(out, wiki_lower, "elephant foot", "elefant_foot_compensation", existing_keys);
    }

    if (!out.suggested_keys.empty() && expects_config) {
        bool missing = false;
        for (const std::string& k : out.suggested_keys) {
            if (!existing_keys.count(k)) {
                missing = true;
                break;
            }
        }
        if (missing) {
            out.verdict = OllamaCriticVerdict::Revise;
            out.message = "Wiki suggests additional settings; include them in set_config if appropriate.";
            return out;
        }
    }

    out.verdict = OllamaCriticVerdict::Approve;
    return out;
}

}} // namespace

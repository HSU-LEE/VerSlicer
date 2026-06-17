#include "AICoachApplyDedup.hpp"

#include "../OllamaAssistant/OllamaActionExecutor.hpp"

#include <algorithm>
#include <vector>

namespace Slic3r { namespace GUI {

AICoachApplyDedup& AICoachApplyDedup::instance()
{
    static AICoachApplyDedup s;
    return s;
}

std::string AICoachApplyDedup::fingerprint_set_config(const nlohmann::json& action)
{
    if (!action.is_object() || action.value("type", "") != "set_config")
        return {};
    if (!action.contains("options") || !action["options"].is_object())
        return {};

    nlohmann::json options = action["options"];
    OllamaActionExecutor::normalize_set_config_options(options);

    const std::string preset = action.value("preset", "print");
    std::vector<std::string> keys;
    for (auto it = options.begin(); it != options.end(); ++it)
        keys.push_back(it.key());
    std::sort(keys.begin(), keys.end());

    std::string fp = preset + "|";
    for (const std::string& key : keys) {
        fp += key + "=";
        const nlohmann::json& val = options[key];
        if (val.is_string())
            fp += val.get<std::string>();
        else if (val.is_boolean())
            fp += val.get<bool>() ? "1" : "0";
        else
            fp += val.dump();
        fp += ";";
    }
    return fp;
}

void AICoachApplyDedup::collect_fingerprints(const nlohmann::json& root, std::unordered_set<std::string>& out)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return;
    for (const auto& action : root["actions"]) {
        if (const std::string fp = fingerprint_set_config(action); !fp.empty())
            out.insert(fp);
    }
}

void AICoachApplyDedup::record_applied_root(const nlohmann::json& root, const char* /*source*/)
{
    collect_fingerprints(root, m_applied_fingerprints);
}

bool AICoachApplyDedup::card_is_redundant(const AICoachCard& card) const
{
    if (card.apply_root.empty())
        return false;

    std::unordered_set<std::string> proposed;
    collect_fingerprints(card.apply_root, proposed);
    if (proposed.empty())
        return false;

    for (const std::string& fp : proposed) {
        if (m_applied_fingerprints.find(fp) == m_applied_fingerprints.end())
            return false;
    }
    return true;
}

void AICoachApplyDedup::clear()
{
    m_applied_fingerprints.clear();
}

}} // namespace

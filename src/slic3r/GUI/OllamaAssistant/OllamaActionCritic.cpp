#include "OllamaActionCritic.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI {

namespace {

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

} // namespace

OllamaCriticResult OllamaActionCritic::review(const nlohmann::json& root, const std::string& user_request,
                                              const nlohmann::json& wiki_context)
{
    (void) user_request;
    (void) wiki_context;
    OllamaCriticResult out;

    if (root_has_set_config(root) && !set_config_has_options(root)) {
        out.verdict = OllamaCriticVerdict::Revise;
        out.message = "set_config actions must include non-empty options.";
        return out;
    }

    out.verdict = OllamaCriticVerdict::Approve;
    return out;
}

}} // namespace

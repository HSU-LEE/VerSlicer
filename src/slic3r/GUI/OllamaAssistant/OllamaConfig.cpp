#include "OllamaConfig.hpp"

#include "../GUI_App.hpp"

#include <boost/algorithm/string.hpp>

#include <wx/app.h>

#include <cstdlib>

namespace Slic3r { namespace GUI {

namespace {

bool env_flag(const char* name, bool default_value)
{
    const char* v = std::getenv(name);
    if (!v || !*v)
        return default_value;
    const std::string s(v);
    return s == "1" || s == "true" || s == "yes" || s == "on";
}

bool config_flag(const char* key, bool default_value)
{
    if (!wxTheApp)
        return default_value;
    if (wxGetApp().app_config) {
        const std::string v = wxGetApp().app_config->get(kOllamaConfigSection, key);
        if (!v.empty())
            return v == "1" || v == "true" || v == "yes" || v == "on";
    }
    return default_value;
}

bool pipeline_flag(const char* config_key, const char* env_key, bool default_value)
{
    const char* v = std::getenv(env_key);
    if (v && *v)
        return env_flag(env_key, default_value);
    return config_flag(config_key, default_value);
}

} // namespace

std::string normalize_ollama_model_tag(std::string model)
{
    boost::trim(model);
    if (model.empty())
        model = kOllamaDefaultModel;
    if (model == "llama3.2")
        model = "llama3.2:latest";
    return model;
}

std::string ollama_host_from_config()
{
    if (wxTheApp && wxGetApp().app_config) {
        const std::string host = wxGetApp().app_config->get(kOllamaConfigSection, kOllamaHostKey);
        if (!host.empty())
            return host;
    }
    return kOllamaDefaultHost;
}

std::string ollama_model_from_config()
{
    if (wxTheApp && wxGetApp().app_config) {
        const std::string model = wxGetApp().app_config->get(kOllamaConfigSection, kOllamaModelKey);
        if (!model.empty())
            return normalize_ollama_model_tag(model);
    }
    return kOllamaDefaultModel;
}

bool ollama_auto_catalog_enabled()
{
    return pipeline_flag(kOllamaAutoCatalogKey, "OLLAMA_AUTO_CATALOG", true);
}

bool ollama_two_hop_enabled()
{
    return pipeline_flag(kOllamaTwoHopKey, "OLLAMA_TWO_HOP", true);
}

bool ollama_keyword_inject_enabled()
{
    return pipeline_flag(kOllamaKeywordInjectKey, "OLLAMA_KEYWORD_INJECT", false);
}

bool ollama_rule_only_fallback_enabled()
{
    if (!ollama_keyword_inject_enabled())
        return false;
    return pipeline_flag(kOllamaRuleOnlyKey, "OLLAMA_RULE_ONLY", true);
}

bool ollama_adaptive_routing_enabled()
{
    return pipeline_flag(kOllamaAdaptiveRouteKey, "OLLAMA_ADAPTIVE_ROUTING", true);
}

bool ollama_wiki_search_enabled()
{
    return pipeline_flag(kOllamaWikiSearchKey, "OLLAMA_WIKI_SEARCH", true);
}

bool ollama_critic_enabled()
{
    return pipeline_flag(kOllamaCriticKey, "OLLAMA_CRITIC", true);
}

}} // namespace

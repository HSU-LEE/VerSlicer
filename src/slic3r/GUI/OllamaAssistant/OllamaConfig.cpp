#include "OllamaConfig.hpp"

#ifndef OLLAMA_HEADLESS_TEST
#include "../GUI_App.hpp"
#include <wx/app.h>
#endif

#include <boost/algorithm/string.hpp>

#include <cstdlib>
#include <algorithm>

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
#ifndef OLLAMA_HEADLESS_TEST
    if (!wxTheApp)
        return default_value;
    if (wxGetApp().app_config) {
        const std::string v = wxGetApp().app_config->get(kOllamaConfigSection, key);
        if (!v.empty())
            return v == "1" || v == "true" || v == "yes" || v == "on";
    }
#endif
    (void) key;
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
    if (model == "qwen2.5")
        model = "qwen2.5:7b";
    return model;
}

std::string ollama_host_from_config()
{
#ifndef OLLAMA_HEADLESS_TEST
    if (wxTheApp && wxGetApp().app_config) {
        const std::string host = wxGetApp().app_config->get(kOllamaConfigSection, kOllamaHostKey);
        if (!host.empty())
            return host;
    }
#endif
    return kOllamaDefaultHost;
}

std::string ollama_model_from_config()
{
#ifndef OLLAMA_HEADLESS_TEST
    if (wxTheApp && wxGetApp().app_config) {
        const std::string model = wxGetApp().app_config->get(kOllamaConfigSection, kOllamaModelKey);
        if (!model.empty())
            return normalize_ollama_model_tag(model);
    }
#endif
    return kOllamaDefaultModel;
}

bool ollama_auto_catalog_enabled()
{
    return pipeline_flag(kOllamaAutoCatalogKey, "OLLAMA_AUTO_CATALOG", true);
}

bool ollama_two_hop_enabled()
{
    return pipeline_flag(kOllamaTwoHopKey, "OLLAMA_TWO_HOP", false);
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
    return pipeline_flag(kOllamaCriticKey, "OLLAMA_CRITIC", false);
}

int config_int(const char* key, int default_value)
{
#ifndef OLLAMA_HEADLESS_TEST
    if (wxTheApp && wxGetApp().app_config) {
        const std::string v = wxGetApp().app_config->get(kOllamaConfigSection, key);
        if (!v.empty()) {
            try {
                return std::stoi(v);
            } catch (...) {
            }
        }
    }
#endif
    (void) key;
    return default_value;
}

bool ollama_assist_loop_enabled()
{
    return pipeline_flag(kOllamaAssistLoopKey, "OLLAMA_ASSIST_LOOP", true);
}

int ollama_assist_max_steps()
{
    const char* v = std::getenv("OLLAMA_ASSIST_MAX_STEPS");
    if (v && *v) {
        try {
            return std::max(1, std::stoi(v));
        } catch (...) {
        }
    }
    return std::max(1, config_int(kOllamaAssistMaxStepsKey, 12));
}

}} // namespace

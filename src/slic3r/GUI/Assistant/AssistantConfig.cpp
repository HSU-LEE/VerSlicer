#include "AssistantConfig.hpp"

#ifndef ASSISTANT_HEADLESS_TEST
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
#ifndef ASSISTANT_HEADLESS_TEST
    if (!wxTheApp)
        return default_value;
    if (wxGetApp().app_config) {
        const std::string v = wxGetApp().app_config->get(kAssistantConfigSection, key);
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

std::string normalize_assistant_model_tag(std::string model)
{
    boost::trim(model);
    if (model == "qwen2.5")
        model = "qwen2.5:7b";
    return model;
}

std::string assistant_host_from_config()
{
#ifndef ASSISTANT_HEADLESS_TEST
    if (wxTheApp && wxGetApp().app_config) {
        const std::string host = wxGetApp().app_config->get(kAssistantConfigSection, kAssistantHostKey);
        if (!host.empty())
            return host;
    }
#endif
    return "chatgpt";
}

std::string assistant_model_from_config()
{
#ifndef ASSISTANT_HEADLESS_TEST
    if (wxTheApp && wxGetApp().app_config) {
        const std::string model = wxGetApp().app_config->get(kAssistantConfigSection, kAssistantModelKey);
        if (!model.empty())
            return normalize_assistant_model_tag(model);
    }
#endif
    return kAssistantDefaultModel;
}

bool assistant_auto_catalog_enabled()
{
    return pipeline_flag(kAssistantAutoCatalogKey, "OLLAMA_AUTO_CATALOG", true);
}

bool assistant_wiki_search_enabled()
{
    return pipeline_flag(kAssistantWikiSearchKey, "OLLAMA_WIKI_SEARCH", true);
}

bool assistant_mcp_server_enabled()
{
    return pipeline_flag(kAssistantMcpServerKey, "VERSLICER_MCP_SERVER", false);
}

int config_int(const char* key, int default_value)
{
#ifndef ASSISTANT_HEADLESS_TEST
    if (wxTheApp && wxGetApp().app_config) {
        const std::string v = wxGetApp().app_config->get(kAssistantConfigSection, key);
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

bool assistant_assist_loop_enabled()
{
    return pipeline_flag(kAssistLoopKey, "OLLAMA_ASSIST_LOOP", true);
}

int assistant_assist_max_steps()
{
    const char* v = std::getenv("OLLAMA_ASSIST_MAX_STEPS");
    if (v && *v) {
        try {
            return std::max(1, std::stoi(v));
        } catch (...) {
        }
    }
    return std::max(1, config_int(kAssistMaxStepsKey, 40));
}

}} // namespace

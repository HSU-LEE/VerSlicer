#include "OllamaConfig.hpp"

#include "../GUI_App.hpp"

#include <boost/algorithm/string.hpp>

namespace Slic3r { namespace GUI {

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
    if (wxGetApp().app_config) {
        const std::string host = wxGetApp().app_config->get(kOllamaConfigSection, kOllamaHostKey);
        if (!host.empty())
            return host;
    }
    return kOllamaDefaultHost;
}

std::string ollama_model_from_config()
{
    if (wxGetApp().app_config) {
        const std::string model = wxGetApp().app_config->get(kOllamaConfigSection, kOllamaModelKey);
        if (!model.empty())
            return normalize_ollama_model_tag(model);
    }
    return kOllamaDefaultModel;
}

}} // namespace

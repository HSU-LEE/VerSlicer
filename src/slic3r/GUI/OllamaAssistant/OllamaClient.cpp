#include "OllamaClient.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/Http.hpp"

#include <boost/algorithm/string.hpp>

namespace Slic3r { namespace GUI {

namespace {

std::string trim_trailing_slash(std::string url)
{
    while (!url.empty() && url.back() == '/')
        url.pop_back();
    return url;
}

} // namespace

OllamaClient::OllamaClient(std::string base_url)
    : m_base_url(trim_trailing_slash(std::move(base_url)))
{}

void OllamaClient::chat(const std::string& model, const std::vector<OllamaMessage>& messages, ChatCallback callback)
{
    if (!callback)
        return;

    nlohmann::json body;
    body["model"]   = model.empty() ? "llama3.2" : model;
    body["stream"]  = false;
    // Ask Ollama to return valid JSON (reduces "plain text" failures).
    body["format"]  = "json";
    // Lower temperature improves setting/action accuracy.
    body["options"] = {{"temperature", 0.15}, {"top_p", 0.9}};
    body["messages"] = nlohmann::json::array();
    for (const OllamaMessage& msg : messages) {
        body["messages"].push_back({{"role", msg.role}, {"content", msg.content}});
    }

    const std::string url = m_base_url + "/api/chat";
    Http::post(url)
        .header("Content-Type", "application/json")
        .timeout_connect(5)
        .timeout_max(180)
        .set_post_body(body.dump())
        .on_error([callback](std::string, std::string error, unsigned) {
            wxGetApp().CallAfter([callback, error = std::move(error)]() {
                if (wxGetApp().is_closing())
                    return;
                callback({}, error);
            });
        })
        .on_complete([callback](std::string response, unsigned http_status) {
            wxGetApp().CallAfter([callback, response = std::move(response), http_status]() {
                if (wxGetApp().is_closing())
                    return;
                if (http_status < 200 || http_status >= 300) {
                    callback({}, "Ollama HTTP " + std::to_string(http_status));
                    return;
                }
                try {
                    const nlohmann::json j = nlohmann::json::parse(response);
                    if (j.contains("message") && j["message"].is_object() && j["message"].contains("content") &&
                        j["message"]["content"].is_string())
                        callback(j["message"]["content"].get<std::string>(), {});
                    else if (j.contains("error")) {
                        std::string err = j["error"].is_string() ? j["error"].get<std::string>() : j["error"].dump();
                        callback({}, err);
                    } else
                        callback({}, "Unexpected Ollama response");
                } catch (const std::exception& e) {
                    callback({}, std::string("Parse error: ") + e.what());
                }
            });
        })
        .perform();
}

void OllamaClient::list_models(ModelsCallback callback)
{
    if (!callback)
        return;

    const std::string url = m_base_url + "/api/tags";
    Http::get(url)
        .header("Content-Type", "application/json")
        .timeout_connect(3)
        .timeout_max(15)
        .on_error([callback](std::string, std::string error, unsigned) {
            wxGetApp().CallAfter([callback, error = std::move(error)]() {
                if (wxGetApp().is_closing())
                    return;
                callback({}, error);
            });
        })
        .on_complete([callback](std::string response, unsigned http_status) {
            wxGetApp().CallAfter([callback, response = std::move(response), http_status]() {
                if (wxGetApp().is_closing())
                    return;
                if (http_status < 200 || http_status >= 300) {
                    callback({}, "Ollama HTTP " + std::to_string(http_status));
                    return;
                }
                try {
                    const nlohmann::json j    = nlohmann::json::parse(response);
                    std::vector<std::string> names;
                    if (j.contains("models") && j["models"].is_array()) {
                        for (const auto& m : j["models"]) {
                            if (m.contains("name") && m["name"].is_string())
                                names.push_back(m["name"].get<std::string>());
                        }
                    }
                    callback(names, {});
                } catch (const std::exception& e) {
                    callback({}, std::string("Parse error: ") + e.what());
                }
            });
        })
        .perform();
}

}} // namespace

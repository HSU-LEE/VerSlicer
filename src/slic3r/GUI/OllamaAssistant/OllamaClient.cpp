#include "OllamaClient.hpp"
#include "OllamaActionExecutor.hpp"
#include "OllamaConfig.hpp"
#include "OllamaIntentContext.hpp"
#include "OllamaModelPick.hpp"
#include "OllamaSettingRegistry.hpp"
#include "OllamaTelemetry.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/Http.hpp"

#include <boost/algorithm/string.hpp>

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace Slic3r { namespace GUI {

namespace {

std::recursive_mutex& ollama_chat_mutex()
{
    static std::recursive_mutex m;
    return m;
}

struct ActiveRequestState
{
    std::mutex                         mutex;
    std::shared_ptr<std::atomic<bool>> cancel_flag;
    Http*                              http{nullptr};
};

ActiveRequestState& active_request_state(OllamaRequestKind kind)
{
    static ActiveRequestState chat;
    static ActiveRequestState advisor;
    static ActiveRequestState planner;
    static ActiveRequestState resolver;
    switch (kind) {
    case OllamaRequestKind::Advisor: return advisor;
    case OllamaRequestKind::Planner: return planner;
    case OllamaRequestKind::Resolver: return resolver;
    default: return chat;
    }
}

static void cancel_request_state(ActiveRequestState& state)
{
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.cancel_flag)
        state.cancel_flag->store(true);
    if (state.http)
        state.http->cancel();
}

struct ModelListCache
{
    std::mutex                            mutex;
    std::string                           base_url;
    std::vector<std::string>              models;
    std::string                           error;
    std::chrono::steady_clock::time_point fetched{};
    static constexpr int                  ttl_success_sec = 60;
    static constexpr int                  ttl_error_sec   = 5;
};

ModelListCache& model_list_cache()
{
    static ModelListCache cache;
    return cache;
}

std::atomic<uint64_t>& request_serial(OllamaRequestKind kind)
{
    static std::atomic<uint64_t> chat{0};
    static std::atomic<uint64_t> advisor{0};
    static std::atomic<uint64_t> planner{0};
    static std::atomic<uint64_t> resolver{0};
    switch (kind) {
    case OllamaRequestKind::Advisor: return advisor;
    case OllamaRequestKind::Planner: return planner;
    case OllamaRequestKind::Resolver: return resolver;
    default: return chat;
    }
}

std::string trim_trailing_slash(std::string url)
{
    while (!url.empty() && url.back() == '/')
        url.pop_back();
    return url;
}

std::string normalize_model_tag(std::string model)
{
    boost::trim(model);
    if (model.empty())
        model = kOllamaDefaultModel;
    if (model == "qwen2.5")
        model = "qwen2.5:3b";
    if (model == "qwen2.5:7b")
        model = "qwen2.5:3b";
    return model;
}

bool is_effectively_empty(const std::string& s)
{
    std::string t = s;
    boost::trim(t);
    return t.empty();
}

std::string extract_chat_content(const nlohmann::json& j)
{
    if (j.contains("message") && j["message"].is_object()) {
        const auto& msg = j["message"];
        if (msg.contains("content")) {
            if (msg["content"].is_string()) {
                const std::string content = msg["content"].get<std::string>();
                if (!is_effectively_empty(content))
                    return content;
            } else if (!msg["content"].is_null())
                return msg["content"].dump();
        }
        if (msg.contains("thinking") && msg["thinking"].is_string()) {
            const std::string thinking = msg["thinking"].get<std::string>();
            if (!is_effectively_empty(thinking))
                return thinking;
        }
    }

    if (j.contains("response") && j["response"].is_string()) {
        const std::string response = j["response"].get<std::string>();
        if (!is_effectively_empty(response))
            return response;
    }

    return {};
}

std::string summarize_empty_reply(const nlohmann::json& j, const std::string& raw)
{
    std::string diag = "Empty Ollama reply";
    if (j.contains("done_reason") && j["done_reason"].is_string())
        diag += " (done_reason=" + j["done_reason"].get<std::string>() + ")";
    if (j.contains("eval_count") && j["eval_count"].is_number())
        diag += " eval_count=" + std::to_string(j["eval_count"].get<int>());
    if (!raw.empty()) {
        const size_t n = std::min<size_t>(raw.size(), 180);
        diag += " raw=" + raw.substr(0, n);
        if (raw.size() > n)
            diag += "…";
    }
    return diag;
}

static std::string inject_priority_catalog_slice(std::string user_content)
{
    const std::string marker = "\n\nUser request:\n";
    const auto        pos    = user_content.rfind(marker);
    if (pos == std::string::npos)
        return user_content;

    std::string context = user_content.substr(0, pos);
    std::string request = user_content.substr(pos + marker.size());
    try {
        nlohmann::json ctx = nlohmann::json::parse(context);
        ctx["setting_catalog"] = OllamaSettingRegistry::build_priority_catalog(nullptr, false, 5);
        const nlohmann::json cached = OllamaIntentContext::cached_intent_signals_json();
        if (!cached.empty())
            ctx["intent_signals"] = cached;
        context                = ctx.dump(2);
    } catch (...) {
    }
    return context + marker + request;
}

std::vector<OllamaMessage> slim_messages_for_retry(const std::vector<OllamaMessage>& messages)
{
    std::vector<OllamaMessage> out;
    out.reserve(8);

    if (!messages.empty() && messages.front().role == "system")
        out.push_back(messages.front());

    const size_t keep_tail = 4;
    const size_t start     = messages.size() > keep_tail ? (messages.size() - keep_tail) : 0;
    for (size_t i = start; i < messages.size(); ++i) {
        if (i == 0 && !out.empty() && messages.front().role == "system")
            continue;
        OllamaMessage msg = messages[i];
        if (msg.role == "user")
            msg.content = inject_priority_catalog_slice(msg.content);
        out.push_back(std::move(msg));
    }
    return out;
}

std::vector<OllamaMessage> minimal_messages_for_retry(const std::vector<OllamaMessage>& messages)
{
    std::vector<OllamaMessage> out;
    if (!messages.empty() && messages.front().role == "system")
        out.push_back(messages.front());
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->role == "user") {
            std::string user = inject_priority_catalog_slice(it->content);
            const std::string marker = "\n\nUser request:\n";
            const auto pos = user.rfind(marker);
            if (pos != std::string::npos)
                user = user.substr(pos + marker.size());
            out.push_back({"user", user});
            break;
        }
    }
    return out;
}

struct ChatAttemptResult
{
    std::string content;
    std::string error;
    int         retry_tier{0};
    unsigned    http_status{0};
    std::string resolved_model;
};

std::vector<std::string> list_models_sync_impl(const std::string& base_url, std::string* error_out = nullptr)
{
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(model_list_cache().mutex);
        auto&                       cache = model_list_cache();
        if (cache.base_url == base_url && cache.fetched != std::chrono::steady_clock::time_point{}) {
            const int age_sec = static_cast<int>(
                std::chrono::duration_cast<std::chrono::seconds>(now - cache.fetched).count());
            const int ttl = cache.error.empty() ? ModelListCache::ttl_success_sec : ModelListCache::ttl_error_sec;
            if (age_sec < ttl) {
                if (error_out)
                    *error_out = cache.error;
                return cache.models;
            }
        }
    }

    std::vector<std::string> names;
    const std::string        url = trim_trailing_slash(base_url) + "/api/tags";

    std::string response;
    std::string http_error;
    unsigned    http_status = 0;

    Http::get(url)
        .header("Content-Type", "application/json")
        .timeout_connect(5)
        .timeout_max(15)
        .on_error([&](std::string body, std::string error, unsigned status) {
            response    = std::move(body);
            http_error  = std::move(error);
            http_status = status;
        })
        .on_complete([&](std::string body, unsigned status) {
            response    = std::move(body);
            http_status = status;
        })
        .perform_sync();

    std::string err;
    if (!http_error.empty())
        err = http_error;
    else if (http_status < 200 || http_status >= 300)
        err = "Ollama HTTP " + std::to_string(http_status);
    else if (response.empty())
        err = "Empty model list from Ollama";

    if (err.empty()) {
        try {
            const nlohmann::json j = nlohmann::json::parse(response);
            if (j.contains("models") && j["models"].is_array()) {
                for (const auto& m : j["models"]) {
                    if (m.contains("name") && m["name"].is_string())
                        names.push_back(m["name"].get<std::string>());
                }
            }
        } catch (const std::exception& e) {
            err = std::string("Parse error: ") + e.what();
        }
    }

    {
        std::lock_guard<std::mutex> lock(model_list_cache().mutex);
        auto&                       cache = model_list_cache();
        cache.base_url                    = base_url;
        cache.models                      = names;
        cache.error                       = err;
        cache.fetched                     = now;
    }
    if (error_out)
        *error_out = err;
    return names;
}

ChatAttemptResult run_chat_http(const std::string& url, const std::string& model,
                                const std::vector<OllamaMessage>& messages,
                                const std::shared_ptr<std::atomic<bool>>& cancel_flag, int retry_tier,
                                OllamaRequestKind kind)
{
    ChatAttemptResult out;
    out.retry_tier = retry_tier;

    nlohmann::json body;
    body["model"]    = model;
    body["stream"]     = false;
    body["keep_alive"] = kOllamaKeepAlive;
    body["options"]    = {{"temperature", 0.2},
                          {"top_p", 0.9},
                          {"num_predict", kOllamaNumPredict},
                          {"num_ctx", kOllamaNumCtx}};
    body["messages"] = nlohmann::json::array();
    for (const OllamaMessage& msg : messages)
        body["messages"].push_back({{"role", msg.role}, {"content", msg.content}});

    const std::string post_body = body.dump();

    std::string response;
    std::string http_error;
    unsigned    http_status = 0;

    Http http = Http::post(url);
    ActiveRequestState& active = active_request_state(kind);
    {
        std::lock_guard<std::mutex> lock(active.mutex);
        active.http = &http;
    }
    http.header("Content-Type", "application/json")
        .timeout_connect(30)
        .timeout_max(600)
        .set_post_body(post_body)
        .on_progress([&](Http::Progress, bool& cancel) {
            if (cancel_flag && cancel_flag->load())
                cancel = true;
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            response    = std::move(body);
            http_error  = std::move(error);
            http_status = status;
        })
        .on_complete([&](std::string body, unsigned status) {
            response    = std::move(body);
            http_status = status;
        })
        .perform_sync();
    {
        std::lock_guard<std::mutex> lock(active.mutex);
        if (active.http == &http)
            active.http = nullptr;
    }

    if (cancel_flag && cancel_flag->load())
        return { {}, "Request cancelled", 0, 0, model };

    if (!http_error.empty()) {
        if (http_error.find("Connection refused") != std::string::npos)
            return { {}, "Cannot connect to Ollama. Start it with: ollama serve", 0, http_status, model };
        return { {}, http_error, 0, http_status, model };
    }
    if (http_status < 200 || http_status >= 300) {
        std::string err = "Ollama HTTP " + std::to_string(http_status) + " (model=" + model + ")";
        if (http_status == 404)
            err += " — model not installed. Run: ollama pull " + model;
        if (!response.empty()) {
            const size_t n = std::min<size_t>(response.size(), 120);
            err += " body=" + response.substr(0, n);
        }
        return { {}, err, 0, http_status, model };
    }
    if (response.empty())
        return { {}, "Empty HTTP body from Ollama", 0, http_status, model };

    try {
        const nlohmann::json j = nlohmann::json::parse(response);

        if (j.contains("error")) {
            out.error          = j["error"].is_string() ? j["error"].get<std::string>() : j["error"].dump();
            out.http_status    = http_status;
            out.resolved_model = model;
            return out;
        }

        out.content        = extract_chat_content(j);
        out.http_status    = http_status;
        out.resolved_model = model;
        if (!is_effectively_empty(out.content))
            return out;

        out.error = summarize_empty_reply(j, response);
    } catch (const std::exception& e) {
        out.error          = std::string("Parse error: ") + e.what();
        out.http_status    = http_status;
        out.resolved_model = model;
    }

    return out;
}

} // namespace

OllamaClient::OllamaClient(std::string base_url)
    : m_base_url(trim_trailing_slash(std::move(base_url)))
{}

void OllamaClient::cancel_active_requests(OllamaCancelDomain domain)
{
    if (domain == OllamaCancelDomain::Chat || domain == OllamaCancelDomain::All) {
        cancel_request_state(active_request_state(OllamaRequestKind::Chat));
        cancel_request_state(active_request_state(OllamaRequestKind::Planner));
        cancel_request_state(active_request_state(OllamaRequestKind::Resolver));
    }
    if (domain == OllamaCancelDomain::Planner || domain == OllamaCancelDomain::All)
        cancel_request_state(active_request_state(OllamaRequestKind::Planner));
    if (domain == OllamaCancelDomain::Resolver || domain == OllamaCancelDomain::All)
        cancel_request_state(active_request_state(OllamaRequestKind::Resolver));
    if (domain == OllamaCancelDomain::Advisor || domain == OllamaCancelDomain::All)
        cancel_request_state(active_request_state(OllamaRequestKind::Advisor));
}

void OllamaClient::chat(const std::string& model, const std::vector<OllamaMessage>& messages, ChatCallback callback,
                        OllamaRequestKind kind)
{
    if (!callback)
        return;

    const OllamaCancelDomain cancel_domain = [&]() {
        switch (kind) {
        case OllamaRequestKind::Advisor: return OllamaCancelDomain::Advisor;
        case OllamaRequestKind::Planner: return OllamaCancelDomain::Planner;
        case OllamaRequestKind::Resolver: return OllamaCancelDomain::Resolver;
        default: return OllamaCancelDomain::Chat;
        }
    }();
    cancel_active_requests(cancel_domain);

    const uint64_t tok = ++request_serial(kind);

    struct Job {
        ChatCallback                       callback;
        std::string                        base_url;
        std::string                        url;
        std::string                        model;
        std::vector<OllamaMessage>         messages;
        std::shared_ptr<std::atomic<bool>> cancel_flag;
        OllamaRequestKind                  kind;
    };

    auto job         = std::make_shared<Job>();
    job->callback    = std::move(callback);
    job->base_url    = m_base_url;
    job->url         = m_base_url + "/api/chat";
    job->model       = normalize_model_tag(model);
    job->messages    = messages;
    job->cancel_flag = std::make_shared<std::atomic<bool>>(false);
    job->kind        = kind;
    {
        std::lock_guard<std::mutex> lock(active_request_state(kind).mutex);
        active_request_state(kind).cancel_flag = job->cancel_flag;
    }

    OllamaTelemetry::chat_started(job->model);
    const auto started = std::chrono::steady_clock::now();

    std::thread([job, tok, kind, started]() {
        ChatAttemptResult result;
        int               retry_tier = 0;

        {
            std::lock_guard<std::recursive_mutex> lock(ollama_chat_mutex());

            std::string model = job->model;
            std::string list_err;
            const auto  installed = list_models_sync_impl(job->base_url, &list_err);
            if (!installed.empty())
                model = pick_installed_ollama_model(installed, model);

            result = run_chat_http(job->url, model, job->messages, job->cancel_flag, retry_tier, job->kind);

            if (result.content.empty() && result.error.find("HTTP 404") != std::string::npos && !installed.empty()) {
                model  = pick_installed_ollama_model(installed, model);
                result = run_chat_http(job->url, model, job->messages, job->cancel_flag, retry_tier, job->kind);
            }

            if (result.content.empty() && result.error.find("HTTP 404") == std::string::npos
                && result.error != "Request cancelled") {
                if (!job->messages.empty()) {
                    retry_tier = 1;
                    result     = run_chat_http(job->url, model, slim_messages_for_retry(job->messages), job->cancel_flag,
                                           retry_tier, job->kind);
                }
                if (result.content.empty()) {
                    retry_tier = 2;
                    result     = run_chat_http(job->url, model, minimal_messages_for_retry(job->messages),
                                           job->cancel_flag, retry_tier, job->kind);
                }
            }
            result.retry_tier = retry_tier;
        }

        const int latency_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());

        wxGetApp().CallAfter([job, tok, kind, result = std::move(result), latency_ms]() {
            if (tok != request_serial(kind).load() || wxGetApp().is_closing())
                return;
            const bool ok = result.error.empty() && !result.content.empty();
            OllamaTelemetry::ChatFinishedInfo info;
            info.error          = result.error;
            info.http_status    = result.http_status;
            info.resolved_model = result.resolved_model.empty() ? job->model : result.resolved_model;
            info.host           = job->base_url;
            info.kind           = job->kind == OllamaRequestKind::Advisor ? "advisor" : "chat";
            OllamaTelemetry::chat_finished(ok, latency_ms, result.retry_tier, info);
            job->callback(result.content, result.error);
        });
    }).detach();
}

std::vector<std::string> OllamaClient::list_models_sync() const
{
    return list_models_sync_impl(m_base_url);
}

void OllamaClient::list_models(ModelsCallback callback)
{
    if (!callback)
        return;

    const std::string url      = m_base_url + "/api/tags";
    const std::string base_url = m_base_url;
    Http::get(url)
        .header("Content-Type", "application/json")
        .timeout_connect(3)
        .timeout_max(15)
        .on_error([callback, base_url](std::string, std::string error, unsigned) {
            wxGetApp().CallAfter([callback, error = std::move(error), base_url]() {
                if (wxGetApp().is_closing())
                    return;
                std::string cached_err;
                const auto  names = list_models_sync_impl(base_url, &cached_err);
                if (!names.empty())
                    callback(names, {});
                else
                    callback({}, error.empty() ? cached_err : error);
            });
        })
        .on_complete([callback, base_url](std::string response, unsigned http_status) {
            wxGetApp().CallAfter([callback, response = std::move(response), http_status, base_url]() {
                if (wxGetApp().is_closing())
                    return;
                if (http_status < 200 || http_status >= 300) {
                    callback({}, "Ollama HTTP " + std::to_string(http_status));
                    return;
                }
                try {
                    const nlohmann::json       j = nlohmann::json::parse(response);
                    std::vector<std::string> names;
                    if (j.contains("models") && j["models"].is_array()) {
                        for (const auto& m : j["models"]) {
                            if (m.contains("name") && m["name"].is_string())
                                names.push_back(m["name"].get<std::string>());
                        }
                    }
                    {
                        std::lock_guard<std::mutex> lock(model_list_cache().mutex);
                        auto&                       cache = model_list_cache();
                        cache.base_url                    = base_url;
                        cache.models                      = names;
                        cache.error.clear();
                        cache.fetched                     = std::chrono::steady_clock::now();
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

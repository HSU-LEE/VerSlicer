#include "OllamaClient.hpp"

#include "ChatGptAccount.hpp"

#include <boost/log/trivial.hpp>
#include "OllamaActionExecutor.hpp"
#include "OllamaToolGateway.hpp"
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
#include <sstream>
#include <utility>

namespace Slic3r { namespace GUI {

namespace {

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
        model = kOllamaDefaultModel;
    return model;
}

struct ChatAttemptResult
{
    std::string content;
    std::string error;
    int         retry_tier{0};
    unsigned    http_status{0};
    std::string resolved_model;
};

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
                        OllamaRequestKind kind, StreamCallback stream_cb, std::string tools_json)
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
        StreamCallback                     stream_cb;
        std::string                        base_url;
        std::string                        url;
        std::string                        model;
        std::vector<OllamaMessage>         messages;
        std::string                        tools_json;
        std::shared_ptr<std::atomic<bool>> cancel_flag;
        OllamaRequestKind                  kind;
    };

    auto job         = std::make_shared<Job>();
    job->callback    = std::move(callback);
    job->stream_cb   = std::move(stream_cb);
    job->base_url    = m_base_url;
    job->url         = m_base_url + "/api/chat";
    job->model       = normalize_model_tag(model);
    job->messages    = messages;
    job->tools_json  = std::move(tools_json);
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
        std::vector<ChatGptTurn> turns;
        turns.reserve(job->messages.size());
        for (const OllamaMessage& message : job->messages)
            turns.push_back({message.role, message.content});

        const ChatGptCompletion completion = ChatGptAccount::complete(
            job->model, turns, job->tools_json, job->cancel_flag, job->stream_cb,
            [kind](Http* http) {
                std::lock_guard<std::mutex> lock(active_request_state(kind).mutex);
                active_request_state(kind).http = http;
            });
        result.content        = completion.text;
        result.error          = completion.error;
        result.http_status    = completion.http_status;
        result.resolved_model = completion.model.empty() ? job->model : completion.model;
        result.retry_tier     = 0;

        const int latency_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());

        if (wxGetApp().is_closing())
            return; // shutting down: never post to a dying main loop
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

void OllamaClient::list_models(ModelsCallback callback)
{
    if (!callback)
        return;

    std::thread([callback]() {
        std::string error;
        const std::vector<std::string> names = ChatGptAccount::list_models(&error);
        wxGetApp().CallAfter([callback, names, error]() {
            if (wxGetApp().is_closing())
                return;
            callback(names, error);
        });
    }).detach();
}

}} // namespace

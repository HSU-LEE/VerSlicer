#include "AssistantClient.hpp"

#include "../UiDispatch.hpp"
#include "ChatGptAccount.hpp"

#include <boost/log/trivial.hpp>
#include "AssistantActionExecutor.hpp"
#include "AssistantToolGateway.hpp"
#include "AssistantConfig.hpp"
#include "AssistantIntentContext.hpp"
#include "AssistantModelPick.hpp"
#include "AssistantSettingRegistry.hpp"
#include "AssistantTelemetry.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/Http.hpp"

#include <boost/algorithm/string.hpp>

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
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

ActiveRequestState& active_request_state(AssistantRequestKind kind)
{
    static ActiveRequestState chat;
    static ActiveRequestState advisor;
    static ActiveRequestState planner;
    static ActiveRequestState resolver;
    switch (kind) {
    case AssistantRequestKind::Advisor: return advisor;
    case AssistantRequestKind::Planner: return planner;
    case AssistantRequestKind::Resolver: return resolver;
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

std::atomic<uint64_t>& request_serial(AssistantRequestKind kind)
{
    static std::atomic<uint64_t> chat{0};
    static std::atomic<uint64_t> advisor{0};
    static std::atomic<uint64_t> planner{0};
    static std::atomic<uint64_t> resolver{0};
    switch (kind) {
    case AssistantRequestKind::Advisor: return advisor;
    case AssistantRequestKind::Planner: return planner;
    case AssistantRequestKind::Resolver: return resolver;
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
    if (model == "qwen2.5")
        model = "qwen2.5:7b";
    return model;
}

// Dev aid: with VERSLICER_EVAL_LOG=/path.jsonl set, append each reply so
// assistant_eval_test can score models offline. Off unless the variable is set.
void append_eval_record(const std::string& model, const std::vector<AssistantMessage>& messages, const std::string& reply)
{
    const char* path = std::getenv("VERSLICER_EVAL_LOG");
    if (!path || !*path || reply.empty())
        return;
    std::string user;
    for (auto it = messages.rbegin(); it != messages.rend(); ++it)
        if (it->role == "user") {
            user = it->content;
            break;
        }
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    std::ofstream out(path, std::ios::app);
    out << nlohmann::json{{"model", model}, {"user", user}, {"reply", reply}}.dump() << '\n';
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

AssistantClient::AssistantClient(std::string base_url)
    : m_base_url(trim_trailing_slash(std::move(base_url)))
{}

void AssistantClient::cancel_active_requests(AssistantCancelDomain domain)
{
    if (domain == AssistantCancelDomain::Chat || domain == AssistantCancelDomain::All) {
        cancel_request_state(active_request_state(AssistantRequestKind::Chat));
        cancel_request_state(active_request_state(AssistantRequestKind::Planner));
        cancel_request_state(active_request_state(AssistantRequestKind::Resolver));
    }
    if (domain == AssistantCancelDomain::Planner || domain == AssistantCancelDomain::All)
        cancel_request_state(active_request_state(AssistantRequestKind::Planner));
    if (domain == AssistantCancelDomain::Resolver || domain == AssistantCancelDomain::All)
        cancel_request_state(active_request_state(AssistantRequestKind::Resolver));
    if (domain == AssistantCancelDomain::Advisor || domain == AssistantCancelDomain::All)
        cancel_request_state(active_request_state(AssistantRequestKind::Advisor));
}

void AssistantClient::chat(const std::string& model, const std::vector<AssistantMessage>& messages, ChatCallback callback,
                        AssistantRequestKind kind, StreamCallback stream_cb, std::string tools_json)
{
    if (!callback)
        return;

    const AssistantCancelDomain cancel_domain = [&]() {
        switch (kind) {
        case AssistantRequestKind::Advisor: return AssistantCancelDomain::Advisor;
        case AssistantRequestKind::Planner: return AssistantCancelDomain::Planner;
        case AssistantRequestKind::Resolver: return AssistantCancelDomain::Resolver;
        default: return AssistantCancelDomain::Chat;
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
        std::vector<AssistantMessage>         messages;
        std::string                        tools_json;
        std::shared_ptr<std::atomic<bool>> cancel_flag;
        AssistantRequestKind                  kind;
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

    AssistantTelemetry::chat_started(job->model);
    const auto started = std::chrono::steady_clock::now();

    std::thread([job, tok, kind, started]() {
        ChatAttemptResult result;
        std::vector<ChatGptTurn> turns;
        turns.reserve(job->messages.size());
        for (const AssistantMessage& message : job->messages)
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
        append_eval_record(result.resolved_model, job->messages, result.content);

        const int latency_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());

        post_to_ui([job, tok, kind, result = std::move(result), latency_ms]() {
            if (tok != request_serial(kind).load() || wxGetApp().is_closing())
                return;
            const bool ok = result.error.empty() && !result.content.empty();
            AssistantTelemetry::ChatFinishedInfo info;
            info.error          = result.error;
            info.http_status    = result.http_status;
            info.resolved_model = result.resolved_model.empty() ? job->model : result.resolved_model;
            info.host           = job->base_url;
            info.kind           = job->kind == AssistantRequestKind::Advisor ? "advisor" : "chat";
            AssistantTelemetry::chat_finished(ok, latency_ms, result.retry_tier, info);
            job->callback(result.content, result.error);
        });
    }).detach();
}

void AssistantClient::list_models(ModelsCallback callback)
{
    if (!callback)
        return;

    std::thread([callback]() {
        std::string error;
        const std::vector<std::string> names = ChatGptAccount::list_models(&error);
        post_to_ui([callback, names, error]() {
            if (wxGetApp().is_closing())
                return;
            callback(names, error);
        });
    }).detach();
}

}} // namespace

#include "OllamaTelemetry.hpp"

#include "slic3r/Utils/NetworkAgent.hpp"

#include "../GUI_App.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI {

namespace {

void track_json(const std::string& key, const nlohmann::json& payload)
{
    if (NetworkAgent* agent = wxGetApp().getAgent())
        agent->track_event(key, payload.dump());
}

} // namespace

void OllamaTelemetry::chat_started(const std::string& model)
{
    track_json("ollama_chat_started", {{"model", model}});
}

void OllamaTelemetry::chat_finished(bool ok, int latency_ms, int retry_tier)
{
    chat_finished(ok, latency_ms, retry_tier, ChatFinishedInfo{});
}

void OllamaTelemetry::chat_finished(bool ok, int latency_ms, int retry_tier, const ChatFinishedInfo& info)
{
    nlohmann::json payload = {
        {"ok", ok},
        {"latency_ms", latency_ms},
        {"retry_tier", retry_tier},
    };
    if (!info.error.empty())
        payload["error"] = info.error;
    if (info.http_status != 0)
        payload["http_status"] = info.http_status;
    if (!info.resolved_model.empty())
        payload["resolved_model"] = info.resolved_model;
    if (!info.host.empty())
        payload["host"] = info.host;
    if (!info.kind.empty())
        payload["kind"] = info.kind;
    track_json("ollama_chat_finished", payload);
}

void OllamaTelemetry::action_blocked(const std::string& type, const std::string& reason)
{
    track_json("ollama_action_blocked", {{"type", type}, {"reason", reason}});
}

void OllamaTelemetry::action_executed(const std::string& type, bool success, bool effective_change)
{
    track_json("ollama_action_executed",
               {{"type", type}, {"success", success}, {"effective_change", effective_change}});
}

void OllamaTelemetry::workflow_finished(bool applied, bool cancelled, bool preview_only, int result_count)
{
    track_json("ollama_workflow_finished",
               {{"applied", applied},
                {"cancelled", cancelled},
                {"preview_only", preview_only},
                {"result_count", result_count}});
}

void OllamaTelemetry::advisor_scheduled()
{
    track_json("ollama_advisor_scheduled", nlohmann::json::object());
}

void OllamaTelemetry::advisor_dedup_skipped()
{
    track_json("ollama_advisor_dedup_skipped", nlohmann::json::object());
}

void OllamaTelemetry::set_config_applied(const std::string& preset, int attempted, int applied, bool partial)
{
    track_json("ollama_set_config_applied",
               {{"preset", preset},
                {"attempted", attempted},
                {"applied", applied},
                {"partial", partial}});
}

void OllamaTelemetry::set_config_noop(const std::string& preset)
{
    track_json("ollama_set_config_noop", {{"preset", preset}});
}

void OllamaTelemetry::normalize_injected_action(const std::string& type, const std::string& reason)
{
    nlohmann::json payload = {{"type", type}};
    if (!reason.empty())
        payload["reason"] = reason;
    track_json("ollama_normalize_injected_action", payload);
}

void OllamaTelemetry::context_cache_invalidate()
{
    track_json("ollama_context_cache_invalidate", nlohmann::json::object());
}

void OllamaTelemetry::slice_feedback_evaluated(bool still_needs_support, int unsupported_islands)
{
    track_json("ollama_slice_feedback_evaluated",
               {{"still_needs_support", still_needs_support}, {"unsupported_islands", unsupported_islands}});
}

void OllamaTelemetry::intent_signal_injected(const std::string& signal, const std::string& source)
{
    track_json("ollama_intent_signal_injected", {{"signal", signal}, {"source", source}});
}

void OllamaTelemetry::server_spawn_attempt(int attempt, const std::string& command)
{
    track_json("ollama_server_spawn_attempt", {{"attempt", attempt}, {"command", command}});
}

void OllamaTelemetry::server_spawn_success(long pid)
{
    track_json("ollama_server_spawn_success", {{"pid", pid}});
}

void OllamaTelemetry::server_spawn_failed(const std::string& reason)
{
    nlohmann::json payload = nlohmann::json::object();
    if (!reason.empty())
        payload["reason"] = reason;
    track_json("ollama_server_spawn_failed", payload);
}

}} // namespace

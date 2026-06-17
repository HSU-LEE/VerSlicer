#ifndef slic3r_OllamaTelemetry_hpp_
#define slic3r_OllamaTelemetry_hpp_

#include <string>

namespace Slic3r { namespace GUI {

struct OllamaTelemetry
{
    struct ChatFinishedInfo
    {
        std::string error;
        unsigned    http_status{0};
        std::string resolved_model;
        std::string host;
        std::string kind;
    };

    static void chat_started(const std::string& model);
    static void chat_finished(bool ok, int latency_ms, int retry_tier);
    static void chat_finished(bool ok, int latency_ms, int retry_tier, const ChatFinishedInfo& info);
    static void action_blocked(const std::string& type, const std::string& reason);
    static void action_executed(const std::string& type, bool success, bool effective_change);
    static void workflow_finished(bool applied, bool cancelled, bool preview_only, int result_count);
    static void advisor_scheduled();
    static void advisor_dedup_skipped();
    static void set_config_applied(const std::string& preset, int attempted, int applied, bool partial);
    static void set_config_noop(const std::string& preset);
    static void normalize_injected_action(const std::string& type, const std::string& reason = {});
    static void context_cache_invalidate();
    static void slice_feedback_evaluated(bool still_needs_support, int unsupported_islands);
    static void intent_signal_injected(const std::string& signal, const std::string& source);

    static void server_spawn_attempt(int attempt, const std::string& command);
    static void server_spawn_success(long pid);
    static void server_spawn_failed(const std::string& reason);
};

}} // namespace

#endif

#include "MakerWorldTelemetry.hpp"

#include "../GUI_App.hpp"

#include "slic3r/Utils/NetworkAgent.hpp"

#include <boost/log/trivial.hpp>
#include <mutex>
#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI {

namespace {

std::mutex  g_import_mutex;
std::string g_pending_design_id;

void track_json(const std::string& key, const nlohmann::json& payload)
{
    if (NetworkAgent* agent = wxGetApp().getAgent())
        agent->track_event(key, payload.dump(), BBL_CLOUD_PROVIDER);
}

} // namespace

void MakerWorldTelemetry::search_started(const std::string& query)
{
    BOOST_LOG_TRIVIAL(info) << "makerworld_search_started query=" << query;
    track_json("makerworld_search_started", {{"query", query}});
}

void MakerWorldTelemetry::search_finished(int count, int latency_ms, const std::string& source, bool ok,
                                          const std::string& detail)
{
    nlohmann::json payload = {
        {"count", count},
        {"latency_ms", latency_ms},
        {"source", source},
        {"ok", ok},
    };
    if (!detail.empty())
        payload["detail"] = detail;
    BOOST_LOG_TRIVIAL(info) << "makerworld_search_results count=" << count << " latency_ms=" << latency_ms
                            << " source=" << source << " ok=" << ok << " " << detail;
    track_json("makerworld_search_results", payload);
}

void MakerWorldTelemetry::import_started(const std::string& design_id)
{
    {
        std::lock_guard<std::mutex> lock(g_import_mutex);
        g_pending_design_id = design_id;
    }
    BOOST_LOG_TRIVIAL(info) << "makerworld_import_started design_id=" << design_id;
    track_json("makerworld_import_started", {{"design_id", design_id}});
}

void MakerWorldTelemetry::import_finished(bool ok, const std::string& design_id, const std::string& detail)
{
    nlohmann::json payload = {{"design_id", design_id}, {"ok", ok}};
    if (!detail.empty())
        payload["detail"] = detail;
    BOOST_LOG_TRIVIAL(info) << (ok ? "makerworld_import_ok" : "makerworld_import_fail")
                            << " design_id=" << design_id << " " << detail;
    track_json(ok ? "makerworld_import_ok" : "makerworld_import_fail", payload);
}

void MakerWorldTelemetry::plater_import_done(bool ok, const std::string& detail)
{
    std::string design_id;
    {
        std::lock_guard<std::mutex> lock(g_import_mutex);
        design_id = std::move(g_pending_design_id);
        g_pending_design_id.clear();
    }
    if (!design_id.empty())
        import_finished(ok, design_id, detail);
}

void MakerWorldTelemetry::translation_failed(const std::string& query, bool fallback_used)
{
    BOOST_LOG_TRIVIAL(warning) << "makerworld_translation_failed query=" << query
                               << " fallback_used=" << fallback_used;
    track_json("makerworld_translation_failed",
               {{"query", query}, {"ok", false}, {"fallback_used", fallback_used}});
}

}} // namespace

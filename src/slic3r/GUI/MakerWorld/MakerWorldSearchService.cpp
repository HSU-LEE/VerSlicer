#include "MakerWorldSearchService.hpp"
#include "MakerWorldQueryTranslator.hpp"
#include "MakerWorldSearchCore.hpp"
#include "MakerWorldTelemetry.hpp"
#include "MakerWorldUrl.hpp"

#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../OllamaAssistant/AiLocale.hpp"

#include "slic3r/Utils/BBLCloudServiceAgent.hpp"
#include "slic3r/Utils/BBLNetworkPlugin.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/ICloudServiceAgent.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"

#include <libslic3r/Utils.hpp>

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>
#include "libslic3r/Preset.hpp"

#include <boost/format.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>

namespace Slic3r { namespace GUI {

namespace {

std::string safe_string(const nlohmann::json& j, const char* key)
{
    if (!j.contains(key))
        return {};
    if (j[key].is_string())
        return j[key].get<std::string>();
    if (j[key].is_number_integer())
        return std::to_string(j[key].get<long long>());
    if (j[key].is_number_unsigned())
        return std::to_string(j[key].get<unsigned long long>());
    return {};
}

std::string pick_filename(const std::string& title, const std::string& fallback)
{
    std::string base = title.empty() ? fallback : title;
    for (char& c : base) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
    }
    boost::algorithm::trim(base);
    if (base.empty())
        base = "model";
    if (base.size() > 80)
        base = base.substr(0, 80);
    if (base.find(".3mf") == std::string::npos)
        base += ".3mf";
    return base;
}

static std::vector<std::string> makerworld_search_variants(const std::string& query)
{
    return search_query_variants(query, translate_search_query_to_english(query));
}

std::string build_makerworld_search_page_url(const std::string& query)
{
    const std::string country =
        wxGetApp().app_config ? wxGetApp().app_config->get_country_code() : "US";
    const std::string host  = wxGetApp().get_model_http_url(country);
    wxString          lang  = wxGetApp().current_language_code_safe().BeforeFirst('_');
    std::string       kw    = query;
    for (const auto& v : makerworld_search_variants(query)) {
        if (v.find_first_of("abcdefghijklmnopqrstuvwxyz0123456789") != std::string::npos) {
            kw = v;
            break;
        }
    }
    return (boost::format("%1%%2%/search/models?keyword=%3%") % host % lang.utf8_string()
            % Slic3r::Http::url_encode(kw))
        .str();
}

void refresh_bbl_session_for_makerworld()
{
    NetworkAgent* agent = wxGetApp().getAgent();
    if (!agent || !agent->is_user_login(BBL_CLOUD_PROVIDER))
        return;
    const auto cloud = agent->get_cloud_agent(BBL_CLOUD_PROVIDER);
    if (!cloud)
        return;
    cloud->connect_server();
    cloud->ensure_token_fresh("makerworld");
}

std::string resolve_cloud_access_token()
{
    // Optional manual token (MakerWorld browser cookie "token", usually starts with AAB_).
    if (wxGetApp().app_config) {
        const std::string manual = wxGetApp().app_config->get("makerworld", "bambu_access_token");
        if (!manual.empty())
            return manual;
    }

    NetworkAgent* agent = wxGetApp().getAgent();
    if (!agent)
        return {};
    if (!agent->is_user_login(BBL_CLOUD_PROVIDER))
        return {};
    const auto cloud = agent->get_cloud_agent(BBL_CLOUD_PROVIDER);
    if (!cloud)
        return {};

    cloud->ensure_token_fresh("makerworld");
    return cloud->get_access_token();
}

bool makerworld_download_auth_available(const MakerWorldSearchContext& ctx)
{
    return ctx.user_logged_in || !resolve_cloud_access_token().empty();
}

bool makerworld_search_auth_available(const MakerWorldSearchContext& ctx)
{
    return makerworld_download_auth_available(ctx);
}

std::string printer_model_query_suffix(const MakerWorldSearchContext& ctx)
{
    if (ctx.printer_model.empty())
        return {};
    return "&printer_model=" + Slic3r::Http::url_encode(ctx.printer_model);
}

MakerWorldSearchContext merge_search_context(const MakerWorldSearchContext& ctx_in)
{
    MakerWorldSearchContext ctx = MakerWorldSearchService::build_context();
    if (!ctx_in.country_code.empty())
        ctx.country_code = ctx_in.country_code;
    if (!ctx_in.locale.empty())
        ctx.locale = ctx_in.locale;
    if (!ctx_in.printer_model.empty())
        ctx.printer_model = ctx_in.printer_model;
    if (NetworkAgent* agent = wxGetApp().getAgent()) {
        ctx.network_agent_ok = true;
        ctx.user_logged_in   = agent->is_user_login(BBL_CLOUD_PROVIDER);
    }
    return ctx;
}

bool api_json_login_required(const nlohmann::json& j, unsigned http_status)
{
    if (http_status == 401 || http_status == 403)
        return true;
    if (j.contains("code") && j["code"].is_number_integer()) {
        const int code = j["code"].get<int>();
        if (code == 4 || code == 401 || code == 403)
            return true;
    }
    for (const char* key : {"error", "message"}) {
        if (!j.contains(key) || !j[key].is_string())
            continue;
        std::string msg = j[key].get<std::string>();
        boost::algorithm::to_lower(msg);
        if (msg.find("login") != std::string::npos || msg.find("sign in") != std::string::npos)
            return true;
    }
    return false;
}

const nlohmann::json* api_payload_node(const nlohmann::json& root)
{
    if (root.contains("data") && root["data"].is_object())
        return &root["data"];
    return &root;
}

void apply_makerworld_http_headers(Slic3r::Http& http)
{
    // Http() already injects BBLCloudServiceAgent::get_extra_header() via the global
    // extra_headers map (set by set_extra_http_header). Re-adding X-BBL-* here duplicates
    // them and MakerWorld design/search endpoints reject the request with HTTP 400.
    http.header("accept", "application/json");
    const std::string token = resolve_cloud_access_token();
    if (!token.empty())
        http.header("Authorization", "Bearer " + token);
}

std::string parse_api_error_message(const std::string& body, unsigned http_status)
{
    if (body.empty())
        return {};
    if (body.front() != '{')
        return body.size() > 200 ? body.substr(0, 200) : body;
    try {
        nlohmann::json j = nlohmann::json::parse(body);
        for (const char* key : {"message", "error", "detail"}) {
            if (j.contains(key) && j[key].is_string()) {
                const std::string msg = j[key].get<std::string>();
                if (!msg.empty())
                    return msg;
            }
        }
    } catch (...) {
        BOOST_LOG_TRIVIAL(debug) << "[MakerWorld] api error body was not valid JSON (http " << http_status << ")";
    }
    return "HTTP " + std::to_string(http_status);
}

struct HttpGetResult
{
    std::string body;
    std::string error;
    unsigned    status{0};

    bool auth_denied() const { return status == 401 || status == 403; }

    bool login_required() const
    {
        if (auth_denied())
            return true;
        if (body.empty() || body.front() != '{')
            return false;
        try {
            return api_json_login_required(nlohmann::json::parse(body), status);
        } catch (...) {
            return false;
        }
    }

    bool failed() const { return status >= 400 || (!error.empty() && body.empty()); }
};

bool http_should_retry(const HttpGetResult& r)
{
    return r.status == 0 || r.status == 502 || r.status == 503 || r.status == 429;
}

HttpGetResult http_get_sync_once(const std::string& url)
{
    HttpGetResult out;
    auto          http = Slic3r::Http::get(url);
    apply_makerworld_http_headers(http);
    http.timeout_connect(15).timeout_max(30);
    http.on_complete([&](std::string b, unsigned status) {
            out.body   = std::move(b);
            out.status = status;
        })
        .on_error([&](std::string, std::string e, unsigned status) {
            out.error  = std::move(e);
            out.status = status;
        })
        .perform_sync();
    if (out.failed() && out.error.empty())
        out.error = parse_api_error_message(out.body, out.status);
    return out;
}

HttpGetResult http_get_sync_ex(const std::string& url)
{
    HttpGetResult out = http_get_sync_once(url);
    if (http_should_retry(out)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        out = http_get_sync_once(url);
    }
    return out;
}

std::string http_get_sync(const std::string& url, std::string& error, unsigned& http_status)
{
    const HttpGetResult r = http_get_sync_ex(url);
    error       = r.error;
    http_status = r.status;
    return r.body;
}

struct StaffpickCache
{
    std::mutex                       mutex;
    std::string                      country_code;
    std::vector<MakerWorldCandidate> pool;
    std::chrono::steady_clock::time_point loaded_at{};
    bool                             loaded{false};
};

constexpr auto kStaffpickTtl = std::chrono::hours(1);

StaffpickCache& staffpick_cache()
{
    static StaffpickCache cache;
    return cache;
}

std::atomic<uint64_t>& search_request_generation()
{
    static std::atomic<uint64_t> gen{0};
    return gen;
}

struct SearchCacheEntry
{
    MakerWorldSearchResult                result;
    std::chrono::steady_clock::time_point at{};
};

constexpr auto kSearchCacheTtl = std::chrono::seconds(60);

std::mutex& search_result_cache_mutex()
{
    static std::mutex m;
    return m;
}

std::map<std::string, SearchCacheEntry>& search_result_cache()
{
    static std::map<std::string, SearchCacheEntry> cache;
    return cache;
}

std::string search_cache_key(const std::string& query, const MakerWorldSearchContext& ctx)
{
    return query + "|" + ctx.country_code + "|" + ctx.locale + "|" + ctx.printer_model;
}

void dedupe_candidates_inplace(std::vector<MakerWorldCandidate>& pool)
{
    std::vector<std::string> seen;
    std::vector<MakerWorldCandidate> out;
    out.reserve(pool.size());
    for (auto& c : pool) {
        if (c.design_id.empty())
            continue;
        if (std::find(seen.begin(), seen.end(), c.design_id) != seen.end())
            continue;
        seen.push_back(c.design_id);
        out.push_back(std::move(c));
    }
    pool = std::move(out);
}

std::string fetch_staffpick_page_plugin(int offset, int limit)
{
    NetworkAgent* agent = wxGetApp().getAgent();
    if (!agent)
        return {};
    auto& plugin = BBLNetworkPlugin::instance();
    if (plugin.get_get_design_staffpick() == nullptr)
        return {};

    std::promise<std::string> promise;
    std::future<std::string>  future = promise.get_future();
    const int                 rc     = agent->get_design_staffpick(
        offset, limit,
        [&promise](std::string body) { promise.set_value(std::move(body)); }, BBL_CLOUD_PROVIDER);
    if (rc != 0)
        return {};
    if (future.wait_for(std::chrono::seconds(30)) != std::future_status::ready)
        return {};
    return future.get();
}

void load_staffpick_pool(const MakerWorldSearchContext& ctx, std::vector<MakerWorldCandidate>& pool)
{
    if (!wxGetApp().app_config)
        return;

    pool.clear();
    const std::string host =
        wxGetApp().get_http_url(ctx.country_code, "v1/design-service/design/staffpick");
    const bool plugin_staffpick = BBLNetworkPlugin::instance().get_get_design_staffpick() != nullptr;

    for (int offset : {0, 60, 120}) {
        std::vector<MakerWorldCandidate> batch;
        if (plugin_staffpick) {
            const std::string body = fetch_staffpick_page_plugin(offset, 60);
            if (!body.empty())
                batch = parse_hits_json(body);
        }
        if (batch.empty()) {
            const std::string url = (boost::format("%1%/?offset=%2%&limit=60") % host % offset).str();
            HttpGetResult     r   = http_get_sync_ex(url);
            if (r.login_required())
                break;
            if (r.failed() && r.body.empty()) {
                r = http_get_sync_ex(url);
            }
            if (!r.body.empty())
                batch = parse_hits_json(r.body);
        }
        pool.insert(pool.end(), batch.begin(), batch.end());
    }
    dedupe_candidates_inplace(pool);
    BOOST_LOG_TRIVIAL(info) << "[MakerWorld] staffpick pool loaded size=" << pool.size()
                            << " country=" << ctx.country_code;
}

bool staffpick_cache_fresh(const StaffpickCache& cache, const std::string& country_code)
{
    if (!cache.loaded || cache.pool.empty() || cache.country_code != country_code)
        return false;
    return std::chrono::steady_clock::now() - cache.loaded_at < kStaffpickTtl;
}

void ensure_staffpick_pool(const MakerWorldSearchContext& ctx, bool force_refresh = false)
{
    auto& cache = staffpick_cache();
    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        if (!force_refresh && staffpick_cache_fresh(cache, ctx.country_code))
            return;
    }

    std::vector<MakerWorldCandidate> pool;
    load_staffpick_pool(ctx, pool);

    std::lock_guard<std::mutex> lock(cache.mutex);
    cache.pool         = std::move(pool);
    cache.country_code = ctx.country_code;
    cache.loaded_at    = std::chrono::steady_clock::now();
    cache.loaded       = true;
}

std::vector<MakerWorldCandidate> get_staffpick_pool_copy(const MakerWorldSearchContext& ctx)
{
    ensure_staffpick_pool(ctx);
    auto& cache = staffpick_cache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    return cache.pool;
}

int staffpick_pool_size()
{
    auto& cache = staffpick_cache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    return static_cast<int>(cache.pool.size());
}

struct DesignDownloadMeta
{
    std::string model_id;
    std::string profile_id;
    std::string title;
    std::string direct_url;
    bool        ok{false};
};

static const nlohmann::json* pick_design_instance(const nlohmann::json& d, const std::string& prefer_profile_id)
{
    if (!d.contains("instances") || !d["instances"].is_array())
        return nullptr;

    int64_t default_instance_id = 0;
    if (d.contains("defaultInstanceId") && d["defaultInstanceId"].is_number_integer())
        default_instance_id = d["defaultInstanceId"].get<int64_t>();

    const nlohmann::json* fallback = nullptr;
    for (const auto& inst : d["instances"]) {
        if (!inst.is_object())
            continue;
        if (!prefer_profile_id.empty()) {
            std::string pid = safe_string(inst, "profileId");
            if (pid.empty() && inst.contains("profileId") && inst["profileId"].is_number_integer())
                pid = std::to_string(inst["profileId"].get<int64_t>());
            if (pid == prefer_profile_id)
                return &inst;
        }
        if (default_instance_id > 0 && inst.contains("id") && inst["id"].is_number_integer()
            && inst["id"].get<int64_t>() == default_instance_id)
            return &inst;
        if (!fallback)
            fallback = &inst;
    }
    return fallback;
}

DesignDownloadMeta fetch_design_download_meta(const std::string& design_id, const MakerWorldSearchContext& ctx,
                                              const std::string& prefer_profile_id = {})
{
    DesignDownloadMeta meta;
    if (design_id.empty() || !wxGetApp().app_config)
        return meta;

    const std::string url =
        wxGetApp().get_http_url(ctx.country_code, "v1/design-service/design/" + design_id);
    std::string error;
    unsigned    status = 0;
    const std::string body = http_get_sync(url, error, status);
    if (body.empty() || body.front() != '{') {
        BOOST_LOG_TRIVIAL(warning) << "[MakerWorld] design meta failed design_id=" << design_id
                                   << " http=" << status << " err=" << error;
        return meta;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(body);
        const nlohmann::json& d = *api_payload_node(j);
        meta.model_id           = safe_string(d, "modelId");
        if (meta.model_id.empty())
            meta.model_id = safe_string(d, "model_id");
        meta.title = safe_string(d, "title");

        if (const nlohmann::json* chosen = pick_design_instance(d, prefer_profile_id)) {
            if (chosen->contains("profileId") && (*chosen)["profileId"].is_number_integer())
                meta.profile_id = std::to_string((*chosen)["profileId"].get<int64_t>());
            else
                meta.profile_id = safe_string(*chosen, "profileId");
            // Some design payloads carry the model id per-instance instead of at
            // the design level; without it the iot-profile download is skipped.
            if (meta.model_id.empty())
                meta.model_id = safe_string(*chosen, "modelId");
            if (meta.model_id.empty())
                meta.model_id = safe_string(*chosen, "model_id");
            if (meta.title.empty())
                meta.title = safe_string(*chosen, "title");
            for (const char* key : {"download_url", "downloadUrl", "url", "f3mfUrl", "packUrl"}) {
                const std::string direct = safe_string(*chosen, key);
                if (!direct.empty() && direct.find(".3mf") != std::string::npos) {
                    meta.direct_url = direct;
                    break;
                }
            }
        }
        // Design-level direct file URL (rare, but returned for some public designs).
        if (meta.direct_url.empty()) {
            for (const char* key : {"download_url", "downloadUrl", "f3mfUrl", "packUrl", "fileUrl"}) {
                const std::string direct = safe_string(d, key);
                if (!direct.empty() && direct.find(".3mf") != std::string::npos) {
                    meta.direct_url = direct;
                    break;
                }
            }
        }

        meta.ok = !meta.model_id.empty() && !meta.profile_id.empty();
    } catch (...) {
        BOOST_LOG_TRIVIAL(warning) << "[MakerWorld] design meta parse failed design_id=" << design_id;
    }
    return meta;
}

void apply_design_meta_to_candidate(MakerWorldCandidate& c, const DesignDownloadMeta& meta)
{
    if (c.model_id.empty())
        c.model_id = meta.model_id;
    if (c.profile_id.empty())
        c.profile_id = meta.profile_id;
    if (c.title.empty() && !meta.title.empty())
        c.title = meta.title;
    if (c.download_url.empty() && !meta.direct_url.empty())
        c.download_url = meta.direct_url;
}

void enrich_candidates_download_meta(std::vector<MakerWorldCandidate>& candidates, const MakerWorldSearchContext& ctx)
{
    std::vector<size_t> indices;
    indices.reserve(candidates.size());
    for (size_t i = 0; i < candidates.size(); ++i) {
        auto& c = candidates[i];
        if (c.design_id.empty())
            continue;
        if (!c.download_url.empty() && !c.model_id.empty() && !c.profile_id.empty())
            continue;
        indices.push_back(i);
    }
    if (indices.empty())
        return;

    const size_t n = indices.size();
    std::vector<std::future<DesignDownloadMeta>> futures;
    futures.reserve(n);
    for (size_t idx : indices) {
        const std::string design_id = candidates[idx].design_id;
        futures.push_back(std::async(std::launch::async, [design_id, ctx]() {
            return fetch_design_download_meta(design_id, ctx);
        }));
    }
    for (size_t i = 0; i < n; ++i)
        apply_design_meta_to_candidate(candidates[indices[i]], futures[i].get());
}

struct BackendSearchOutcome
{
    std::vector<MakerWorldCandidate> candidates;
    std::string                      source;
    std::string                      error;
    bool                             auth_denied{false};
    unsigned                         http_status{0};
};

BackendSearchOutcome search_via_plugin(const std::string& query, const MakerWorldSearchContext& ctx, int limit)
{
    BackendSearchOutcome outcome;
    if (!ctx.plugin_search_available)
        return outcome;
    NetworkAgent* agent = wxGetApp().getAgent();
    if (!agent)
        return outcome;

    if (makerworld_search_auth_available(ctx))
        refresh_bbl_session_for_makerworld();

    std::string body;
    unsigned    http_code = 0;
    std::string http_error;
    const int   rc = agent->search_makerworld(query, limit, ctx.locale, ctx.printer_model, &body, &http_code, &http_error);
    outcome.http_status = http_code;
    if (http_code == 401 || http_code == 403) {
        outcome.auth_denied = true;
        outcome.error       = _u8L("Sign in to Bambu Cloud to search MakerWorld, or paste a model link.");
        return outcome;
    }
    if (!body.empty() && body.front() == '{') {
        try {
            if (api_json_login_required(nlohmann::json::parse(body), http_code)) {
                outcome.auth_denied = true;
                outcome.error       = _u8L("Sign in to Bambu Cloud to search MakerWorld, or paste a model link.");
                return outcome;
            }
        } catch (...) {
            BOOST_LOG_TRIVIAL(debug) << "[MakerWorld] plugin search body was not valid JSON";
        }
    }
    if (rc != 0 || body.empty()) {
        outcome.error = http_error.empty() ? "search_makerworld plugin failed" : http_error;
        return outcome;
    }
    outcome.candidates = parse_hits_json(body);
    outcome.source     = "plugin";
    return outcome;
}

BackendSearchOutcome fetch_search_service_page(const std::string& path, const MakerWorldSearchContext& ctx)
{
    BackendSearchOutcome outcome;
    const std::string    url = wxGetApp().get_http_url(ctx.country_code, path);
    const HttpGetResult  r   = http_get_sync_ex(url);
    outcome.http_status      = r.status;
    if (r.login_required()) {
        outcome.auth_denied = true;
        outcome.error       = _u8L("Sign in to Bambu Cloud to search MakerWorld, or paste a model link.");
        return outcome;
    }
    if (r.body.empty())
        return outcome;
    outcome.candidates = parse_hits_json(r.body);
    outcome.source     = "search_service";
    if (!r.error.empty())
        outcome.error = r.error;
    return outcome;
}

BackendSearchOutcome search_via_search_service(const std::string& query, const MakerWorldSearchContext& ctx, int limit)
{
    BackendSearchOutcome outcome;
    if (!makerworld_search_auth_available(ctx) || !wxGetApp().app_config || query.empty())
        return outcome;

    const std::string printer_q = printer_model_query_suffix(ctx);
    const std::vector<std::string> paths = {
        (boost::format("v1/search-service/select/design2?keyword=%1%&limit=%2%&offset=0&lang=%3%%4%")
         % Slic3r::Http::url_encode(query) % limit % ctx.locale % printer_q)
            .str(),
        (boost::format("v1/search-service/select/design2?keyword=%1%&limit=%2%&offset=8&lang=%3%%4%")
         % Slic3r::Http::url_encode(query) % limit % ctx.locale % printer_q)
            .str(),
        (boost::format("v1/search-service/searchlist?keyword=%1%&limit=%2%&offset=0&lang=%3%%4%")
         % Slic3r::Http::url_encode(query) % limit % ctx.locale % printer_q)
            .str(),
    };

    for (const auto& path : paths) {
        BackendSearchOutcome page = fetch_search_service_page(path, ctx);
        if (page.auth_denied) {
            outcome.auth_denied = true;
            outcome.error       = page.error;
            return outcome;
        }
        if (!page.candidates.empty()) {
            outcome = std::move(page);
            return outcome;
        }
        if (!page.error.empty())
            outcome.error = page.error;
    }
    return outcome;
}

BackendSearchOutcome search_via_http(const std::string& query, const MakerWorldSearchContext& ctx, int limit)
{
    BackendSearchOutcome outcome;
    if (!wxGetApp().app_config)
        return outcome;

    const std::string printer_q = printer_model_query_suffix(ctx);
    const std::string host      = wxGetApp().get_http_url(ctx.country_code, "v1/design-service/design/search");
    const std::vector<std::string> urls = {
        (boost::format("%1%?keyword=%2%&limit=%3%&locale=%4%&offset=0%5%") % host % Slic3r::Http::url_encode(query)
         % limit % ctx.locale % printer_q)
            .str(),
        (boost::format("%1%?keyword=%2%&limit=%3%&locale=%4%&offset=8%5%") % host % Slic3r::Http::url_encode(query)
         % limit % ctx.locale % printer_q)
            .str(),
    };

    for (const std::string& url : urls) {
        const HttpGetResult r = http_get_sync_ex(url);
        outcome.http_status   = r.status;
        if (r.login_required()) {
            outcome.auth_denied = true;
            outcome.error       = _u8L("Sign in to Bambu Cloud to search MakerWorld, or paste a model link.");
            return outcome;
        }
        if (r.body.empty() && !r.error.empty()) {
            outcome.error = r.error;
            continue;
        }
        auto batch = parse_hits_json(r.body);
        if (!batch.empty()) {
            outcome.candidates = std::move(batch);
            outcome.source     = "http_search";
            return outcome;
        }
        if (!r.error.empty())
            outcome.error = r.error;
    }
    return outcome;
}

MakerWorldSearchResult search_via_staffpick_filter(const std::string& query, const MakerWorldSearchContext& ctx,
                                                   int display_limit)
{
    MakerWorldSearchResult result;
    if (!wxGetApp().app_config)
        return result;

    const std::vector<MakerWorldCandidate> pool = get_staffpick_pool_copy(ctx);
    std::vector<MakerWorldCandidate>       merged;
    for (const auto& qv : makerworld_search_variants(query)) {
        merged = merge_candidates(std::move(merged), filter_by_query_scored(pool, qv, display_limit * 2));
    }
    result.candidates = rank_and_dedupe(std::move(merged), query, display_limit, true);
    result.ok         = !result.candidates.empty();
    result.source     = "staffpick_filter";
    return result;
}

std::string build_detail_page_url(const std::string& design_id)
{
    if (design_id.empty())
        return {};

    const std::string host = wxGetApp().get_model_http_url(
        wxGetApp().app_config ? wxGetApp().app_config->get_country_code() : "US");
    wxString lang = wxGetApp().current_language_code_safe().BeforeFirst('_');
    const std::string model_path =
        (boost::format("%1%/models/%2%") % lang.utf8_string() % design_id).str();

    if (NetworkAgent* agent = wxGetApp().getAgent()) {
        if (agent->is_user_login(BBL_CLOUD_PROVIDER)) {
            std::string ticket;
            if (agent->request_bind_ticket(&ticket) == 0 && !ticket.empty()) {
                return host + "api/sign-in/ticket?to=" + host + Slic3r::Http::url_encode(model_path)
                     + "&ticket=" + ticket;
            }
        }
    }
    return host + model_path;
}

std::string fetch_download_via_plugin(const std::string& design_id, std::string& filename,
                                      const MakerWorldSearchContext& ctx)
{
    if (!ctx.plugin_download_available)
        return {};
    NetworkAgent* agent = wxGetApp().getAgent();
    if (!agent)
        return {};

    refresh_bbl_session_for_makerworld();
    std::string url;
    unsigned    http_code = 0;
    std::string http_error;
    const int   rc = agent->get_makerworld_download_url(design_id, &url, &filename, &http_code, &http_error,
                                                        BBL_CLOUD_PROVIDER);
    if (rc == 0 && !url.empty())
        return url;

    BOOST_LOG_TRIVIAL(warning) << "[MakerWorld] plugin download failed design_id=" << design_id << " rc=" << rc
                               << " http=" << http_code << " err=" << http_error;
    return {};
}

std::string extract_download_url_from_json(const nlohmann::json& node, int depth = 0)
{
    if (depth > 4 || !node.is_object())
        return {};
    for (const char* key : {"url", "download_url", "downloadUrl", "f3mfUrl", "packUrl", "fileUrl"}) {
        const std::string u = safe_string(node, key);
        if (!u.empty())
            return u;
    }
    if (node.contains("data") && node["data"].is_object())
        return extract_download_url_from_json(node["data"], depth + 1);
    return {};
}

std::string fetch_download_via_iot_profile(const std::string& profile_id, const std::string& model_id,
                                           const MakerWorldSearchContext& ctx, std::string& filename,
                                           unsigned& http_status, bool& login_required)
{
    login_required = false;
    if (!wxGetApp().app_config || profile_id.empty() || model_id.empty())
        return {};
    if (resolve_cloud_access_token().empty()) {
        BOOST_LOG_TRIVIAL(warning) << "[MakerWorld] iot download skipped: no access token profile="
                                   << profile_id << " model=" << model_id;
        return {};
    }

    const std::string path = (boost::format("v1/iot-service/api/user/profile/%1%?model_id=%2%") % profile_id
                              % Slic3r::Http::url_encode(model_id))
                                 .str();
    const std::string url  = wxGetApp().get_http_url(ctx.country_code, path);
    const HttpGetResult r  = http_get_sync_ex(url);
    http_status            = r.status;
    if (r.body.empty() || r.body.front() != '{') {
        BOOST_LOG_TRIVIAL(warning) << "[MakerWorld] iot download empty body profile=" << profile_id
                                   << " http=" << r.status << " err=" << r.error;
        return {};
    }

    try {
        nlohmann::json j = nlohmann::json::parse(r.body);
        if (api_json_login_required(j, http_status) || r.login_required()) {
            login_required = true;
            return {};
        }
        if (j.contains("code") && j["code"].is_number_integer() && j["code"].get<int>() != 0) {
            BOOST_LOG_TRIVIAL(warning) << "[MakerWorld] iot download api code=" << j["code"].get<int>()
                                       << " profile=" << profile_id;
            return {};
        }
        const nlohmann::json& d = *api_payload_node(j);
        std::string out_url     = extract_download_url_from_json(d);
        if (out_url.empty())
            out_url = extract_download_url_from_json(j);
        if (out_url.empty()) {
            BOOST_LOG_TRIVIAL(warning) << "[MakerWorld] iot download no url in response profile=" << profile_id;
            return {};
        }
        filename = safe_string(d, "filename");
        if (filename.empty())
            filename = safe_string(d, "name");
        return out_url;
    } catch (...) {
        return {};
    }
}

std::string make_download_info(const std::string& url, const std::string& filename)
{
    return url + "&name=" + Slic3r::Http::url_encode(filename);
}

} // namespace

MakerWorldSearchContext MakerWorldSearchService::build_context()
{
    MakerWorldSearchContext ctx;
    if (wxGetApp().app_config)
        ctx.country_code = wxGetApp().app_config->get_country_code();
    ctx.locale = wxGetApp().current_language_code_safe().BeforeFirst('_').utf8_string();
    if (wxGetApp().preset_bundle) {
        const Preset& pr = wxGetApp().preset_bundle->printers.get_selected_preset();
        if (pr.config.has("printer_model"))
            ctx.printer_model = pr.config.opt_string("printer_model");
        if (ctx.printer_model.empty() && pr.config.has("machine"))
            ctx.printer_model = pr.config.opt_string("machine");
        if (ctx.printer_model.empty())
            ctx.printer_model = pr.name;
    }
    if (NetworkAgent* agent = wxGetApp().getAgent()) {
        ctx.network_agent_ok = true;
        ctx.user_logged_in   = agent->is_user_login(BBL_CLOUD_PROVIDER);
    }
    auto& plugin = BBLNetworkPlugin::instance();
    ctx.plugin_search_available   = plugin.get_search_makerworld() != nullptr;
    ctx.plugin_download_available = plugin.get_get_makerworld_download_url() != nullptr;
    return ctx;
}

std::string build_search_telemetry_detail(const std::string& normalized, int variant_count, bool auth_denied,
                                        int deduped_count, int pool_size,
                                        const std::vector<std::string>& backend_attempts)
{
    nlohmann::json j;
    j["query_normalized"] = normalized;
    j["variant_count"]    = variant_count;
    j["auth_denied"]      = auth_denied;
    j["deduped_count"]    = deduped_count;
    j["pool_size"]        = pool_size;
    j["backend_attempts"] = backend_attempts;
    return j.dump();
}

MakerWorldSearchResult MakerWorldSearchService::search_sync(const std::string& query, const MakerWorldSearchContext& ctx_in)
{
    const auto t0 = std::chrono::steady_clock::now();

    MakerWorldSearchContext ctx = merge_search_context(ctx_in);

    const std::string normalized =
        sanitize_search_text(MakerWorldSearchService::normalize_search_query(query));

    auto record_search = [&](MakerWorldSearchResult& r, const std::string& telemetry_detail) {
        const auto t1 = std::chrono::steady_clock::now();
        r.latency_ms  = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
        MakerWorldTelemetry::search_finished(static_cast<int>(r.candidates.size()), r.latency_ms, r.source, r.ok,
                                             telemetry_detail.empty() ? r.error : telemetry_detail);
    };

    MakerWorldSearchResult result;
    if (normalized.empty()) {
        result.error = _u8L("Enter a search term or paste a MakerWorld link.");
        record_search(result, {});
        return result;
    }
    if (normalized.size() < 2) {
        result.error = _u8L("Please describe the model more specifically (e.g. size, articulated, vase).");
        record_search(result, {});
        return result;
    }

    {
        std::lock_guard<std::mutex> lock(search_result_cache_mutex());
        const auto                  it = search_result_cache().find(search_cache_key(normalized, ctx));
        if (it != search_result_cache().end()
            && std::chrono::steady_clock::now() - it->second.at < kSearchCacheTtl) {
            MakerWorldSearchResult cached = it->second.result;
            record_search(cached, "cache_hit=1");
            return cached;
        }
    }

    MakerWorldTelemetry::search_started(normalized);

    if (makerworld_search_auth_available(ctx))
        refresh_bbl_session_for_makerworld();

    constexpr int kFetchLimit    = 24;
    constexpr int kDisplayLimit  = 8;

    const std::string            translated_en = translate_search_query_to_english(normalized);
    const auto                   variants    = search_query_variants(normalized, translated_en);
    std::vector<MakerWorldCandidate> merged;
    std::string                  primary_source;
    bool                         auth_denied = false;
    std::vector<std::string>     backend_attempts;

    for (const auto& qv : variants) {
        if (ctx.plugin_search_available) {
            BackendSearchOutcome o = search_via_plugin(qv, ctx, kFetchLimit);
            backend_attempts.push_back("plugin:" + std::to_string(o.candidates.size()) + ":http=" + std::to_string(o.http_status)
                                       + (o.auth_denied ? ":auth" : ""));
            if (o.auth_denied)
                auth_denied = true;
            else if (!o.candidates.empty()) {
                merged = merge_candidates(std::move(merged), o.candidates);
                if (primary_source.empty())
                    primary_source = o.source;
            }
        }
        if (makerworld_search_auth_available(ctx)) {
            BackendSearchOutcome o = search_via_search_service(qv, ctx, kFetchLimit);
            backend_attempts.push_back("search_service:" + std::to_string(o.candidates.size()) + ":http="
                                       + std::to_string(o.http_status) + (o.auth_denied ? ":auth" : ""));
            if (o.auth_denied)
                auth_denied = true;
            else if (!o.candidates.empty()) {
                merged = merge_candidates(std::move(merged), o.candidates);
                if (primary_source.empty())
                    primary_source = o.source;
            }
        }
        BackendSearchOutcome o = search_via_http(qv, ctx, kFetchLimit);
        backend_attempts.push_back("http_search:" + std::to_string(o.candidates.size()) + ":http="
                                   + std::to_string(o.http_status) + (o.auth_denied ? ":auth" : ""));
        if (o.auth_denied)
            auth_denied = true;
        else if (!o.candidates.empty()) {
            merged = merge_candidates(std::move(merged), o.candidates);
            if (primary_source.empty())
                primary_source = o.source;
        }
    }

    const int pre_rank_count = static_cast<int>(merged.size());
    if (!merged.empty()) {
        result.candidates = rank_and_dedupe(std::move(merged), normalized, kDisplayLimit,
                                            !makerworld_search_auth_available(ctx));
        result.ok         = !result.candidates.empty();
        result.source     = primary_source.empty() ? "merged" : primary_source;
    }

    if (!result.ok) {
        MakerWorldSearchResult sp = search_via_staffpick_filter(normalized, ctx, kDisplayLimit);
        backend_attempts.push_back("staffpick:" + std::to_string(sp.candidates.size()));
        if (sp.ok) {
            result           = std::move(sp);
            auth_denied        = false;
        }
    }

    if (result.ok)
        enrich_candidates_download_meta(result.candidates, ctx);

    if (!result.ok) {
        if (auth_denied && !makerworld_search_auth_available(ctx))
            result.error = _u8L("Sign in to Bambu Cloud to search MakerWorld, or paste a model link.");
        else if (result.error.empty())
            result.error = _u8L("No models found on MakerWorld. Try different keywords or paste a model link.");
    }

    const std::string telemetry_detail = build_search_telemetry_detail(
        normalized, static_cast<int>(variants.size()), auth_denied, pre_rank_count, staffpick_pool_size(),
        backend_attempts);
    record_search(result, telemetry_detail);

    if (result.ok) {
        std::lock_guard<std::mutex> lock(search_result_cache_mutex());
        search_result_cache()[search_cache_key(normalized, ctx)] = {result, std::chrono::steady_clock::now()};
    }

    return result;
}

void MakerWorldSearchService::search_async(const std::string& query, const MakerWorldSearchContext& ctx,
                                           std::function<void(MakerWorldSearchResult)> callback)
{
    const uint64_t generation = ++search_request_generation();
    std::thread([query, ctx, cb = std::move(callback), generation]() {
        MakerWorldSearchResult r = MakerWorldSearchService::search_sync(query, ctx);
        if (wxGetApp().is_closing())
            return; // shutting down: never post to a dying main loop
        wxGetApp().CallAfter([r, cb, generation]() {
            if (wxGetApp().is_closing())
                return;
            if (generation != search_request_generation().load())
                return;
            cb(r);
        });
    }).detach();
}

void MakerWorldSearchService::prefetch_staffpick_pool()
{
    // build_context() reads app_config / presets / the network agent and is only
    // safe on the main thread, so snapshot it here and hand the copy to the
    // worker; only the network fetch runs off-thread.
    if (wxGetApp().is_closing())
        return;
    const MakerWorldSearchContext ctx = MakerWorldSearchService::build_context();
    std::thread([ctx]() {
        ensure_staffpick_pool(ctx);
    }).detach();
}

MakerWorldImportPayload MakerWorldSearchService::resolve_import(const MakerWorldCandidate& candidate)
{
    MakerWorldImportPayload out;
    out.detail_page_url = build_detail_page_url(candidate.design_id);

    const MakerWorldSearchContext ctx = build_context();
    const bool                    has_auth = makerworld_download_auth_available(ctx);

    if (candidate.login_required && !has_auth) {
        out.error = AiLocale::text("Sign in to Bambu Cloud to download this model.",
                                   "이 모델을 내려받으려면 Bambu Cloud에 로그인하세요.").utf8_string();
        return out;
    }

    if (ctx.user_logged_in)
        refresh_bbl_session_for_makerworld();

    std::string url      = candidate.download_url;
    std::string filename = candidate.filename.empty() ? pick_filename(candidate.title, "model") : candidate.filename;

    if (url.empty() && !candidate.design_id.empty()) {
        url = fetch_download_via_plugin(candidate.design_id, filename, ctx);
    }

    std::string model_id   = candidate.model_id;
    std::string profile_id = candidate.profile_id;
    std::string title      = candidate.title;

    if (url.empty() && !candidate.design_id.empty()) {
        const DesignDownloadMeta meta = fetch_design_download_meta(candidate.design_id, ctx, profile_id);
        if (model_id.empty())
            model_id = meta.model_id;
        if (profile_id.empty())
            profile_id = meta.profile_id;
        if (title.empty())
            title = meta.title;
        if (!meta.direct_url.empty())
            url = meta.direct_url;

        if (url.empty() && !model_id.empty() && !profile_id.empty()) {
            if (resolve_cloud_access_token().empty())
                refresh_bbl_session_for_makerworld();
            unsigned http_status    = 0;
            bool     login_required = false;
            url = fetch_download_via_iot_profile(profile_id, model_id, ctx, filename, http_status, login_required);
            if (url.empty() && login_required) {
                out.error = has_auth
                    ? AiLocale::text("Bambu Cloud session expired. Sign out and sign in again to download this model.",
                                     "Bambu Cloud 세션이 만료되었습니다. 로그아웃 후 다시 로그인하면 내려받을 수 있습니다.").utf8_string()
                    : AiLocale::text("Sign in to Bambu Cloud to download this model.",
                                     "이 모델을 내려받으려면 Bambu Cloud에 로그인하세요.").utf8_string();
                return out;
            }
        }
    }

    if (url.empty()) {
        BOOST_LOG_TRIVIAL(warning) << "[MakerWorld] resolve_import: no download url design_id="
                                   << candidate.design_id << " model_id=" << model_id
                                   << " profile_id=" << profile_id
                                   << " plugin_dl=" << (ctx.plugin_download_available ? 1 : 0)
                                   << " has_token=" << (resolve_cloud_access_token().empty() ? 0 : 1);
        if (resolve_cloud_access_token().empty())
            out.error = AiLocale::text(
                "Sign in to Bambu Cloud (Settings → Online) to download. If already signed in, sign out and sign in again.",
                "내려받으려면 Bambu Cloud에 로그인하세요 (설정 → 온라인). 이미 로그인했다면 로그아웃 후 다시 로그인해 주세요.").utf8_string();
        else
            out.error = AiLocale::text(
                "Could not get a download link. Open the model on MakerWorld and use Download, or paste the file link.",
                "다운로드 링크를 받지 못했습니다. MakerWorld에서 모델을 열어 내려받거나 파일 링크를 붙여넣어 주세요.").utf8_string();
        return out;
    }

    if (filename.empty())
        filename = pick_filename(title.empty() ? candidate.title : title, "model_" + candidate.design_id);

    if (!is_allowed_makerworld_download_url(url) && url.find(".3mf") == std::string::npos) {
        // Allow CDN hosts returned by API even if not makerworld.com
        if (url.find("https://") != 0 && url.find("http://") != 0) {
            out.error = AiLocale::text("Download URL is not allowed.",
                                       "허용되지 않는 다운로드 URL입니다.").utf8_string();
            return out;
        }
    }

    out.download_info = make_download_info(url, filename);
    out.ok            = true;
    return out;
}

MakerWorldImportPayload MakerWorldSearchService::resolve_import_from_url(const std::string& url)
{
    MakerWorldImportPayload out;
    std::string lower = url;
    boost::algorithm::to_lower(lower);

    if (lower.find(".3mf") != std::string::npos || lower.find("/download") != std::string::npos) {
        std::string filename = "model.3mf";
        if (auto pos = url.find_last_of('/'); pos != std::string::npos && pos + 1 < url.size()) {
            const std::string tail = url.substr(pos + 1);
            if (tail.find(".3mf") != std::string::npos)
                filename = tail;
        }
        out.download_info = make_download_info(url, filename);
        out.ok            = true;
        return out;
    }

    const std::string id = parse_design_id_from_url(url);
    if (!id.empty()) {
        MakerWorldCandidate c;
        c.design_id   = id;
        c.profile_id  = parse_profile_id_from_url(url);
        c.title       = "model_" + id;
        c.filename    = pick_filename({}, c.title);
        return resolve_import(c);
    }

    out.error = AiLocale::text("Could not read a model ID from that MakerWorld link.",
                               "MakerWorld 링크에서 모델 ID를 읽지 못했습니다.").utf8_string();
    return out;
}

MakerWorldImportPayload MakerWorldSearchService::resolve_import_from_design_id(const std::string& design_id)
{
    MakerWorldCandidate c;
    c.design_id = design_id;
    c.title     = "model_" + design_id;
    c.filename  = pick_filename({}, c.title);
    return resolve_import(c);
}

std::string MakerWorldSearchService::makerworld_search_page_url(const std::string& query)
{
    return build_makerworld_search_page_url(query);
}

std::string MakerWorldSearchService::normalize_search_query(const std::string& user_text)
{
    return normalize_makerworld_search_query(user_text);
}

void MakerWorldSearchService::apply_download_http_headers(Slic3r::Http& http)
{
    refresh_bbl_session_for_makerworld();
    apply_makerworld_http_headers(http);
}

bool MakerWorldSearchService::download_url_needs_auth(const std::string& url)
{
    std::string lower = url;
    boost::algorithm::to_lower(lower);
    // Presigned CDN/S3 URLs must not get extra Authorization headers.
    if (lower.find("amazonaws.com") != std::string::npos || lower.find("cloudfront.net") != std::string::npos)
        return false;
    return lower.find("bambulab.com") != std::string::npos || lower.find("makerworld.com") != std::string::npos;
}

std::string MakerWorldSearchService::resolve_download_info(const std::string& url)
{
    const MakerWorldImportPayload payload = resolve_import_from_url(url);
    return payload.ok ? payload.download_info : std::string{};
}

}} // namespace

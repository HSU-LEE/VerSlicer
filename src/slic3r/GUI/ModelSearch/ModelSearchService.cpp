#include "ModelSearchService.hpp"

#include "MakerWorldProvider.hpp"
#include "ModelSearchDedupe.hpp"
#include "ModelSearchQueryBuilder.hpp"

#include "../MakerWorld/MakerWorldSearchService.hpp"

#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../OllamaAssistant/AiLocale.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <thread>
#include <utility>

namespace Slic3r { namespace GUI {

namespace {

// Fan-out timing / sizing. Centralized so no timeout magic numbers leak into the
// control flow. Providers start concurrently, so wall time is bounded by the
// per-provider budget; the aggregate budget is a hard ceiling.
constexpr auto kPerProviderTimeout    = std::chrono::seconds(12);
constexpr auto kAggregateTimeout      = std::chrono::seconds(15);
constexpr int  kAggregateDisplayLimit = 24;

} // namespace

ModelSearchService& ModelSearchService::instance()
{
    static ModelSearchService s_instance;
    return s_instance;
}

ModelSearchService::ModelSearchService()
{
    // Default provider set for Foundation A. Additional providers are registered
    // by later phases via register_provider().
    register_provider(std::make_shared<const MakerWorldProvider>());
}

void ModelSearchService::register_provider(ModelProviderPtr provider)
{
    if (!provider)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& existing : m_providers) {
        if (existing && existing->provider_enum() == provider->provider_enum())
            return; // idempotent: one instance per provider id
    }
    m_providers.push_back(std::move(provider));
}

std::vector<ModelProviderPtr> ModelSearchService::enabled_providers() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_providers;
}

ModelProviderPtr ModelSearchService::find_provider(ModelProviderId id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& p : m_providers) {
        if (p && p->provider_enum() == id)
            return p;
    }
    return nullptr;
}

ModelSearchQuery ModelSearchService::build_query(const std::string& user_text, bool allow_translation)
{
    return ModelSearchQueryBuilder::build_query(user_text, allow_translation);
}

ModelSearchContext ModelSearchService::build_context()
{
    // Must run on the main thread: MakerWorldSearchService::build_context reads
    // wxGetApp() state (app_config, preset_bundle, NetworkAgent, plugins).
    const MakerWorldSearchContext mw = MakerWorldSearchService::build_context();

    ModelSearchContext ctx;
    ctx.locale                    = mw.locale;
    ctx.country_code              = mw.country_code;
    ctx.printer_model             = mw.printer_model;
    ctx.user_logged_in            = mw.user_logged_in;
    ctx.network_agent_ok          = mw.network_agent_ok;
    ctx.plugin_search_available   = mw.plugin_search_available;
    ctx.plugin_download_available = mw.plugin_download_available;
    return ctx;
}

std::vector<ModelProviderSearchResult> ModelSearchService::fan_out(const std::vector<ModelProviderPtr>& providers,
                                                                   const ModelSearchQuery&              query,
                                                                   const ModelSearchContext&            ctx) const
{
    std::vector<ModelProviderPtr> searchers;
    searchers.reserve(providers.size());
    for (const auto& p : providers) {
        if (p && p->capabilities().can_search)
            searchers.push_back(p);
    }

    const size_t n = searchers.size();
    std::vector<ModelProviderSearchResult> results;
    if (n == 0)
        return results;

    // Shared, ref-counted collection state. Detached worker threads keep it
    // alive via their captured shared_ptr, so late finishers never touch freed
    // memory even after this function has returned partial results.
    struct Shared
    {
        std::mutex                             m;
        std::condition_variable                cv;
        std::vector<ModelProviderSearchResult> slots;
        std::vector<char>                      done;
        size_t                                 remaining{0};
    };
    auto shared = std::make_shared<Shared>();
    shared->slots.resize(n);
    shared->done.assign(n, 0);
    shared->remaining = n;

    for (size_t i = 0; i < n; ++i) {
        ModelProviderPtr provider = searchers[i];
        // Capture query/ctx BY VALUE: worker threads must not alias caller state.
        std::thread([shared, provider, query, ctx, i]() {
            ModelProviderSearchResult r;
            try {
                r = provider->search_sync(query, ctx);
            } catch (...) {
                r.provider_id = provider->provider_enum();
                r.ok          = false;
                r.error       = "provider search threw an exception";
            }
            std::lock_guard<std::mutex> lk(shared->m);
            shared->slots[i] = std::move(r);
            shared->done[i]  = 1;
            if (shared->remaining > 0)
                --shared->remaining;
            shared->cv.notify_all();
        }).detach();
    }

    const auto start        = std::chrono::steady_clock::now();
    const auto agg_deadline = start + kAggregateTimeout;
    const auto pp_deadline  = start + kPerProviderTimeout;
    const auto wait_until    = std::min(agg_deadline, pp_deadline);

    results.resize(n);
    {
        std::unique_lock<std::mutex> lk(shared->m);
        shared->cv.wait_until(lk, wait_until, [&] { return shared->remaining == 0; });
        for (size_t i = 0; i < n; ++i) {
            if (shared->done[i]) {
                results[i] = std::move(shared->slots[i]);
            } else {
                results[i].provider_id = searchers[i]->provider_enum();
                results[i].ok          = false;
                results[i].timed_out   = true;
                results[i].error       = "provider timed out";
            }
        }
    }
    return results;
}

ModelSearchAggregateResult ModelSearchService::search_sync(const ModelSearchQuery&      query,
                                                           const ModelSearchContext&    ctx,
                                                           const CandidateRankerConfig& rank_cfg) const
{
    const auto t0 = std::chrono::steady_clock::now();

    ModelSearchAggregateResult agg;
    if (query.empty()) {
        agg.error = AiLocale::text("Enter a search term or paste a model link.",
                                   "검색어를 입력하거나 모델 링크를 붙여넣어 주세요.").utf8_string();
        return agg;
    }

    agg.per_provider = fan_out(enabled_providers(), query, ctx);

    std::vector<ModelCandidate> merged;
    bool any_ok   = false;
    bool any_fail = false;
    for (const auto& pr : agg.per_provider) {
        if (pr.ok)
            any_ok = true;
        else
            any_fail = true;
        merged.insert(merged.end(), pr.candidates.begin(), pr.candidates.end());
    }

    ModelSearchDedupe::dedupe_cross_provider(merged);
    CandidateRanker::rank_in_place(merged, query, rank_cfg);
    if (static_cast<int>(merged.size()) > kAggregateDisplayLimit)
        merged.resize(kAggregateDisplayLimit);

    agg.candidates = std::move(merged);
    agg.ok         = !agg.candidates.empty();
    agg.partial    = any_fail && (any_ok || !agg.candidates.empty());

    if (!agg.ok) {
        for (const auto& pr : agg.per_provider) {
            if (!pr.error.empty()) {
                agg.error = pr.error;
                break;
            }
        }
        if (agg.error.empty())
            agg.error = AiLocale::text("No models found. Try different keywords or paste a model link.",
                                       "모델을 찾지 못했습니다. 다른 검색어로 시도하거나 모델 링크를 붙여넣어 주세요.").utf8_string();
    }

    const auto t1 = std::chrono::steady_clock::now();
    agg.total_latency_ms =
        static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
    return agg;
}

void ModelSearchService::search_all_providers(const std::string&           user_text,
                                              ModelSearchResultCallback    callback,
                                              const ModelSearchContext&    ctx,
                                              const CandidateRankerConfig& rank_cfg)
{
    const uint64_t generation = ++m_generation;

    // `this` is the process-lifetime singleton, so capturing it is safe.
    std::thread([this, user_text, cb = std::move(callback), ctx, rank_cfg, generation]() {
        // Worker thread: translation (blocking Ollama call) is allowed here.
        const ModelSearchQuery     query = ModelSearchService::build_query(user_text, /*allow_translation=*/true);
        ModelSearchAggregateResult agg   = this->search_sync(query, ctx, rank_cfg);

        if (wxGetApp().is_closing())
            return; // shutting down: never post to a dying main loop
        wxGetApp().CallAfter([this, cb, agg, generation]() {
            if (wxGetApp().is_closing())
                return;
            if (generation != m_generation.load())
                return;
            if (cb)
                cb(agg);
        });
    }).detach();
}

void ModelSearchService::cancel_pending()
{
    ++m_generation;
}

ModelImportPayload ModelSearchService::resolve_import(const ModelCandidate& candidate) const
{
    if (ModelProviderPtr p = find_provider(candidate.provider_id))
        return p->resolve_import(candidate);

    ModelImportPayload out;
    out.provider_id = candidate.provider_id;
    out.error       = AiLocale::text("No provider is available to import this model.",
                                     "이 모델을 가져올 수 있는 제공자가 없습니다.").utf8_string();
    return out;
}

ModelImportPayload ModelSearchService::resolve_import_from_url(const std::string& url) const
{
    const std::vector<ModelProviderPtr> providers = enabled_providers();

    for (const auto& p : providers) {
        if (p && p->capabilities().can_import_from_url && p->is_provider_url(url))
            return p->resolve_import_from_url(url);
    }

    // Fallback: MakerWorld handles bare/direct links (e.g. *.3mf) not matched above.
    if (ModelProviderPtr mw = find_provider(ModelProviderId::MakerWorld))
        return mw->resolve_import_from_url(url);

    ModelImportPayload out;
    out.error = AiLocale::text("This link is not from a supported model provider.",
                               "지원하지 않는 모델 제공자의 링크입니다.").utf8_string();
    return out;
}

std::string ModelSearchService::search_page_url(const ModelSearchQuery& query, ModelProviderId provider) const
{
    if (ModelProviderPtr p = find_provider(provider))
        return p->search_page_url(query);
    return {};
}

}} // namespace Slic3r::GUI

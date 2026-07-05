#ifndef slic3r_ModelSearchService_hpp_
#define slic3r_ModelSearchService_hpp_

#include "CandidateRanker.hpp"
#include "IModelProvider.hpp"
#include "ModelSearchTypes.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

// Central registry + orchestrator for provider-abstracted model search.
//
// Foundation A registers MakerWorldProvider by default and leaves a clean seam
// for additional providers (Printables/Thingiverse) to be registered later
// without touching call sites. This module is NOT yet wired into the app UI.
class ModelSearchService
{
public:
    static ModelSearchService& instance();

    // Registration is additive and thread-safe. Providers are held as
    // shared_ptr<const IModelProvider> so they are safe to share with workers.
    void                          register_provider(ModelProviderPtr provider);
    std::vector<ModelProviderPtr> enabled_providers() const;

    // Query building. allow_translation must be false on the main thread (see
    // ModelSearchQueryBuilder). build_context() MUST run on the main thread.
    static ModelSearchQuery   build_query(const std::string& user_text, bool allow_translation = false);
    static ModelSearchContext build_context();

    // Synchronous fan-out across search-capable providers with per-provider and
    // aggregate timeouts, then merge + dedupe + rank. Safe to call off the main
    // thread; performs no wx access itself.
    ModelSearchAggregateResult search_sync(const ModelSearchQuery&      query,
                                           const ModelSearchContext&    ctx,
                                           const CandidateRankerConfig& rank_cfg) const;

    // Asynchronous fan-out. Spawns a worker that builds the query (with
    // translation), runs search_sync, and marshals the result to the main thread
    // via wxGetApp().CallAfter, guarded by is_closing() and a generation match
    // for cancellation (mirrors MakerWorldSearchService::search_async).
    void search_all_providers(const std::string&        user_text,
                              ModelSearchResultCallback callback,
                              const ModelSearchContext& ctx,
                              const CandidateRankerConfig& rank_cfg = CandidateRankerConfig::defaults());

    // Invalidate in-flight async searches; their completion callbacks are dropped.
    void cancel_pending();

    ModelImportPayload resolve_import(const ModelCandidate& candidate) const;
    ModelImportPayload resolve_import_from_url(const std::string& url) const;

    std::string search_page_url(const ModelSearchQuery& query,
                                ModelProviderId         provider = ModelProviderId::MakerWorld) const;

private:
    ModelSearchService();
    ModelSearchService(const ModelSearchService&)            = delete;
    ModelSearchService& operator=(const ModelSearchService&) = delete;

    ModelProviderPtr find_provider(ModelProviderId id) const;

    std::vector<ModelProviderSearchResult> fan_out(const std::vector<ModelProviderPtr>& providers,
                                                   const ModelSearchQuery&              query,
                                                   const ModelSearchContext&            ctx) const;

    mutable std::mutex            m_mutex;
    std::vector<ModelProviderPtr> m_providers;
    std::atomic<uint64_t>         m_generation{0};
};

}} // namespace Slic3r::GUI

#endif

#ifndef slic3r_IModelProvider_hpp_
#define slic3r_IModelProvider_hpp_

#include "ModelSearchTypes.hpp"

#include <memory>
#include <string>

namespace Slic3r { namespace GUI {

// Abstract model-catalog provider. Implementations wrap a concrete backend
// (MakerWorld today; Printables/Thingiverse later) behind a uniform surface so
// ModelSearchService can fan out and merge results.
//
// Threading contract:
//  - search_sync() runs on a WORKER thread. It MUST NOT touch wxWidgets,
//    GUI_App, or NetworkAgent directly. Everything main-thread-only must be
//    captured into ModelSearchContext on the main thread beforehand.
//  - prefetch() may spawn its own background work; it must be safe to call
//    from any thread and return quickly.
//  - resolve_import(), resolve_import_from_url(), search_page_url(),
//    is_provider_url() follow the wrapped backend's existing threading model
//    (MakerWorld invokes these from the main thread today).
class IModelProvider
{
public:
    virtual ~IModelProvider() = default;

    virtual ModelProviderId           provider_enum() const = 0;
    virtual std::string               provider_id() const = 0;   // stable lowercase key
    virtual std::string               display_name() const = 0;
    virtual ModelProviderCapabilities capabilities() const = 0;

    /** Worker-thread search. No wx / GUI_App / NetworkAgent access. */
    virtual ModelProviderSearchResult search_sync(const ModelSearchQuery&   query,
                                                  const ModelSearchContext& ctx) const = 0;

    /** Warm any provider-side caches in the background. Non-blocking. */
    virtual void prefetch(const ModelSearchContext& ctx) const = 0;

    virtual ModelImportPayload resolve_import(const ModelCandidate& candidate) const = 0;
    virtual ModelImportPayload resolve_import_from_url(const std::string& url) const = 0;

    /** Browser fallback search page for this provider. */
    virtual std::string search_page_url(const ModelSearchQuery& query) const = 0;

    /** True when the URL belongs to this provider (page or direct download). */
    virtual bool is_provider_url(const std::string& url) const = 0;
};

using ModelProviderPtr = std::shared_ptr<const IModelProvider>;

}} // namespace Slic3r::GUI

#endif

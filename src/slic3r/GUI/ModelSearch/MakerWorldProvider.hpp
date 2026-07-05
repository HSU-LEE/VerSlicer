#ifndef slic3r_MakerWorldProvider_hpp_
#define slic3r_MakerWorldProvider_hpp_

#include "IModelProvider.hpp"
#include "../MakerWorld/MakerWorldTypes.hpp"

namespace Slic3r { namespace GUI {

// IModelProvider adapter over the existing MakerWorldSearchService. Non-invasive:
// it only CALLS the MakerWorld backend and converts between MakerWorldCandidate
// and ModelCandidate. No MakerWorld source is modified.
class MakerWorldProvider final : public IModelProvider
{
public:
    ModelProviderId           provider_enum() const override { return ModelProviderId::MakerWorld; }
    std::string               provider_id() const override { return "makerworld"; }
    std::string               display_name() const override { return "MakerWorld"; }
    ModelProviderCapabilities capabilities() const override;

    ModelProviderSearchResult search_sync(const ModelSearchQuery&   query,
                                          const ModelSearchContext& ctx) const override;

    void prefetch(const ModelSearchContext& ctx) const override;

    ModelImportPayload resolve_import(const ModelCandidate& candidate) const override;
    ModelImportPayload resolve_import_from_url(const std::string& url) const override;

    std::string search_page_url(const ModelSearchQuery& query) const override;
    bool        is_provider_url(const std::string& url) const override;

    // Lossless conversions between the two candidate representations.
    static ModelCandidate      to_model_candidate(const MakerWorldCandidate& mw);
    static MakerWorldCandidate to_makerworld_candidate(const ModelCandidate& mc);

    // Convert a snapshotted ModelSearchContext to the MakerWorld variant.
    static MakerWorldSearchContext to_makerworld_context(const ModelSearchContext& ctx);
};

}} // namespace Slic3r::GUI

#endif

#include "MakerWorldProvider.hpp"

#include "../MakerWorld/MakerWorldSearchService.hpp"
#include "../MakerWorld/MakerWorldUrl.hpp"

#include "../OllamaAssistant/AiLocale.hpp"

namespace Slic3r { namespace GUI {

namespace {

// MakerWorldSearchService composes its search errors with _u8L, but the ko_KR
// catalog does not yet carry the AI-surface strings (see AiLocale.hpp), so a
// Korean UI would show them in English. Map the known messages onto the
// bilingual dual path here, at the provider boundary, so every ModelSearch
// consumer benefits. If a string is unknown — or arrives already translated by
// a future catalog — it passes through untouched.
std::string localize_search_error(const std::string& err)
{
    struct Entry { const char* en; const char* ko; };
    static const Entry kKnown[] = {
        {"No models found on MakerWorld. Try different keywords or paste a model link.",
         "MakerWorld에서 모델을 찾지 못했습니다. 다른 검색어로 시도하거나 모델 링크를 붙여넣어 주세요."},
        {"Sign in to Bambu Cloud to search MakerWorld, or paste a model link.",
         "MakerWorld를 검색하려면 Bambu Cloud에 로그인하거나 모델 링크를 붙여넣어 주세요."},
        {"Enter a search term or paste a MakerWorld link.",
         "검색어를 입력하거나 MakerWorld 링크를 붙여넣어 주세요."},
        {"Please describe the model more specifically (e.g. size, articulated, vase).",
         "모델을 좀 더 구체적으로 설명해 주세요 (예: 크기, 관절형, 화병)."},
    };
    for (const Entry& e : kKnown) {
        if (err == e.en)
            return AiLocale::text(e.en, e.ko).utf8_string();
    }
    return err;
}

} // namespace

ModelProviderCapabilities MakerWorldProvider::capabilities() const
{
    ModelProviderCapabilities caps;
    caps.can_search                   = true;
    caps.can_import                   = true;
    caps.can_import_from_url          = true;
    // Search degrades gracefully (staffpick pool) without login; downloads of
    // most designs require Bambu Cloud auth.
    caps.requires_login_for_search    = false;
    caps.requires_login_for_download  = true;
    caps.provides_download_count      = true;
    caps.provides_license             = true;
    // Signals MakerWorld does not surface in candidate payloads today.
    caps.provides_likes               = false;
    caps.provides_update_date         = false;
    caps.provides_success_rate        = false;
    caps.provides_ai_confidence       = false;
    caps.provides_print_time          = false;
    caps.provides_difficulty          = false;
    caps.provides_support_flag        = false;
    return caps;
}

ModelCandidate MakerWorldProvider::to_model_candidate(const MakerWorldCandidate& mw)
{
    ModelCandidate c;
    c.provider_id    = ModelProviderId::MakerWorld;
    c.id             = mw.design_id;
    c.canonical_key  = mw.design_id.empty() ? std::string{} : ("makerworld:" + mw.design_id);
    c.title          = mw.title;
    c.author         = mw.author;
    c.thumbnail_url  = mw.cover_url;
    c.license        = mw.license;
    c.downloads      = mw.download_count;
    c.login_required = mw.login_required;
    c.download_url   = mw.download_url;
    c.filename       = mw.filename;
    c.model_id       = mw.model_id;
    c.profile_id     = mw.profile_id;
    // MakerWorld does not populate these; leave as "unknown" sentinels.
    c.likes          = 0;
    c.success_rate   = -1.0;
    c.ai_confidence  = -1.0;
    return c;
}

MakerWorldCandidate MakerWorldProvider::to_makerworld_candidate(const ModelCandidate& mc)
{
    MakerWorldCandidate mw;
    mw.design_id      = mc.id;
    mw.model_id       = mc.model_id;
    mw.profile_id     = mc.profile_id;
    mw.title          = mc.title;
    mw.author         = mc.author;
    mw.cover_url      = mc.thumbnail_url;
    mw.license        = mc.license;
    mw.download_url   = mc.download_url;
    mw.filename       = mc.filename;
    mw.download_count = mc.downloads;
    mw.login_required = mc.login_required;
    return mw;
}

MakerWorldSearchContext MakerWorldProvider::to_makerworld_context(const ModelSearchContext& ctx)
{
    MakerWorldSearchContext mw;
    mw.locale                    = ctx.locale;
    mw.country_code              = ctx.country_code;
    mw.printer_model             = ctx.printer_model;
    mw.user_logged_in            = ctx.user_logged_in;
    mw.network_agent_ok          = ctx.network_agent_ok;
    mw.plugin_search_available   = ctx.plugin_search_available;
    mw.plugin_download_available = ctx.plugin_download_available;
    return mw;
}

ModelProviderSearchResult MakerWorldProvider::search_sync(const ModelSearchQuery&   query,
                                                          const ModelSearchContext& ctx) const
{
    ModelProviderSearchResult out;
    out.provider_id = ModelProviderId::MakerWorld;

    // Pass the raw user text so MakerWorldSearchService runs its own
    // normalize/translate/variants exactly as in the legacy path (parity).
    const std::string query_text = query.raw_text.empty() ? query.normalized_text : query.raw_text;

    const MakerWorldSearchContext mw_ctx = to_makerworld_context(ctx);
    const MakerWorldSearchResult  r      = MakerWorldSearchService::search_sync(query_text, mw_ctx);

    out.ok         = r.ok;
    out.error      = localize_search_error(r.error);
    out.latency_ms = r.latency_ms;
    out.source     = r.source;
    out.candidates.reserve(r.candidates.size());
    for (const auto& mw : r.candidates)
        out.candidates.push_back(to_model_candidate(mw));
    return out;
}

void MakerWorldProvider::prefetch(const ModelSearchContext& /*ctx*/) const
{
    // MakerWorldSearchService::prefetch_staffpick_pool spawns its own worker and
    // rebuilds context internally; ctx is not required here.
    MakerWorldSearchService::prefetch_staffpick_pool();
}

ModelImportPayload MakerWorldProvider::resolve_import(const ModelCandidate& candidate) const
{
    const MakerWorldCandidate     mw      = to_makerworld_candidate(candidate);
    const MakerWorldImportPayload payload = MakerWorldSearchService::resolve_import(mw);

    ModelImportPayload out;
    out.provider_id     = ModelProviderId::MakerWorld;
    out.ok              = payload.ok;
    out.error           = payload.error;
    out.download_info   = payload.download_info;
    out.detail_page_url = payload.detail_page_url;
    return out;
}

ModelImportPayload MakerWorldProvider::resolve_import_from_url(const std::string& url) const
{
    const MakerWorldImportPayload payload = MakerWorldSearchService::resolve_import_from_url(url);

    ModelImportPayload out;
    out.provider_id     = ModelProviderId::MakerWorld;
    out.ok              = payload.ok;
    out.error           = payload.error;
    out.download_info   = payload.download_info;
    out.detail_page_url = payload.detail_page_url;
    return out;
}

std::string MakerWorldProvider::search_page_url(const ModelSearchQuery& query) const
{
    const std::string q = query.normalized_text.empty() ? query.raw_text : query.normalized_text;
    return MakerWorldSearchService::makerworld_search_page_url(q);
}

bool MakerWorldProvider::is_provider_url(const std::string& url) const
{
    return is_makerworld_host_url(url) || text_contains_makerworld_link(url);
}

}} // namespace Slic3r::GUI

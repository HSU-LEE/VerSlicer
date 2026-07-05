#ifndef slic3r_MakerWorldSearchService_hpp_
#define slic3r_MakerWorldSearchService_hpp_

#include "MakerWorldTypes.hpp"

#include "slic3r/Utils/Http.hpp"

#include <functional>
#include <string>

class wxString;
class wxWindow;

namespace Slic3r { namespace GUI {

class MakerWorldSearchService
{
public:
    static MakerWorldSearchContext build_context();

    static MakerWorldSearchResult search_sync(const std::string& query, const MakerWorldSearchContext& ctx);

    static void search_async(const std::string& query, const MakerWorldSearchContext& ctx,
                             std::function<void(MakerWorldSearchResult)> callback);

    /** Warm staffpick pool in background (no UI). Safe to call repeatedly. */
    static void prefetch_staffpick_pool();

    static MakerWorldImportPayload resolve_import(const MakerWorldCandidate& candidate);
    static MakerWorldImportPayload resolve_import_from_url(const std::string& url);
    static MakerWorldImportPayload resolve_import_from_design_id(const std::string& design_id);

    /** Resolve MakerWorld page / .3mf URL to download_info for Plater. */
    static std::string resolve_download_info(const std::string& url);

    static void apply_download_http_headers(Slic3r::Http& http);
    static bool download_url_needs_auth(const std::string& url);

    /** Strip search boilerplate from user chat text. */
    static std::string normalize_search_query(const std::string& user_text);

    /** MakerWorld web search URL (browser fallback). */
    static std::string makerworld_search_page_url(const std::string& query);
};

}} // namespace

#endif

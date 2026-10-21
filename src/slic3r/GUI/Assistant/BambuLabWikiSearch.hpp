#ifndef slic3r_BambuLabWikiSearch_hpp_
#define slic3r_BambuLabWikiSearch_hpp_

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

struct BambuWikiPageHit
{
    std::string id;
    std::string title;
    std::string path;
    std::string description;
    std::string url;
};

struct BambuWikiArticle
{
    std::string title;
    std::string path;
    std::string url;
    std::string excerpt;
};

/** Search + fetch excerpts from wiki.bambulab.com (Wiki.js GraphQL + public HTML). */
class BambuLabWikiSearch
{
public:
    static std::string wiki_locale(bool ko_ui);

    /** Map user symptom text to English-friendly wiki search terms. */
    static std::string normalize_search_query(const std::string& user_request, bool ko_ui);

    static std::vector<BambuWikiPageHit> search_pages(const std::string& query, const std::string& locale,
                                                      size_t limit = 5);

    /** Fetch readable excerpt from a wiki page path (HTML → plain text). */
    static std::string fetch_page_excerpt(const std::string& path, const std::string& locale,
                                          size_t max_chars = 1800);

    /** Search top pages and fetch excerpts for LLM context. */
    static nlohmann::json build_wiki_context(const std::string& user_request, bool ko_ui, size_t max_pages = 2,
                                             size_t max_chars_per_page = 1600);

    /** Merge wiki excerpts from multiple search queries (diagnosis step). */
    static nlohmann::json build_wiki_context_from_queries(const std::vector<std::string>& queries, bool ko_ui,
                                                          size_t max_pages = 2, size_t max_chars_per_page = 1400);

    /** Strip HTML to plain text (testable). */
    static std::string html_to_plain_text(const std::string& html, size_t max_chars = 0);
};

}} // namespace

#endif

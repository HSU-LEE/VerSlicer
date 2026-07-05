#include "BambuLabWikiSearch.hpp"

#include <boost/log/trivial.hpp>
#include "BambuLabWikiSearchCore.hpp"

#include "slic3r/Utils/Http.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include <boost/algorithm/string.hpp>

namespace Slic3r { namespace GUI {

namespace {

constexpr const char* kWikiHost = "https://wiki.bambulab.com";

using namespace BambuLabWikiSearchCore;

struct WikiCacheEntry
{
    nlohmann::json                        context;
    std::chrono::steady_clock::time_point at;
};

std::mutex& wiki_cache_mutex()
{
    static std::mutex m;
    return m;
}

std::unordered_map<std::string, WikiCacheEntry>& wiki_cache()
{
    static std::unordered_map<std::string, WikiCacheEntry> c;
    return c;
}

std::string cache_key(const std::string& query, const std::string& locale, size_t max_pages)
{
    return locale + "|" + query + "|" + std::to_string(max_pages);
}

std::string http_post_json_sync(const std::string& url, const std::string& body)
{
    std::string response;
    std::string error;
    Http::post(url)
        .header("Content-Type", "application/json")
        .header("Accept", "application/json")
        .timeout_connect(5)
        .timeout_max(20)
        .set_post_body(body)
        .on_error([&](std::string body_in, std::string err, unsigned) {
            response = std::move(body_in);
            error    = std::move(err);
        })
        .on_complete([&](std::string body_in, unsigned) { response = std::move(body_in); })
        .perform_sync();
    if (!error.empty() && response.empty())
        return {};
    (void) error;
    return response;
}

std::string http_get_sync(const std::string& url)
{
    std::string response;
    std::string error;
    Http::get(url)
        .header("Accept", "text/html,application/xhtml+xml")
        .timeout_connect(5)
        .timeout_max(25)
        .on_error([&](std::string body_in, std::string err, unsigned) {
            response = std::move(body_in);
            error    = std::move(err);
        })
        .on_complete([&](std::string body_in, unsigned) { response = std::move(body_in); })
        .perform_sync();
    if (!error.empty() && response.empty())
        return {};
    (void) error;
    return response;
}

} // namespace

std::string BambuLabWikiSearch::wiki_locale(bool ko_ui) { return BambuLabWikiSearchCore::wiki_locale(ko_ui); }

std::string BambuLabWikiSearch::normalize_search_query(const std::string& user_request, bool ko_ui)
{
    return BambuLabWikiSearchCore::normalize_search_query(user_request, ko_ui);
}

std::string BambuLabWikiSearch::html_to_plain_text(const std::string& html, size_t max_chars)
{
    return BambuLabWikiSearchCore::html_to_plain_text(html, max_chars);
}

std::vector<BambuWikiPageHit> BambuLabWikiSearch::search_pages(const std::string& query, const std::string& locale,
                                                               size_t limit)
{
    std::vector<BambuWikiPageHit> hits;
    if (query.empty())
        return hits;

    const std::string gql =
        std::string("{ pages { search(query: \"") + escape_graphql_string(query) + "\", locale: \"" +
        escape_graphql_string(locale) + "\") { results { id title path description } } } }";
    const nlohmann::json body = {{"query", gql}};

    const std::string response = http_post_json_sync(std::string(kWikiHost) + "/graphql", body.dump());
    if (response.empty())
        return hits;

    try {
        const nlohmann::json root = nlohmann::json::parse(response);
        const auto&          results =
            root.at("data").at("pages").at("search").at("results");
        for (const auto& r : results) {
            if (!r.is_object())
                continue;
            BambuWikiPageHit hit;
            hit.id          = r.value("id", "");
            hit.title       = r.value("title", "");
            hit.path        = r.value("path", "");
            hit.description = r.value("description", "");
            if (hit.path.empty())
                continue;
            hit.url = std::string(kWikiHost) + "/" + locale + "/" + hit.path;
            hits.push_back(std::move(hit));
            if (hits.size() >= limit)
                break;
        }
    } catch (...) {
        BOOST_LOG_TRIVIAL(warning) << "[BambuWiki] search result JSON parse failed; returning "
                                   << hits.size() << " hits";
    }

    if (hits.empty() && locale != "en")
        return search_pages(query, "en", limit);
    return hits;
}

std::string BambuLabWikiSearch::fetch_page_excerpt(const std::string& path, const std::string& locale,
                                                   size_t max_chars)
{
    if (path.empty())
        return {};
    const std::string url  = std::string(kWikiHost) + "/" + locale + "/" + path;
    const std::string html = http_get_sync(url);
    if (html.empty())
        return {};
    return html_to_plain_text(html, max_chars);
}

nlohmann::json BambuLabWikiSearch::build_wiki_context(const std::string& user_request, bool ko_ui, size_t max_pages,
                                                      size_t max_chars_per_page)
{
    const std::string locale = wiki_locale(ko_ui);
    const std::string query  = normalize_search_query(user_request, ko_ui);
    if (query.empty())
        return nlohmann::json::array();

    {
        std::lock_guard<std::mutex> lock(wiki_cache_mutex());
        const auto                  key = cache_key(query, locale, max_pages);
        const auto                    it  = wiki_cache().find(key);
        if (it != wiki_cache().end()) {
            const auto age = std::chrono::steady_clock::now() - it->second.at;
            if (age < std::chrono::minutes(30))
                return it->second.context;
        }
    }

    nlohmann::json articles = nlohmann::json::array();
    const auto     hits     = search_pages(query, locale, std::max(max_pages, size_t(3)));

    size_t added = 0;
    for (const BambuWikiPageHit& hit : hits) {
        if (added >= max_pages)
            break;
        std::string excerpt = fetch_page_excerpt(hit.path, locale, max_chars_per_page);
        if (excerpt.size() < 80 && locale != "en")
            excerpt = fetch_page_excerpt(hit.path, "en", max_chars_per_page);
        if (excerpt.size() < 40)
            continue;
        nlohmann::json item;
        item["title"]   = hit.title;
        item["url"]     = hit.url;
        item["path"]    = hit.path;
        item["excerpt"] = excerpt;
        articles.push_back(std::move(item));
        ++added;
    }

    {
        std::lock_guard<std::mutex> lock(wiki_cache_mutex());
        wiki_cache()[cache_key(query, locale, max_pages)] = {articles, std::chrono::steady_clock::now()};
    }
    return articles;
}

nlohmann::json BambuLabWikiSearch::build_wiki_context_from_queries(const std::vector<std::string>& queries, bool ko_ui,
                                                                   size_t max_pages, size_t max_chars_per_page)
{
    nlohmann::json merged   = nlohmann::json::array();
    std::unordered_set<std::string> seen_paths;

    for (const std::string& q : queries) {
        if (merged.size() >= max_pages)
            break;
        const std::string trimmed = boost::trim_copy(q);
        if (trimmed.empty())
            continue;
        const nlohmann::json batch = build_wiki_context(trimmed, ko_ui, max_pages, max_chars_per_page);
        if (!batch.is_array())
            continue;
        for (const auto& item : batch) {
            if (!item.is_object())
                continue;
            const std::string path = item.value("path", "");
            const std::string dedupe_key = path.empty() ? item.value("url", "") : path;
            if (!dedupe_key.empty() && !seen_paths.insert(dedupe_key).second)
                continue;
            merged.push_back(item);
            if (merged.size() >= max_pages)
                break;
        }
    }

    if (merged.empty() && !queries.empty())
        return build_wiki_context(queries.front(), ko_ui, max_pages, max_chars_per_page);
    return merged;
}

}} // namespace

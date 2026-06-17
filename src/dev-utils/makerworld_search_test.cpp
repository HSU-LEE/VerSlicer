#include "../slic3r/GUI/MakerWorld/MakerWorldSearchCore.hpp"
#include "../slic3r/GUI/MakerWorld/MakerWorldUrl.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace Slic3r::GUI;

static int g_failures = 0;

static void expect_true(bool cond, const char* label)
{
    if (!cond) {
        std::cerr << "FAIL: " << label << '\n';
        ++g_failures;
    }
}

static void expect_eq(const std::string& got, const std::string& want, const char* label)
{
    if (got != want) {
        std::cerr << "FAIL: " << label << " got='" << got << "' want='" << want << "'\n";
        ++g_failures;
    }
}

int main()
{
    expect_eq(parse_design_id_from_url("https://makerworld.com/en/models/12345"), "12345",
              "parse_design_id_numeric");
    expect_eq(parse_design_id_from_url("https://makerworld.com/en/models/12345-articulated-dragon"), "12345",
              "parse_design_id_slug");
    expect_eq(parse_profile_id_from_url("https://makerworld.com/en/models/99#profileId-42"), "42",
              "parse_profile_id_from_url");
    expect_eq(absolute_makerworld_browser_url("api/sign-in/ticket?to=%2Fko%2Fmodels%2F1"),
              "https://makerworld.com/api/sign-in/ticket?to=%2Fko%2Fmodels%2F1", "absolute_browser_url");
    expect_true(is_makerworld_host_url("https://makerworld.com/en/models/1"), "is_makerworld_host_url");
    expect_true(!is_makerworld_host_url("https://example.com/models/1"), "not_makerworld_host");

    const std::string json = R"({
        "hits": [
            {"design": {"designId": "42", "designTitle": "Keycap", "login_required": true}},
            {"id": "7", "title": "Dragon"},
            {"id": "7", "title": "Dragon Duplicate"}
        ]
    })";
    const auto hits = parse_hits_json(json);
    expect_true(hits.size() == 3, "parse_hits_json count");
    expect_eq(hits[0].design_id, "42", "parse_hits designId");
    expect_eq(hits[0].title, "Keycap", "parse_hits designTitle");
    expect_true(hits[0].login_required, "parse_hits login_required");

    std::vector<MakerWorldCandidate> pool;
    MakerWorldCandidate a;
    a.design_id = "1";
    a.title     = "Articulated Dragon";
    a.download_count = 5000;
    pool.push_back(a);
    MakerWorldCandidate b;
    b.design_id = "2";
    b.title     = "Phone Stand";
    pool.push_back(b);
    MakerWorldCandidate c;
    c.design_id = "3";
    c.title     = "Dragon Mini";
    c.download_count = 12000;
    pool.push_back(c);

    const auto filtered = filter_by_query(pool, "dragon");
    expect_true(filtered.size() >= 1, "filter_by_query");
    expect_true(filtered[0].title.find("Dragon") != std::string::npos || filtered[0].title.find("dragon") != std::string::npos,
                "filter_by_query top has dragon");

    const auto ranked = rank_and_dedupe(hits, "dragon", 2, true);
    expect_true(ranked.size() == 2, "rank_and_dedupe limit");
    expect_eq(ranked[0].design_id, "7", "rank_and_dedupe dragon first");

    std::vector<MakerWorldCandidate> dup = hits;
    dup.push_back(hits[1]);
    const auto deduped = rank_and_dedupe(std::move(dup), "dragon", 8, true);
    expect_true(deduped.size() == 2, "rank_and_dedupe dedupe");

    const auto merged = merge_candidates(pool, filtered);
    expect_true(merged.size() >= 2, "merge_candidates");

    const auto variants = search_query_variants("키캡", "keycap");
    expect_true(variants.size() >= 2, "search_query_variants with translation");
    bool has_keycap = false;
    for (const auto& v : variants) {
        if (v == "keycap")
            has_keycap = true;
    }
    expect_true(has_keycap, "search_query_variants translated english");

    const auto stripped = search_query_variants("articulated dragon mini");
    bool has_broader = false;
    for (const auto& v : stripped) {
        if (v.find("dragon") != std::string::npos && v.find("mini") == std::string::npos)
            has_broader = true;
    }
    expect_true(has_broader, "search_query_variants qualifier strip");

    const std::string sanitized = sanitize_search_text("dragon\u200B\u200C");
    expect_eq(sanitized, "dragon", "sanitize_search_text zero_width");

    std::vector<MakerWorldCandidate> or_pool;
    MakerWorldCandidate x;
    x.design_id = "10";
    x.title     = "Articulated Cat";
    or_pool.push_back(x);
    MakerWorldCandidate y;
    y.design_id = "11";
    y.title     = "Phone Holder";
    or_pool.push_back(y);
    const auto or_result = filter_by_query_scored(or_pool, "cat phone", 8);
    expect_true(or_result.size() >= 1, "filter_by_query_scored or_fallback");

    expect_eq(normalize_makerworld_search_query("MakerWorld에서 articulated dragon 모델 찾아줘"),
              "articulated dragon", "normalize strips filler");
    expect_eq(normalize_makerworld_search_query("find me a \"keycap cat\""), "keycap cat",
              "normalize quoted phrase");
    expect_eq(normalize_makerworld_search_query("키캡 검색해줘"), "키캡", "normalize korean filler");

    std::vector<MakerWorldCandidate> word_pool;
    MakerWorldCandidate dragonfly;
    dragonfly.design_id = "20";
    dragonfly.title     = "Dragonfly Ornament";
    word_pool.push_back(dragonfly);
    MakerWorldCandidate dragon;
    dragon.design_id = "21";
    dragon.title     = "Articulated Dragon";
    dragon.download_count = 8000;
    word_pool.push_back(dragon);
    const auto word_ranked = rank_and_dedupe(word_pool, "dragon", 2, true);
    expect_true(!word_ranked.empty() && word_ranked[0].design_id == "21",
                "word_boundary ranks exact dragon first");

    if (g_failures == 0) {
        std::cout << "makerworld_search_test: all passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "makerworld_search_test: " << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
}

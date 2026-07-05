// Headless unit test for the ModelSearch CandidateRanker.
//
// Verifies that CandidateRanker::rank_in_place with the makerworld_legacy()
// profile reproduces the exact ordering of MakerWorldSearchCore::rank_and_dedupe
// (the legacy MakerWorld ranking), plus sanity-checks the normalization helpers
// and cross-provider dedupe. No wxWidgets is linked.

#include "../slic3r/GUI/ModelSearch/CandidateRanker.hpp"
#include "../slic3r/GUI/ModelSearch/ModelSearchDedupe.hpp"
#include "../slic3r/GUI/ModelSearch/ModelSearchTypes.hpp"

#include "../slic3r/GUI/MakerWorld/MakerWorldSearchCore.hpp"
#include "../slic3r/GUI/MakerWorld/MakerWorldTypes.hpp"

#include <iostream>
#include <string>
#include <vector>

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

namespace {

struct Row
{
    std::string id;
    std::string title;
    std::string author;
    int         downloads;
    bool        login;
};

std::vector<Row> sample_rows()
{
    return {
        {"1", "Articulated Dragon Flexi", "alice", 12000, false},
        {"2", "Dragon keychain", "bob", 500, false},
        {"3", "articulated dragon (login only)", "carol", 40000, true},
        {"4", "Cute cat planter", "dave", 8000, false},
        {"5", "Articulated Dragon V2", "erin", 3000, false},
        {"6", "dragon", "frank", 100, false},
    };
}

MakerWorldCandidate to_mw(const Row& r)
{
    MakerWorldCandidate c;
    c.design_id      = r.id;
    c.title          = r.title;
    c.author         = r.author;
    c.download_count = r.downloads;
    c.login_required = r.login;
    return c;
}

ModelCandidate to_model(const Row& r)
{
    ModelCandidate c;
    c.provider_id    = ModelProviderId::MakerWorld;
    c.id             = r.id;
    c.canonical_key  = "makerworld:" + r.id;
    c.title          = r.title;
    c.author         = r.author;
    c.downloads      = r.downloads;
    c.login_required = r.login;
    return c;
}

std::vector<std::string> legacy_order(const std::string& query)
{
    std::vector<MakerWorldCandidate> in;
    for (const auto& r : sample_rows())
        in.push_back(to_mw(r));
    // deprioritize_login_required == true (unauthenticated legacy default).
    const auto ranked = rank_and_dedupe(std::move(in), query, 100, true);
    std::vector<std::string> ids;
    for (const auto& c : ranked)
        ids.push_back(c.design_id);
    return ids;
}

std::vector<std::string> ranker_order(const std::string& query)
{
    std::vector<ModelCandidate> in;
    for (const auto& r : sample_rows())
        in.push_back(to_model(r));

    ModelSearchQuery q;
    q.raw_text        = query;
    q.normalized_text = query;

    CandidateRanker::rank_in_place(in, q, CandidateRankerConfig::makerworld_legacy());
    std::vector<std::string> ids;
    for (const auto& c : in)
        ids.push_back(c.id);
    return ids;
}

void test_legacy_parity()
{
    for (const char* query : {"articulated dragon", "dragon", "cat planter"}) {
        const auto legacy = legacy_order(query);
        const auto ranked = ranker_order(query);
        expect_true(legacy == ranked, (std::string("legacy_parity:") + query).c_str());
        if (legacy != ranked) {
            std::cerr << "  legacy: ";
            for (const auto& s : legacy) std::cerr << s << ' ';
            std::cerr << "\n  ranker: ";
            for (const auto& s : ranked) std::cerr << s << ' ';
            std::cerr << '\n';
        }
    }
}

void test_normalizers()
{
    expect_true(CandidateRanker::normalize_downloads(0) == 0.0, "downloads_zero");
    expect_true(CandidateRanker::normalize_downloads(100) > 0.0
                    && CandidateRanker::normalize_downloads(100) <= 1.0,
                "downloads_range");
    expect_true(CandidateRanker::normalize_downloads(1000000) >= 0.99, "downloads_saturates");
    expect_true(CandidateRanker::normalize_downloads(50) < CandidateRanker::normalize_downloads(50000),
                "downloads_monotonic");

    expect_true(CandidateRanker::normalize_success_rate(-1.0) == 0.5, "success_unknown_neutral");
    expect_true(CandidateRanker::normalize_success_rate(0.9) == 0.9, "success_passthrough");
    expect_true(CandidateRanker::normalize_ai_confidence(-1.0) == 0.5, "ai_unknown_neutral");

    expect_true(CandidateRanker::normalize_license("CC0") == 1.0, "license_cc0");
    expect_true(CandidateRanker::normalize_license("") == 0.5, "license_unknown_neutral");
    expect_true(CandidateRanker::normalize_license("Standard Digital File License") == 0.2,
                "license_standard_restrictive");
    expect_true(CandidateRanker::normalize_license("CC-BY") > CandidateRanker::normalize_license("CC-BY-NC"),
                "license_permissive_gt_noncommercial");
}

void test_dedupe()
{
    std::vector<ModelCandidate> in;

    ModelCandidate a;
    a.provider_id   = ModelProviderId::MakerWorld;
    a.id            = "100";
    a.canonical_key = "makerworld:100";
    a.title         = "Benchy Boat";
    a.author        = "creativetools";
    a.downloads     = 5000;
    in.push_back(a);

    // Exact canonical duplicate carrying an import hint the first lacked.
    ModelCandidate a_dup = a;
    a_dup.download_url = "https://example.com/benchy.3mf";
    a_dup.downloads    = 9000;
    in.push_back(a_dup);

    // Fuzzy duplicate: same title/author, different provider + id, no canonical match.
    ModelCandidate a_fuzzy;
    a_fuzzy.provider_id   = ModelProviderId::Printables;
    a_fuzzy.id            = "xyz";
    a_fuzzy.canonical_key = "printables:xyz";
    a_fuzzy.title         = "  benchy   boat ";
    a_fuzzy.author        = "CreativeTools";
    a_fuzzy.likes         = 42;
    in.push_back(a_fuzzy);

    // Distinct model.
    ModelCandidate b;
    b.provider_id   = ModelProviderId::MakerWorld;
    b.id            = "200";
    b.canonical_key = "makerworld:200";
    b.title         = "Calibration Cube";
    b.author        = "someone";
    in.push_back(b);

    const int removed = ModelSearchDedupe::dedupe_cross_provider(in);
    expect_true(removed == 2, "dedupe_removed_two");
    expect_true(in.size() == 2, "dedupe_kept_two");
    if (in.size() >= 1) {
        expect_eq(in[0].id, "100", "dedupe_kept_first");
        expect_eq(in[0].download_url, "https://example.com/benchy.3mf", "dedupe_enriched_url");
        expect_true(in[0].downloads == 9000, "dedupe_max_downloads");
        expect_true(in[0].likes == 42, "dedupe_merged_likes");
    }

    expect_eq(ModelSearchDedupe::normalize_identity("  Benchy Boat! "), "benchyboat", "normalize_identity");
}

} // namespace

int main()
{
    test_legacy_parity();
    test_normalizers();
    test_dedupe();

    if (g_failures == 0)
        std::cout << "model_search_ranker_test: all checks passed\n";
    else
        std::cerr << "model_search_ranker_test: " << g_failures << " failure(s)\n";
    return g_failures == 0 ? 0 : 1;
}

#include "CandidateRanker.hpp"

#include "../MakerWorld/MakerWorldSearchCore.hpp"
#include "../MakerWorld/MakerWorldTypes.hpp"

#include "libslic3r/AppConfig.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace Slic3r { namespace GUI {

namespace {

// Rescale factor for the legacy integer text score into [0..1]. A strong exact
// title match (phrase 1500 + token bonuses + prefix/bigram + download) lands
// near this value; anything above is clamped to 1.
constexpr double kTextScoreScale = 2500.0;

// Log-scale saturation points: a candidate at this many downloads/likes maps to
// ~1.0 on the normalized curve.
constexpr double kDownloadsSaturation = 1000000.0;
constexpr double kLikesSaturation     = 100000.0;

// Recency window: <= kRecencyFreshDays maps to 1.0, decaying to 0.0 at kRecencyStaleDays.
constexpr double kRecencyFreshDays = 7.0;
constexpr double kRecencyStaleDays = 730.0;

double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

MakerWorldCandidate to_scoring_candidate(const ModelCandidate& c)
{
    MakerWorldCandidate mc;
    // Only fields consumed by MakerWorldSearchCore::score_candidate matter here.
    mc.design_id      = c.id;
    mc.title          = c.title;
    mc.author         = c.author;
    mc.download_count = c.downloads;
    mc.login_required = c.login_required;
    return mc;
}

int legacy_raw_score(const ModelCandidate&           c,
                     const std::vector<std::string>& tokens,
                     const std::string&              phrase_lower,
                     bool                            deprioritize_login_required)
{
    const MakerWorldCandidate mc = to_scoring_candidate(c);
    return score_candidate(mc, tokens, phrase_lower, deprioritize_login_required);
}

std::string format_count(int n)
{
    if (n >= 1000000) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1fM", static_cast<double>(n) / 1000000.0);
        return buf;
    }
    if (n >= 1000) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1fk", static_cast<double>(n) / 1000.0);
        return buf;
    }
    return std::to_string(n);
}

} // namespace

CandidateRankerConfig CandidateRankerConfig::defaults()
{
    return CandidateRankerConfig{};
}

CandidateRankerConfig CandidateRankerConfig::makerworld_legacy()
{
    CandidateRankerConfig c;
    c.legacy_mode = true;
    return c;
}

CandidateRankerConfig CandidateRankerConfig::from_app_config(const Slic3r::AppConfig* cfg)
{
    CandidateRankerConfig c = defaults();
    if (cfg == nullptr)
        return c;

    const std::string sec = "model_search";
    if (cfg->get(sec, "ranking_profile") == "legacy")
        return makerworld_legacy();

    auto load = [&](const char* key, double& out) {
        if (!cfg->has(sec, key))
            return;
        try {
            out = std::stod(cfg->get(sec, key));
        } catch (...) {
            // keep default on parse failure
        }
    };
    load("w_text_relevance", c.w_text_relevance);
    load("w_downloads", c.w_downloads);
    load("w_likes", c.w_likes);
    load("w_recency", c.w_recency);
    load("w_license", c.w_license);
    load("w_success_rate", c.w_success_rate);
    load("w_ai_confidence", c.w_ai_confidence);
    load("login_penalty", c.login_penalty);
    return c;
}

double CandidateRanker::normalize_downloads(int downloads)
{
    if (downloads <= 0)
        return 0.0;
    return clamp01(std::log10(1.0 + downloads) / std::log10(1.0 + kDownloadsSaturation));
}

double CandidateRanker::normalize_likes(int likes)
{
    if (likes <= 0)
        return 0.0;
    return clamp01(std::log10(1.0 + likes) / std::log10(1.0 + kLikesSaturation));
}

double CandidateRanker::normalize_recency(const std::optional<std::chrono::system_clock::time_point>& update_date)
{
    if (!update_date.has_value())
        return 0.5; // unknown -> neutral
    const auto now = std::chrono::system_clock::now();
    const double age_days =
        std::chrono::duration_cast<std::chrono::hours>(now - *update_date).count() / 24.0;
    if (age_days <= kRecencyFreshDays)
        return 1.0;
    if (age_days >= kRecencyStaleDays)
        return 0.0;
    return clamp01(1.0 - (age_days - kRecencyFreshDays) / (kRecencyStaleDays - kRecencyFreshDays));
}

double CandidateRanker::normalize_license(const std::string& license)
{
    if (license.empty())
        return 0.5; // unknown -> neutral

    std::string l = license;
    std::transform(l.begin(), l.end(), l.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    if (l.find("cc0") != std::string::npos || l.find("public domain") != std::string::npos
        || l.find("pd") != std::string::npos)
        return 1.0;
    if (l.find("by-nc") != std::string::npos || l.find("by-nd") != std::string::npos
        || l.find("noncommercial") != std::string::npos || l.find("no derivatives") != std::string::npos)
        return 0.4;
    if (l.find("by-sa") != std::string::npos || l.find("sharealike") != std::string::npos)
        return 0.6;
    if (l.find("cc-by") != std::string::npos || l.find("cc by") != std::string::npos
        || l.find("mit") != std::string::npos || l.find("apache") != std::string::npos
        || l.find("gpl") != std::string::npos)
        return 0.8;
    if (l.find("all rights") != std::string::npos || l.find("standard") != std::string::npos
        || l.find("arr") != std::string::npos)
        return 0.2;
    return 0.5;
}

double CandidateRanker::normalize_success_rate(double success_rate)
{
    if (success_rate < 0.0)
        return 0.5; // unknown -> neutral
    return clamp01(success_rate);
}

double CandidateRanker::normalize_ai_confidence(double ai_confidence)
{
    if (ai_confidence < 0.0)
        return 0.5; // unknown -> neutral
    return clamp01(ai_confidence);
}

double CandidateRanker::score_text_relevance(const ModelCandidate& candidate, const ModelSearchQuery& query)
{
    const auto  tokens = tokenize_query(query.normalized_text);
    std::string phrase = query.normalized_text;
    std::transform(phrase.begin(), phrase.end(), phrase.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    // deprioritize_login_required = false: login handling is a separate,
    // configurable term in multi-signal mode (login_penalty).
    const int raw = legacy_raw_score(candidate, tokens, phrase, false);
    return clamp01(static_cast<double>(raw) / kTextScoreScale);
}

void CandidateRanker::rank_in_place(std::vector<ModelCandidate>& candidates,
                                    const ModelSearchQuery&      query,
                                    const CandidateRankerConfig& config)
{
    if (candidates.empty())
        return;

    const auto  tokens = tokenize_query(query.normalized_text);
    std::string phrase = query.normalized_text;
    std::transform(phrase.begin(), phrase.end(), phrase.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    const bool korean = query.contains_cjk;

    for (auto& c : candidates) {
        if (config.legacy_mode) {
            // Exact legacy ordering: raw MakerWorld score with login penalty baked
            // in (deprioritize == true), tie-broken by downloads below.
            c.composite_score = static_cast<double>(legacy_raw_score(c, tokens, phrase, true));
        } else {
            const double t   = clamp01(static_cast<double>(legacy_raw_score(c, tokens, phrase, false)) / kTextScoreScale);
            const double d   = normalize_downloads(c.downloads);
            const double l   = normalize_likes(c.likes);
            const double r   = normalize_recency(c.update_date);
            const double lic = normalize_license(c.license);
            const double sr  = normalize_success_rate(c.success_rate);
            const double ai  = normalize_ai_confidence(c.ai_confidence);

            double score = config.w_text_relevance * t + config.w_downloads * d + config.w_likes * l
                         + config.w_recency * r + config.w_license * lic + config.w_success_rate * sr
                         + config.w_ai_confidence * ai;
            if (c.login_required)
                score -= config.login_penalty;
            c.composite_score = score;
        }
        c.recommendation_reason = build_recommendation_reason(c, query, korean);
    }

    // Stable sort: composite desc, download tiebreak (mirrors rank_and_dedupe).
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const ModelCandidate& a, const ModelCandidate& b) {
                         if (a.composite_score != b.composite_score)
                             return a.composite_score > b.composite_score;
                         return a.downloads > b.downloads;
                     });
}

std::string CandidateRanker::build_recommendation_reason(const ModelCandidate&   candidate,
                                                        const ModelSearchQuery& query,
                                                        bool                    korean)
{
    std::vector<std::string> reasons;

    // Title match against the query phrase.
    std::string phrase = query.normalized_text;
    std::transform(phrase.begin(), phrase.end(), phrase.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    std::string title_lower = candidate.title;
    std::transform(title_lower.begin(), title_lower.end(), title_lower.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (!phrase.empty() && title_lower.find(phrase) != std::string::npos)
        reasons.push_back(korean ? "검색어 정확 일치" : "Exact title match");

    if (candidate.downloads >= 1000)
        reasons.push_back(korean ? ("인기 다운로드 " + format_count(candidate.downloads) + "회")
                                 : ("Popular (" + format_count(candidate.downloads) + " downloads)"));

    if (candidate.likes >= 100)
        reasons.push_back(korean ? ("좋아요 " + format_count(candidate.likes)) : (format_count(candidate.likes) + " likes"));

    if (candidate.update_date.has_value() && normalize_recency(candidate.update_date) >= 0.75)
        reasons.push_back(korean ? "최근 업데이트" : "Recently updated");

    if (normalize_license(candidate.license) >= 0.8 && !candidate.license.empty())
        reasons.push_back(korean ? "자유로운 라이선스" : "Permissive license");

    if (candidate.success_rate >= 0.8)
        reasons.push_back(korean ? "높은 프린트 성공률" : "High print success rate");

    if (reasons.empty())
        return korean ? "검색 결과 추천" : "Recommended result";

    std::string out;
    const size_t kMaxReasons = 2;
    for (size_t i = 0; i < reasons.size() && i < kMaxReasons; ++i) {
        if (!out.empty())
            out += " · ";
        out += reasons[i];
    }
    return out;
}

}} // namespace Slic3r::GUI

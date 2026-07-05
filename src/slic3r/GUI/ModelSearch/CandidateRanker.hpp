#ifndef slic3r_CandidateRanker_hpp_
#define slic3r_CandidateRanker_hpp_

#include "ModelSearchTypes.hpp"

#include <string>
#include <vector>

namespace Slic3r { class AppConfig; }

namespace Slic3r { namespace GUI {

// Multi-signal ranking for ModelCandidate. All tunables live in
// CandidateRankerConfig; no ranking magic numbers are scattered in code.
//
// This translation unit is headless: it depends only on the standard library,
// MakerWorldSearchCore (headless) and libslic3r's AppConfig. It never touches
// wxWidgets, so it can be exercised from dev-utils tests.
struct CandidateRankerConfig
{
    // When true, ranking reproduces the legacy MakerWorld ordering exactly
    // (text score via MakerWorldSearchCore::score_candidate, download tiebreak,
    // login penalty baked into the text score). Multi-signal weights are ignored.
    bool legacy_mode{false};

    // Multi-signal weights (used when legacy_mode == false). Each normalized
    // signal is in [0..1]; composite is their weighted sum minus login penalty.
    double w_text_relevance{0.55};
    double w_downloads{0.15};
    double w_likes{0.08};
    double w_recency{0.07};
    double w_license{0.05};
    double w_success_rate{0.06};
    double w_ai_confidence{0.04};
    double login_penalty{0.30};

    // Balanced default profile for cross-provider ranking.
    static CandidateRankerConfig defaults();

    // Exact-parity profile: preserves current MakerWorld ordering.
    static CandidateRankerConfig makerworld_legacy();

    // Load overrides from AppConfig section "model_search" (falls back to
    // defaults() for any missing/unparseable key). cfg may be null.
    static CandidateRankerConfig from_app_config(const Slic3r::AppConfig* cfg);
};

class CandidateRanker
{
public:
    // Normalization helpers, all returning [0..1]. Exposed for testing/reuse.
    static double normalize_downloads(int downloads);
    static double normalize_likes(int likes);
    static double normalize_recency(const std::optional<std::chrono::system_clock::time_point>& update_date);
    static double normalize_license(const std::string& license);
    static double normalize_success_rate(double success_rate); // -1 => neutral 0.5
    static double normalize_ai_confidence(double ai_confidence); // -1 => neutral 0.5

    // Text relevance in [0..1] (legacy MakerWorld score rescaled). May delegate
    // to MakerWorldSearchCore::score_candidate for parity.
    static double score_text_relevance(const ModelCandidate& candidate, const ModelSearchQuery& query);

    // Rank candidates in place: fills composite_score + recommendation_reason,
    // then sorts descending (download tiebreak). Stable across equal scores.
    static void rank_in_place(std::vector<ModelCandidate>& candidates,
                              const ModelSearchQuery&      query,
                              const CandidateRankerConfig& config);

    static std::string build_recommendation_reason(const ModelCandidate&   candidate,
                                                   const ModelSearchQuery& query,
                                                   bool                    korean);
};

}} // namespace Slic3r::GUI

#endif

#ifndef slic3r_ModelSearchTypes_hpp_
#define slic3r_ModelSearchTypes_hpp_

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

// Provider-abstracted model search primitives (Phase 4a Foundation A).
//
// This header is intentionally dependency-light: it only pulls the C++ standard
// library so it can be included from headless test targets and from interface
// headers without dragging in wxWidgets or the MakerWorld backend.

enum class ModelProviderId {
    Unknown = 0,
    MakerWorld,
    Printables,   // registry seam only (no network implementation in Foundation A)
    Thingiverse,  // registry seam only (no network implementation in Foundation A)
};

/** Stable lowercase key for a provider (config keys, canonical_key prefix, telemetry). */
inline std::string model_provider_key(ModelProviderId id)
{
    switch (id) {
    case ModelProviderId::MakerWorld:  return "makerworld";
    case ModelProviderId::Printables:  return "printables";
    case ModelProviderId::Thingiverse: return "thingiverse";
    case ModelProviderId::Unknown:
    default:                           return "unknown";
    }
}

inline ModelProviderId model_provider_from_key(const std::string& key)
{
    if (key == "makerworld")  return ModelProviderId::MakerWorld;
    if (key == "printables")  return ModelProviderId::Printables;
    if (key == "thingiverse") return ModelProviderId::Thingiverse;
    return ModelProviderId::Unknown;
}

/** What a provider is able to do; used to gate fan-out and UI hints. */
struct ModelProviderCapabilities
{
    bool can_search{false};
    bool can_import{false};
    bool can_import_from_url{false};
    bool requires_login_for_search{false};
    bool requires_login_for_download{false};
    // Which candidate signals this provider populates (so the ranker/UI can
    // treat missing signals as "unknown" rather than "zero").
    bool provides_download_count{false};
    bool provides_likes{false};
    bool provides_license{false};
    bool provides_update_date{false};
    bool provides_success_rate{false};
    bool provides_ai_confidence{false};
    bool provides_print_time{false};
    bool provides_difficulty{false};
    bool provides_support_flag{false};
};

/** Main-thread snapshot passed to worker-thread provider searches (no wx handles). */
struct ModelSearchContext
{
    std::string locale;
    std::string country_code;
    std::string printer_model;
    bool        user_logged_in{false};
    bool        network_agent_ok{false};
    bool        plugin_search_available{false};
    bool        plugin_download_available{false};
    // Pre-resolved on the main thread (see MakerWorldSearchService::build_context).
    // Worker-thread HTTP must not call NetworkAgent / app_config for tokens.
    std::string access_token;
};

/** Normalized query plus recall-broadening variants (built via MakerWorld helpers). */
struct ModelSearchQuery
{
    std::string              raw_text;          // user text as received
    std::string              normalized_text;   // boilerplate stripped keywords
    std::string              translated_english; // optional Ollama translation (may be empty)
    std::vector<std::string> variants;          // includes normalized_text at index 0 when non-empty
    bool                     contains_cjk{false};

    bool empty() const { return normalized_text.empty(); }
};

/** Superset of MakerWorldCandidate covering multi-provider ranking signals. */
struct ModelCandidate
{
    ModelProviderId provider_id{ModelProviderId::Unknown};

    std::string id;             // provider-native primary id (MakerWorld: design_id)
    std::string canonical_key;  // "<provider>:<id>" cross-provider identity
    std::string title;
    std::string author;
    std::string thumbnail_url;
    std::string url;            // human-facing detail page
    std::string license;

    int         downloads{0};
    int         likes{0};

    std::optional<std::chrono::system_clock::time_point> update_date;

    double      success_rate{-1.0};   // 0..1, -1 = unknown
    double      ai_confidence{-1.0};  // 0..1, -1 = unknown

    std::optional<int>  est_print_time_sec;
    std::optional<int>  difficulty;   // provider-relative scale (e.g. 1..5)
    std::optional<bool> needs_support;

    double      composite_score{0.0};     // filled by CandidateRanker
    std::string recommendation_reason;    // filled by CandidateRanker

    // Import hints.
    std::string download_url;
    std::string filename;
    bool        login_required{false};

    // MakerWorld extras (kept so MakerWorld import path is lossless).
    std::string model_id;
    std::string profile_id;
};

/** One provider's contribution to a fan-out search. */
struct ModelProviderSearchResult
{
    ModelProviderId              provider_id{ModelProviderId::Unknown};
    bool                         ok{false};
    bool                         timed_out{false};
    std::string                  error;
    std::vector<ModelCandidate>  candidates;
    int                          latency_ms{0};
    std::string                  source; // provider-specific backend tag
};

/** Merged + deduped + ranked result across all providers. */
struct ModelSearchAggregateResult
{
    bool                                    ok{false};
    bool                                    partial{false}; // some providers failed / timed out
    std::string                             error;
    std::vector<ModelCandidate>             candidates;
    std::vector<ModelProviderSearchResult>  per_provider;
    int                                     total_latency_ms{0};
};

/** Resolved download payload for the Plater import path. */
struct ModelImportPayload
{
    ModelProviderId provider_id{ModelProviderId::Unknown};
    bool            ok{false};
    std::string     error;
    std::string     download_info;  // url&name= for Plater::request_model_download
    std::string     detail_page_url;
};

using ModelSearchResultCallback = std::function<void(ModelSearchAggregateResult)>;

}} // namespace Slic3r::GUI

#endif

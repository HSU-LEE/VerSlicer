#ifndef slic3r_MakerWorldSearchCore_hpp_
#define slic3r_MakerWorldSearchCore_hpp_

#include "MakerWorldTypes.hpp"

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

/** Strip zero-width chars and trim; safe pre-search cleanup (no full ICU NFC). */
std::string sanitize_search_text(const std::string& query);

/** Extract MakerWorld keywords from chat text (strips search boilerplate). */
std::string normalize_makerworld_search_query(const std::string& user_text);

std::vector<MakerWorldCandidate> parse_hits_json(const std::string& body);
std::vector<MakerWorldCandidate> filter_by_query(const std::vector<MakerWorldCandidate>& in, const std::string& query);
/** Build search keyword variants; optional translated_english from Ollama translation. */
std::vector<std::string> search_query_variants(const std::string& query,
                                               const std::string& translated_english = {});

std::vector<std::string> tokenize_query(const std::string& query);

int score_candidate(const MakerWorldCandidate& c, const std::vector<std::string>& tokens, const std::string& phrase_lower,
                    bool deprioritize_login_required);

std::vector<MakerWorldCandidate> merge_candidates(std::vector<MakerWorldCandidate> base,
                                                    const std::vector<MakerWorldCandidate>& extra);

std::vector<MakerWorldCandidate> rank_and_dedupe(std::vector<MakerWorldCandidate> candidates, const std::string& query,
                                                 int limit, bool deprioritize_login_required = true);

std::vector<MakerWorldCandidate> filter_by_query_scored(const std::vector<MakerWorldCandidate>& in,
                                                        const std::string& query, int limit);

}} // namespace

#endif

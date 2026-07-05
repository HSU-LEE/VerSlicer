#include "ModelSearchDedupe.hpp"

#include <algorithm>
#include <cctype>

namespace Slic3r { namespace GUI {

namespace {

// Fill fields on `keep` from a duplicate `dup` without overwriting existing data.
// Conservative merge: only import hints and stronger-signal numeric fields.
void enrich_from_duplicate(ModelCandidate& keep, const ModelCandidate& dup)
{
    if (keep.download_url.empty())  keep.download_url  = dup.download_url;
    if (keep.filename.empty())      keep.filename      = dup.filename;
    if (keep.thumbnail_url.empty()) keep.thumbnail_url = dup.thumbnail_url;
    if (keep.url.empty())           keep.url           = dup.url;
    if (keep.license.empty())       keep.license       = dup.license;
    if (keep.model_id.empty())      keep.model_id      = dup.model_id;
    if (keep.profile_id.empty())    keep.profile_id    = dup.profile_id;

    keep.downloads = std::max(keep.downloads, dup.downloads);
    keep.likes     = std::max(keep.likes, dup.likes);

    if (keep.success_rate < 0.0 && dup.success_rate >= 0.0)
        keep.success_rate = dup.success_rate;
    if (keep.ai_confidence < 0.0 && dup.ai_confidence >= 0.0)
        keep.ai_confidence = dup.ai_confidence;
    if (!keep.update_date.has_value() && dup.update_date.has_value())
        keep.update_date = dup.update_date;
    if (!keep.est_print_time_sec.has_value() && dup.est_print_time_sec.has_value())
        keep.est_print_time_sec = dup.est_print_time_sec;
    if (!keep.difficulty.has_value() && dup.difficulty.has_value())
        keep.difficulty = dup.difficulty;
    if (!keep.needs_support.has_value() && dup.needs_support.has_value())
        keep.needs_support = dup.needs_support;
}

} // namespace

std::string ModelSearchDedupe::normalize_identity(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (unsigned char ch : text) {
        if (ch >= 0x80) {
            // Preserve multibyte (CJK) sequence bytes verbatim.
            out.push_back(static_cast<char>(ch));
        } else if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::tolower(ch)));
        }
        // ASCII punctuation / whitespace dropped (collapses spacing differences).
    }
    return out;
}

int ModelSearchDedupe::dedupe_cross_provider(std::vector<ModelCandidate>& candidates)
{
    if (candidates.size() < 2)
        return 0;

    std::vector<ModelCandidate> out;
    out.reserve(candidates.size());

    // Parallel keys for kept entries (index-aligned with `out`).
    std::vector<std::string> keep_canonical;
    std::vector<std::string> keep_title_author;
    keep_canonical.reserve(candidates.size());
    keep_title_author.reserve(candidates.size());

    int removed = 0;
    for (auto& c : candidates) {
        const std::string canon = c.canonical_key;
        const std::string norm_title  = normalize_identity(c.title);
        const std::string norm_author = normalize_identity(c.author);
        const bool        fuzzy_ok    = !norm_title.empty() && !norm_author.empty();
        const std::string title_author = fuzzy_ok ? (norm_title + "\x1f" + norm_author) : std::string{};

        int match_index = -1;
        for (size_t i = 0; i < out.size(); ++i) {
            if (!canon.empty() && keep_canonical[i] == canon) {
                match_index = static_cast<int>(i);
                break;
            }
            if (fuzzy_ok && !keep_title_author[i].empty() && keep_title_author[i] == title_author) {
                match_index = static_cast<int>(i);
                break;
            }
        }

        if (match_index >= 0) {
            enrich_from_duplicate(out[static_cast<size_t>(match_index)], c);
            ++removed;
            continue;
        }

        out.push_back(std::move(c));
        keep_canonical.push_back(canon);
        keep_title_author.push_back(title_author);
    }

    candidates = std::move(out);
    return removed;
}

}} // namespace Slic3r::GUI

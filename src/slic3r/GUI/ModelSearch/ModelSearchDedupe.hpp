#ifndef slic3r_ModelSearchDedupe_hpp_
#define slic3r_ModelSearchDedupe_hpp_

#include "ModelSearchTypes.hpp"

#include <vector>

namespace Slic3r { namespace GUI {

// Cross-provider deduplication. Runs BEFORE ranking so the ranker sees one entry
// per logical model. Conservative by design: it only collapses candidates that
// are near-certainly the same model, and enriches the kept entry with any
// import hints the duplicate carried.
//
// Two passes:
//   1. exact canonical_key match (non-empty)
//   2. normalized title AND author match (both non-empty)
class ModelSearchDedupe
{
public:
    // Dedupes in place, preserving first-seen order. Returns number removed.
    static int dedupe_cross_provider(std::vector<ModelCandidate>& candidates);

    // Exposed for testing: normalize a string for fuzzy identity (lowercase,
    // alnum + CJK bytes only, collapsed).
    static std::string normalize_identity(const std::string& text);
};

}} // namespace Slic3r::GUI

#endif

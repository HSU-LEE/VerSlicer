#ifndef slic3r_ModelSearchQueryBuilder_hpp_
#define slic3r_ModelSearchQueryBuilder_hpp_

#include "ModelSearchTypes.hpp"

#include <string>

namespace Slic3r { namespace GUI {

// Builds a normalized ModelSearchQuery from raw chat text by reusing the
// existing MakerWorld query helpers (normalize, translate, variants). No query
// logic is duplicated here.
class ModelSearchQueryBuilder
{
public:
    // Build a query from user text.
    //
    // allow_translation controls whether translate_search_query_to_english() is
    // invoked. That helper performs a BLOCKING network (Ollama) call, so it must
    // only run on a worker thread. Callers on the main thread must pass false.
    static ModelSearchQuery build_query(const std::string& user_text, bool allow_translation = false);
};

}} // namespace Slic3r::GUI

#endif

#include "ModelSearchQueryBuilder.hpp"

#include "../MakerWorld/MakerWorldQueryTranslator.hpp"
#include "../MakerWorld/MakerWorldSearchCore.hpp"

namespace Slic3r { namespace GUI {

ModelSearchQuery ModelSearchQueryBuilder::build_query(const std::string& user_text, bool allow_translation)
{
    ModelSearchQuery q;
    q.raw_text = user_text;

    // Strip search boilerplate ("찾아줘", "search for", platform names, ...) and
    // zero-width chars, matching the MakerWorld search pipeline exactly.
    q.normalized_text = sanitize_search_text(normalize_makerworld_search_query(user_text));
    q.contains_cjk    = search_text_contains_cjk(q.normalized_text);

    if (q.normalized_text.empty())
        return q;

    // translate_search_query_to_english() is a blocking Ollama HTTP call; only
    // perform it when the caller guarantees a worker-thread context.
    if (allow_translation && q.contains_cjk)
        q.translated_english = translate_search_query_to_english(q.normalized_text);

    q.variants = search_query_variants(q.normalized_text, q.translated_english);
    return q;
}

}} // namespace Slic3r::GUI

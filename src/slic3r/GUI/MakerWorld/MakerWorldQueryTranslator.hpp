#ifndef slic3r_MakerWorldQueryTranslator_hpp_
#define slic3r_MakerWorldQueryTranslator_hpp_

#include <string>

namespace Slic3r { namespace GUI {

/** True when query contains Hangul / CJK / kana characters. */
bool search_text_contains_cjk(const std::string& text);

/**
 * Translate a MakerWorld search query to concise English keywords via Ollama.
 * Returns empty on failure, when Ollama is unavailable, or when translation is unnecessary.
 */
std::string translate_search_query_to_english(const std::string& query);

void invalidate_search_translation_cache();

}} // namespace

#endif

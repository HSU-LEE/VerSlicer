#ifndef slic3r_BambuLabWikiSearchCore_hpp_
#define slic3r_BambuLabWikiSearchCore_hpp_

#include <string>

namespace Slic3r { namespace GUI {

/** Text helpers for Bambu Lab wiki search (no network). */
namespace BambuLabWikiSearchCore {

std::string wiki_locale(bool ko_ui);
std::string normalize_search_query(const std::string& user_request, bool ko_ui);
std::string html_to_plain_text(const std::string& html, size_t max_chars = 0);
std::string escape_graphql_string(const std::string& s);

} // namespace BambuLabWikiSearchCore

}} // namespace

#endif

#include "BambuLabWikiSearchCore.hpp"

#include <boost/algorithm/string.hpp>

#include <regex>

namespace Slic3r { namespace GUI {
namespace BambuLabWikiSearchCore {

std::string wiki_locale(bool ko_ui) { return ko_ui ? "ko" : "en"; }

std::string normalize_search_query(const std::string& user_request, bool ko_ui)
{
    (void) ko_ui;
    std::string q = user_request;
    boost::trim(q);
    return q;
}

std::string html_to_plain_text(const std::string& html, size_t max_chars)
{
    std::string text = html;
    text             = std::regex_replace(text, std::regex(R"(<script[\s\S]*?</script>)", std::regex::icase), " ");
    text             = std::regex_replace(text, std::regex(R"(<style[\s\S]*?</style>)", std::regex::icase), " ");
    text             = std::regex_replace(text, std::regex(R"(<nav[\s\S]*?</nav>)", std::regex::icase), " ");
    text             = std::regex_replace(text, std::regex(R"(<header[\s\S]*?</header>)", std::regex::icase), " ");
    text             = std::regex_replace(text, std::regex(R"(<footer[\s\S]*?</footer>)", std::regex::icase), " ");
    text             = std::regex_replace(text, std::regex(R"(<[^>]+>)"), "\n");
    text             = std::regex_replace(text, std::regex(R"(\s+)"), " ");
    boost::trim(text);

    if (max_chars > 0 && text.size() > max_chars) {
        text = text.substr(0, max_chars);
        const auto cut = text.rfind(' ');
        if (cut > max_chars / 2)
            text = text.substr(0, cut);
        text += "…";
    }
    return text;
}

std::string escape_graphql_string(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '\\' || c == '"')
            out += '\\';
        out += c;
    }
    return out;
}

} // namespace BambuLabWikiSearchCore
}} // namespace

#include "BambuLabWikiSearchCore.hpp"

#include "OllamaIntentRules.hpp"

#include <boost/algorithm/string.hpp>

#include <regex>

namespace Slic3r { namespace GUI {
namespace BambuLabWikiSearchCore {

using namespace OllamaIntentRules;

std::string wiki_locale(bool ko_ui) { return ko_ui ? "ko" : "en"; }

std::string normalize_search_query(const std::string& user_request, bool ko_ui)
{
    std::string q = user_request;
    boost::trim(q);
    if (q.empty())
        return q;

    if (contains_midair_or_failure_intent(q) || contains_support_intent(q))
        return "overhang support print quality";
    if (contains_adhesion_intent(q) || user_request.find("warp") != std::string::npos)
        return "warping bed adhesion brim";
    if (user_request.find("실") != std::string::npos || boost::icontains(q, "stringing")
        || boost::icontains(q, "ooze"))
        return "stringing oozing retraction";
    if (contains_durability_intent(q) || contains_strength_intent(q))
        return "print strength infill wall";
    if (user_request.find("첫") != std::string::npos || boost::icontains(q, "first layer"))
        return "first layer adhesion";
    if (boost::icontains(q, "elephant") || user_request.find("코끼리") != std::string::npos)
        return "elephant foot compensation";
    if (boost::icontains(q, "layer height") || user_request.find("층") != std::string::npos)
        return "layer height print quality";

    if (ko_ui) {
        static const std::pair<const char*, const char*> ko_to_en[] = {
            {"서포트", "support overhang"},
            {"브림", "brim adhesion"},
            {"채움", "infill density"},
            {"리트랙션", "retraction stringing"},
            {"온도", "nozzle temperature"},
            {"베드", "bed adhesion"},
        };
        for (const auto& kv : ko_to_en) {
            if (user_request.find(kv.first) != std::string::npos)
                return kv.second;
        }
    }
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

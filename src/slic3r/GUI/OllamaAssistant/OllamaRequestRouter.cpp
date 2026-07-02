#include "OllamaRequestRouter.hpp"

#include "OllamaSettingSearch.hpp"

#include <boost/algorithm/string.hpp>

#include <cctype>

namespace Slic3r { namespace GUI {

namespace {

bool contains_word_ci(const std::string& hay, const char* needle)
{
    if (!needle || !*needle)
        return false;
    const std::string token(needle);
    std::string       lower_hay = hay;
    boost::algorithm::to_lower(lower_hay);
    std::string lower_tok = token;
    boost::algorithm::to_lower(lower_tok);
    for (size_t pos = 0;;) {
        const size_t found = lower_hay.find(lower_tok, pos);
        if (found == std::string::npos)
            return false;
        const bool before_ok =
            found == 0 || !std::isalnum(static_cast<unsigned char>(lower_hay[found - 1]));
        const size_t after_pos = found + lower_tok.size();
        const bool after_ok    = after_pos >= lower_hay.size()
            || !std::isalnum(static_cast<unsigned char>(lower_hay[after_pos]));
        if (before_ok && after_ok)
            return true;
        pos = found + 1;
    }
}

bool looks_like_quality_not_placement(const std::string& user)
{
    return boost::icontains(user, "failed") || boost::icontains(user, "failure")
        || boost::icontains(user, "breaking") || boost::icontains(user, "break")
        || user.find("실패") != std::string::npos || user.find("망") != std::string::npos
        || user.find("부서") != std::string::npos;
}

bool looks_like_placement_request(const std::string& user)
{
    if (user.find("정렬") != std::string::npos || user.find("배치") != std::string::npos)
        return true;
    if (contains_word_ci(user, "arrange") || contains_word_ci(user, "auto-arrange")
        || contains_word_ci(user, "autoarrange"))
        return true;
    return false;
}

} // namespace

bool OllamaRequestRouter::is_geometry_request(const std::string& user)
{
    if (contains_word_ci(user, "rotate") || contains_word_ci(user, "flip")
        || contains_word_ci(user, "translate") || contains_word_ci(user, "scale")
        || contains_word_ci(user, "delete"))
        return true;

    if (user.find("회전") != std::string::npos || user.find("돌려") != std::string::npos
        || user.find("뒤집") != std::string::npos || user.find("삭제") != std::string::npos
        || user.find("이동") != std::string::npos || user.find("크기") != std::string::npos)
        return true;

    if (user.find('%') != std::string::npos)
        return true;

    if (looks_like_placement_request(user) && !looks_like_quality_not_placement(user))
        return true;

    return false;
}

OllamaRequestRoute OllamaRequestRouter::classify(const std::string& user_request)
{
    const std::string user = user_request;
    if (user.empty())
        return OllamaRequestRoute::Fast;

    if (OllamaRequestRouter::is_geometry_request(user))
        return OllamaRequestRoute::Fast;

    const auto hits = OllamaSettingSearch::search(user, 2, 8);
    if (hits.empty())
        return OllamaRequestRoute::Deep;
    if (hits.front().score >= 55)
        return OllamaRequestRoute::Standard;
    return OllamaRequestRoute::Deep;
}

const char* OllamaRequestRouter::route_name(OllamaRequestRoute route)
{
    switch (route) {
    case OllamaRequestRoute::Fast: return "fast";
    case OllamaRequestRoute::Standard: return "standard";
    case OllamaRequestRoute::Deep: return "deep";
    }
    return "unknown";
}

bool OllamaRequestRouter::benefits_from_wiki(const std::string& user_request)
{
    return classify(user_request) != OllamaRequestRoute::Fast;
}

}} // namespace

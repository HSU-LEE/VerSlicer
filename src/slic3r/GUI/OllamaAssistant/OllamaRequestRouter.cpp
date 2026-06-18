#include "OllamaRequestRouter.hpp"

#include "OllamaIntentRules.hpp"
#include "OllamaSettingSearch.hpp"

#include <boost/algorithm/string.hpp>
#include <regex>

namespace Slic3r { namespace GUI {

namespace {

using namespace OllamaIntentRules;

bool is_vague_fix(const std::string& user)
{
    return user.find("고쳐") != std::string::npos || user.find("도와") != std::string::npos ||
           user.find("help") != std::string::npos || user.find("fix") != std::string::npos ||
           user.find("문제") != std::string::npos || user.find("안돼") != std::string::npos ||
           user.find("안되") != std::string::npos || user.find("망했") != std::string::npos ||
           boost::icontains(user, "failed");
}

bool is_transform_only(const std::string& user)
{
    if (user_wants_delete(user))
        return true;
    if (contains_placement_intent(user) && !describes_print_quality_symptom(user))
        return true;
    if (contains_rotate_intent(user) && !describes_print_quality_symptom(user))
        return true;
    if (contains_flip_intent(user) && !describes_print_quality_symptom(user))
        return true;
    return false;
}

bool has_explicit_config_value(const std::string& user)
{
    static const std::regex re_pct(R"((\d+)\s*%)", std::regex::icase);
    static const std::regex re_mm(R"((\d+(?:\.\d+)?)\s*mm)", std::regex::icase);
    static const std::regex re_temp(R"((\d+)\s*(?:c|°|도)?\s*(?:nozzle|temp|온도))", std::regex::icase);
    return std::regex_search(user, re_pct) || std::regex_search(user, re_mm) || std::regex_search(user, re_temp) ||
           contains_explicit_infill_intent(user);
}

bool has_clear_single_intent(const std::string& user)
{
    int intents = 0;
    if (contains_support_intent(user))
        ++intents;
    if (contains_brim_intent(user))
        ++intents;
    if (contains_strength_intent(user) || contains_durability_intent(user))
        ++intents;
    if (contains_adhesion_intent(user))
        ++intents;
    if (contains_explicit_infill_intent(user))
        ++intents;
    return intents == 1;
}

} // namespace

OllamaRequestRoute OllamaRequestRouter::classify(const std::string& user_request)
{
    const std::string user = user_request;
    if (user.empty())
        return OllamaRequestRoute::Fast;

    if (is_vague_fix(user))
        return OllamaRequestRoute::Deep;

    if (is_transform_only(user))
        return OllamaRequestRoute::Fast;

    if (has_explicit_config_value(user) && has_clear_single_intent(user))
        return OllamaRequestRoute::Fast;

    const auto hits = OllamaSettingSearch::search(user, 2, 8);
    if (hits.empty() && describes_print_quality_symptom(user))
        return OllamaRequestRoute::Deep;

    if (!hits.empty() && hits.front().score >= 75 && has_clear_single_intent(user))
        return OllamaRequestRoute::Standard;

    if (!hits.empty() && hits.front().score >= 60 && !describes_print_quality_symptom(user))
        return OllamaRequestRoute::Standard;

    if (describes_print_quality_symptom(user))
        return OllamaRequestRoute::Deep;

    if (!hits.empty())
        return OllamaRequestRoute::Standard;

    return OllamaRequestRoute::Fast;
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
    if (is_vague_fix(user_request))
        return true;
    return describes_print_quality_symptom(user_request);
}

}} // namespace

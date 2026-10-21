#include "AssistantRequestRouter.hpp"

namespace Slic3r { namespace GUI {

bool AssistantRequestRouter::is_geometry_request(const std::string& /*user*/)
{
    // The model chooses tools. Sentences are not classified into geometry vs settings.
    return false;
}

AssistantRequestRoute AssistantRequestRouter::classify(const std::string& user_request)
{
    if (user_request.empty())
        return AssistantRequestRoute::Fast;
    return AssistantRequestRoute::Standard;
}

const char* AssistantRequestRouter::route_name(AssistantRequestRoute route)
{
    switch (route) {
    case AssistantRequestRoute::Fast: return "fast";
    case AssistantRequestRoute::Standard: return "standard";
    case AssistantRequestRoute::Deep: return "deep";
    }
    return "unknown";
}

bool AssistantRequestRouter::benefits_from_wiki(const std::string& /*user_request*/)
{
    // Wiki is a tool the model calls (wiki_search), not a keyword prefetch.
    return false;
}

}} // namespace

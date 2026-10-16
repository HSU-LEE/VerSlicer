#include "OllamaRequestRouter.hpp"

namespace Slic3r { namespace GUI {

bool OllamaRequestRouter::is_geometry_request(const std::string& /*user*/)
{
    // The model chooses tools. Sentences are not classified into geometry vs settings.
    return false;
}

OllamaRequestRoute OllamaRequestRouter::classify(const std::string& user_request)
{
    if (user_request.empty())
        return OllamaRequestRoute::Fast;
    return OllamaRequestRoute::Standard;
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

bool OllamaRequestRouter::benefits_from_wiki(const std::string& /*user_request*/)
{
    // Wiki is a tool the model calls (wiki_search), not a keyword prefetch.
    return false;
}

}} // namespace

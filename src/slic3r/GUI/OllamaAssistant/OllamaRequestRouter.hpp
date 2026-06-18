#ifndef slic3r_OllamaRequestRouter_hpp_
#define slic3r_OllamaRequestRouter_hpp_

#include <string>

namespace Slic3r { namespace GUI {

enum class OllamaRequestRoute
{
    /** Single LLM call with compact context (transform/arrange/explicit). */
    Fast,
    /** Single resolver-style call with search-selected catalog slice. */
    Standard,
    /** Planner + Resolver (vague / multi-symptom). */
    Deep,
};

/** Classify how many LLM hops to use for an apply-mode request. */
class OllamaRequestRouter
{
public:
    static OllamaRequestRoute classify(const std::string& user_request);
    static const char*        route_name(OllamaRequestRoute route);
    /** Quality/vague requests that benefit from Bambu Lab wiki lookup. */
    static bool               benefits_from_wiki(const std::string& user_request);
};

}} // namespace

#endif

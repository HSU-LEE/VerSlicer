#ifndef slic3r_AssistantRequestRouter_hpp_
#define slic3r_AssistantRequestRouter_hpp_

#include <string>

namespace Slic3r { namespace GUI {

enum class AssistantRequestRoute
{
    /** Single LLM call with compact context (transform/arrange/explicit). */
    Fast,
    /** Single resolver-style call with search-selected catalog slice. */
    Standard,
    /** Planner + Resolver (vague / multi-symptom). */
    Deep,
};

/** Classify how many LLM hops to use for an apply-mode request. */
class AssistantRequestRouter
{
public:
    static AssistantRequestRoute classify(const std::string& user_request);
    static const char*        route_name(AssistantRequestRoute route);
    /** Quality/vague requests that benefit from Bambu Lab wiki lookup. */
    static bool               benefits_from_wiki(const std::string& user_request);
    /** True when the user asked to move, rotate, scale, arrange, or delete models. */
    static bool               is_geometry_request(const std::string& user_request);
};

}} // namespace

#endif

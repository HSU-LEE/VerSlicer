#ifndef slic3r_OllamaAssistRouter_hpp_
#define slic3r_OllamaAssistRouter_hpp_

#include <string>

namespace Slic3r { namespace GUI {

/** Backend routing for Assist mode (no UI). */
class OllamaAssistRouter
{
public:
    /** Multi-step observe→act loop (Cursor-style). */
    static bool should_use_assist_loop(const std::string& user_request, bool apply_mode);

    /** Planner → resolver two-hop LLM path. */
    static bool should_use_two_hop(const std::string& user_request, bool apply_mode);

    /** Skip LLM: apply rule fallbacks for stringing / strength / slice-suggested support. */
    static bool should_apply_rule_only(const std::string& user_request, bool apply_mode);
};

}} // namespace

#endif

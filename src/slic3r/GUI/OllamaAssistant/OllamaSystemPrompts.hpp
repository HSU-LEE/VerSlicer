#ifndef slic3r_OllamaSystemPrompts_hpp_
#define slic3r_OllamaSystemPrompts_hpp_

#include <string>

namespace Slic3r { namespace GUI {

/** Layered system prompts for outcome-first 3D printing engineer persona. */
class OllamaSystemPrompts
{
public:
    static std::string apply_system_prompt(bool korean);
    static std::string question_mode_suffix(bool korean);
    static std::string diagnostic_system_prompt(bool korean);
    static std::string proposal_turn_instructions(bool korean);
    /** @deprecated use diagnostic_system_prompt */
    static std::string planner_system_prompt(bool korean);
    /** @deprecated use proposal_turn_instructions */
    static std::string resolver_turn_instructions(bool korean);
};

}} // namespace

#endif

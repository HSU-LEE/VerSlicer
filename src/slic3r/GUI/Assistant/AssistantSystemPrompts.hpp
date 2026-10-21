#ifndef slic3r_AssistantSystemPrompts_hpp_
#define slic3r_AssistantSystemPrompts_hpp_

#include <string>

namespace Slic3r { namespace GUI {

/** Layered system prompts for outcome-first 3D printing engineer persona. */
class AssistantSystemPrompts
{
public:
    static std::string apply_system_prompt(bool korean);
    static std::string question_system_prompt(bool korean);
};

}} // namespace

#endif

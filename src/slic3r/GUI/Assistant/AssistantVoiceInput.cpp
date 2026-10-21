#include "AssistantVoiceInput.hpp"

namespace Slic3r { namespace GUI {

#ifndef __APPLE__
std::unique_ptr<AssistantVoiceInput> create_assistant_voice_input()
{
    return nullptr;
}
#endif

}} // namespace


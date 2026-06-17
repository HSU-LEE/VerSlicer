#include "OllamaVoiceInput.hpp"

namespace Slic3r { namespace GUI {

#ifndef __APPLE__
std::unique_ptr<OllamaVoiceInput> create_ollama_voice_input()
{
    return nullptr;
}
#endif

}} // namespace


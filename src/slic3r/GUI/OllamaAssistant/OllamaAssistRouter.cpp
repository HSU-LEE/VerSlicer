#include "OllamaAssistRouter.hpp"

#include "OllamaConfig.hpp"

#include "../MakerWorld/MakerWorldIntent.hpp"

namespace Slic3r { namespace GUI {

bool OllamaAssistRouter::should_use_assist_loop(const std::string& user_request, bool apply_mode)
{
    if (!apply_mode || user_request.empty() || !ollama_assist_loop_enabled())
        return false;
    if (MakerWorldIntent::is_pure_makerworld_request(user_request))
        return false;
    return true;
}

}} // namespace

#include "OllamaAssistRouter.hpp"

#include "OllamaConfig.hpp"

namespace Slic3r { namespace GUI {

bool OllamaAssistRouter::should_use_assist_loop(const std::string& user_request, bool apply_mode,
                                               bool plate_has_model)
{
    (void) plate_has_model;
    if (!apply_mode || user_request.empty() || !ollama_assist_loop_enabled())
        return false;
    return true;
}

}} // namespace

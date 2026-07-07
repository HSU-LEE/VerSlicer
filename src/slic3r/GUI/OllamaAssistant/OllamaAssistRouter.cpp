#include "OllamaAssistRouter.hpp"

#include "OllamaConfig.hpp"
#include "OllamaUserFlow.hpp"

#include "../MakerWorld/MakerWorldIntent.hpp"

namespace Slic3r { namespace GUI {

bool OllamaAssistRouter::should_use_assist_loop(const std::string& user_request, bool apply_mode,
                                               bool plate_has_model)
{
    if (!apply_mode || user_request.empty() || !ollama_assist_loop_enabled())
        return false;
    if (MakerWorldIntent::is_pure_makerworld_request(user_request))
        return false;
    // "get me X and print it" belongs in PrintJobOrchestrator, not the settings agent.
    if (OllamaUserFlow::is_acquisition_print_request(user_request, plate_has_model))
        return false;
    return true;
}

}} // namespace

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
};

}} // namespace

#endif

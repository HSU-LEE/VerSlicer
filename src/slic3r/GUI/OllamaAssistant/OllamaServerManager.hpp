#ifndef slic3r_OllamaServerManager_hpp_
#define slic3r_OllamaServerManager_hpp_

#include <wx/string.h>

namespace Slic3r { namespace GUI {

/** Tracks an Ollama server process started by Verslicer and stops it on app exit. */
class OllamaServerManager
{
public:
    static wxString resolve_ollama_command();

    /** Call after wxExecute("… serve") when Verslicer started the server. */
    static void mark_started(long pid);

    /** True if Verslicer may spawn `ollama serve` (not already starting / over retry budget). */
    static bool should_spawn_serve();

    /** Record a spawn attempt; call before wxExecute serve. */
    static void note_serve_spawn_attempt();

    /** Number of serve spawn attempts in this session (after note_serve_spawn_attempt). */
    static int serve_spawn_attempt_count();

    /** Clear serve retry budget after a successful API probe. */
    static void note_serve_reachable();

    /** Terminate the server only if Verslicer started it (not a pre-existing Ollama). */
    static void shutdown_if_started();
};

}} // namespace

#endif

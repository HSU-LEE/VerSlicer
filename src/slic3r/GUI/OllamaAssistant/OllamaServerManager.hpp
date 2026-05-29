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

    /** Terminate the server only if Verslicer started it (not a pre-existing Ollama). */
    static void shutdown_if_started();
};

}} // namespace

#endif

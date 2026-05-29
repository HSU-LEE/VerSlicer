#include "OllamaServerManager.hpp"

#include <atomic>
#include <wx/filefn.h>
#include <wx/process.h>

namespace Slic3r { namespace GUI {

namespace {

std::atomic<long> s_pid{ 0 };
std::atomic<bool> s_started_by_app{ false };

} // namespace

wxString OllamaServerManager::resolve_ollama_command()
{
    const wxString candidates[] = {
        "/opt/homebrew/bin/ollama",
        "/usr/local/bin/ollama",
        "ollama"
    };
    for (const auto& c : candidates) {
        if (c.Contains("/") && wxFileExists(c))
            return c;
        if (!c.Contains("/"))
            return c;
    }
    return "ollama";
}

void OllamaServerManager::mark_started(long pid)
{
    if (pid <= 0)
        return;
    s_pid.store(pid);
    s_started_by_app.store(true);
}

void OllamaServerManager::shutdown_if_started()
{
    if (!s_started_by_app.exchange(false))
        return;

    const long pid = s_pid.exchange(0);
    if (pid <= 0)
        return;

    wxKillError err = wxKILL_OK;
    if (wxKill(pid, wxSIGTERM, &err, wxKILL_CHILDREN) == 0 &&
        (err == wxKILL_OK || err == wxKILL_NO_PROCESS))
        return;

    wxKill(pid, wxSIGKILL, nullptr, wxKILL_CHILDREN);
}

}} // namespace

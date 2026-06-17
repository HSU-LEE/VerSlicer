#include "OllamaServerManager.hpp"

#include <atomic>
#include <wx/filefn.h>
#include <wx/process.h>
#include <wx/stdpaths.h>

namespace Slic3r { namespace GUI {

namespace {

std::atomic<long> s_pid{ 0 };
std::atomic<bool> s_started_by_app{ false };
std::atomic<bool> s_serve_spawn_pending{ false };
std::atomic<int>  s_serve_spawn_attempts{ 0 };

constexpr int kMaxServeSpawnAttempts = 10;

} // namespace

wxString OllamaServerManager::resolve_ollama_command()
{
#if defined(__APPLE__)
    const wxString candidates[] = {
        "/opt/homebrew/bin/ollama",
        "/usr/local/bin/ollama",
        "ollama",
    };
#elif defined(_WIN32)
    wxString local = wxStandardPaths::Get().GetUserLocalDataDir() + "\\Programs\\Ollama\\ollama.exe";
    const wxString candidates[] = {
        local,
        "ollama.exe",
        "ollama",
    };
#else
    const wxString candidates[] = {
        "/usr/local/bin/ollama",
        "/usr/bin/ollama",
        "/snap/bin/ollama",
        "ollama",
    };
#endif
    for (const auto& c : candidates) {
        if (c.Contains("/") || c.Contains("\\")) {
            if (wxFileExists(c))
                return c;
        } else {
            return c;
        }
    }
    return "ollama";
}

void OllamaServerManager::mark_started(long pid)
{
    if (pid <= 0) {
        s_serve_spawn_pending.store(false);
        return;
    }
    s_pid.store(pid);
    s_started_by_app.store(true);
    s_serve_spawn_pending.store(false);
}

bool OllamaServerManager::should_spawn_serve()
{
    if (s_started_by_app.load() && s_pid.load() > 0)
        return false;
    if (s_serve_spawn_pending.load())
        return false;
    return s_serve_spawn_attempts.load() < kMaxServeSpawnAttempts;
}

void OllamaServerManager::note_serve_spawn_attempt()
{
    s_serve_spawn_pending.store(true);
    s_serve_spawn_attempts.fetch_add(1);
}

int OllamaServerManager::serve_spawn_attempt_count()
{
    return s_serve_spawn_attempts.load();
}

void OllamaServerManager::note_serve_reachable()
{
    s_serve_spawn_pending.store(false);
    s_serve_spawn_attempts.store(0);
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

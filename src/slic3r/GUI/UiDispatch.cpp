#include "UiDispatch.hpp"

#include <wx/app.h>

#include <boost/log/trivial.hpp>

#include <exception>

#include <mutex>

namespace Slic3r { namespace GUI {

namespace {
std::mutex& gate_mutex()
{
    static std::mutex m;
    return m;
}
bool g_open = true; // guarded by gate_mutex()
} // namespace

void post_to_ui(std::function<void()> fn)
{
    // Holding the lock across CallAfter means ui_dispatch_close() cannot finish,
    // and the app cannot be torn down, while a post is half done.
    std::lock_guard<std::mutex> lock(gate_mutex());
    if (!g_open || !fn)
        return;
    if (wxAppConsole* app = wxTheApp)
        app->CallAfter([fn = std::move(fn)]() {
            // An exception escaping a queued callback reaches wx's main-loop
            // handler, which rethrows and terminates the app. Stop it here.
            try {
                fn();
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "UI callback threw: " << e.what();
            } catch (...) {
                BOOST_LOG_TRIVIAL(error) << "UI callback threw an unknown exception";
            }
        });
}

void ui_dispatch_close()
{
    std::lock_guard<std::mutex> lock(gate_mutex());
    g_open = false;
}

bool ui_dispatch_open()
{
    std::lock_guard<std::mutex> lock(gate_mutex());
    return g_open;
}

}} // namespace

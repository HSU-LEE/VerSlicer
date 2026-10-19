#ifndef slic3r_UiDispatch_hpp_
#define slic3r_UiDispatch_hpp_

#include <functional>

namespace Slic3r { namespace GUI {

/** Run fn on the UI thread. Safe from any background thread, including while the
 *  app is shutting down: once ui_dispatch_close() has run, fn is dropped instead
 *  of being posted to an application object that may already be gone. */
void post_to_ui(std::function<void()> fn);

/** Main thread, when shutdown begins. Waits for posts in flight, then refuses new ones. */
void ui_dispatch_close();

/** True until ui_dispatch_close(). Safe from any thread (wxGetApp() is not). */
bool ui_dispatch_open();

}} // namespace

#endif

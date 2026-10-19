#ifndef slic3r_ChatGptAccountBar_hpp_
#define slic3r_ChatGptAccountBar_hpp_

#include <wx/string.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

class Button;
class wxBoxSizer;
class wxStaticText;
class wxWindow;

namespace Slic3r { namespace GUI {

/** The "Connect ChatGPT / Sign out" row of the assistant panel: owns the
 *  account label and button, runs browser sign-in off the UI thread, and
 *  reports back through callbacks so the chat panel only reacts to results. */
class ChatGptAccountBar
{
public:
    struct Callbacks
    {
        /** Post a system line to the chat and the status bar. */
        std::function<void(const wxString&)> notify;
        /** Map a raw sign-in error to user-facing text. */
        std::function<wxString(const std::string&)> format_error;
        std::function<void()> signed_in;
        std::function<void()> signed_out;
        /** Re-layout the host after the row changes size. */
        std::function<void()> relayout;
    };

    ChatGptAccountBar(wxWindow* parent, int row_height, Callbacks callbacks);
    ~ChatGptAccountBar();

    wxBoxSizer* sizer() const { return m_row; }
    bool        signing_in() const { return m_signing_in; }

    /** Sign in when unlinked, or offer to sign out when linked. */
    void toggle();
    /** Disable while the chat is busy or a sign-in is pending. */
    void set_busy(bool busy);
    /** Restyle the button and label for the current link state. */
    void refresh();

private:
    void start_sign_in();
    void finish_sign_in(const std::string& error);

    wxWindow*     m_parent{nullptr};
    wxBoxSizer*   m_row{nullptr};
    wxStaticText* m_label{nullptr};
    Button*       m_button{nullptr};
    bool          m_signing_in{false};
    bool          m_busy{false};
    Callbacks     m_cb;
    std::shared_ptr<std::atomic<bool>> m_alive;
};

}} // namespace

#endif

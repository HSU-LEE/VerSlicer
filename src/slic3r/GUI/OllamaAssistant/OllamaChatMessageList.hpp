#ifndef slic3r_OllamaChatMessageList_hpp_
#define slic3r_OllamaChatMessageList_hpp_

#include <wx/scrolwin.h>
#include <wx/string.h>

#include <vector>

class StaticBox;
class wxBoxSizer;
class wxStaticText;
class wxTimer;

namespace Slic3r { namespace GUI {

enum class ChatMessageRole { System, User, Assistant };

enum class ChatMessageKind { Normal, Progress, Error, Question };

/**
 * Scrollable chat transcript: one rounded bubble per message, user messages
 * right-aligned, assistant/system messages left-aligned. Owns an internal
 * message model (role / kind / text) so the whole transcript can be re-wrapped
 * on resize and re-rendered on reset. Also hosts a transient "pending" bubble
 * (animated ellipsis) used while the assistant is thinking / streaming.
 *
 * All methods are main-thread only. Colors come from SlicePilotUi::Theme, so
 * the widget is dark-mode safe.
 */
class OllamaChatMessageList : public wxScrolledWindow
{
public:
    explicit OllamaChatMessageList(wxWindow* parent);
    ~OllamaChatMessageList() override;

    /** Append a message bubble; returns its id (stable until clear()). */
    int  append_message(ChatMessageRole role, const wxString& text, ChatMessageKind kind = ChatMessageKind::Normal);
    /** Replace the text of an existing message (streaming / edits). */
    void update_message(int id, const wxString& text);
    /** Replace the first System message text (mode-welcome swap). */
    void set_first_system_message(const wxString& text);
    /** Text of the newest Assistant message ("" when none). */
    wxString last_assistant_text() const;
    /** Remove all messages and the pending bubble. */
    void clear();

    // --- Pending ("thinking") bubble -------------------------------------
    /** Show the pending bubble with an animated ellipsis (no-op when shown). */
    void begin_pending(const wxString& caption);
    /** Append one muted progress line inside the pending bubble. */
    void append_pending_line(const wxString& line);
    /** Replace the streamed preview text inside the pending bubble. */
    void set_pending_stream_text(const wxString& text);
    bool has_pending() const { return m_pending_row != nullptr; }
    /** Remove the pending bubble (final reply arrived / request ended). */
    void clear_pending();

private:
    struct Row
    {
        int             id{-1};
        ChatMessageRole role{ChatMessageRole::Assistant};
        ChatMessageKind kind{ChatMessageKind::Normal};
        wxString        text;
        wxPanel*        row_panel{nullptr};
        StaticBox*      bubble{nullptr};
        wxStaticText*   label{nullptr};
    };

    Row  make_row(ChatMessageRole role, ChatMessageKind kind, const wxString& text, bool pending);
    void style_bubble(const Row& row);
    int  bubble_wrap_width() const;
    void rewrap_row(Row& row);
    void rewrap_all();
    void scroll_to_bottom();
    void drop_oldest_if_needed();
    void on_size(wxSizeEvent& evt);
    void on_pending_timer();
    void refresh_pending_label();

    std::vector<Row> m_rows;
    wxBoxSizer*      m_sizer{nullptr};
    int              m_next_id{1};
    int              m_last_wrap_width{0};

    // Pending bubble state
    Row*             m_pending_row{nullptr};
    Row              m_pending_storage;
    wxString         m_pending_caption;
    wxString         m_pending_lines;
    wxString         m_pending_stream;
    wxTimer*         m_pending_timer{nullptr};
    int              m_pending_dots{0};
};

}} // namespace Slic3r::GUI

#endif

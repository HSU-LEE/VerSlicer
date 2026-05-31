#ifndef slic3r_OllamaChatPanel_hpp_
#define slic3r_OllamaChatPanel_hpp_

#include "../GUI_Utils.hpp"
#include "OllamaClient.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

class wxButton;
class wxStaticText;
class wxTextCtrl;
class wxTimer;

namespace Slic3r { namespace GUI {

class OllamaChatPanel : public wxPanel
{
public:
    explicit OllamaChatPanel(wxWindow* parent, bool show_header = true);
    ~OllamaChatPanel() override;

    void refresh_models(); // keeps Ollama warm / triggers auto-pull if needed
    void submit_text_and_send(const wxString& text);
    void set_input_text(const wxString& text);

    void set_collapsed(bool collapsed);
    bool is_collapsed() const { return m_collapsed; }

private:
    void load_settings();
    void save_settings();
    void ensure_ollama_running();
    void ensure_default_model_ready(const std::vector<std::string>& models);
    void append_chat(const wxString& role, const wxString& text);
    void set_busy(bool busy);
    void on_send(wxCommandEvent& event);
    void on_models_loaded(const std::vector<std::string>& models, const std::string& error);
    void on_chat_response(const std::string& assistant_text, const std::string& error);
    void schedule_model_poll(int delay_ms);
    void trim_message_history();

    wxPanel*      m_header{nullptr};
    wxButton*     m_collapse_btn{nullptr};
    wxStaticText* m_title{nullptr};
    bool          m_show_header{true};

    wxPanel*      m_body{nullptr};
    wxTextCtrl*   m_history_ctrl{nullptr};
    wxTextCtrl*   m_input_ctrl{nullptr};
    wxButton*     m_send_btn{nullptr};
    wxStaticText* m_status{nullptr};

    OllamaClient               m_client;
    std::vector<OllamaMessage> m_messages;
    std::shared_ptr<std::atomic<bool>> m_alive;
    uint64_t                   m_request_gen{0};
    wxTimer*                   m_poll_timer{nullptr};
    bool                       m_pull_in_progress{false};
    bool                       m_busy{false};
    bool                       m_collapsed{false};
};

}} // namespace

#endif


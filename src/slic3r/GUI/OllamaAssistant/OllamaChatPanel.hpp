#ifndef slic3r_OllamaChatPanel_hpp_
#define slic3r_OllamaChatPanel_hpp_

#include "../GUI_Utils.hpp"
#include "../MakerWorld/MakerWorldImportFlow.hpp"
#include "OllamaClient.hpp"
#include "OllamaDiagnosticPipeline.hpp"
#include "OllamaAgentController.hpp"
#include "OllamaExecutionPolicy.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

class Button;
class TextInput;
class wxButton;
class wxChoice;
class wxPanel;
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
    void begin_thinking_block();
    void append_thinking_line(const wxString& line);
    void append_thinking_text(const wxString& text);
    void clear_thinking_block();
    void refresh_chat_display();
    wxString thinking_role_label() const;
    void set_busy(bool busy);
    void on_send(wxCommandEvent& event);
    void on_models_loaded(const std::vector<std::string>& models, const std::string& error);
    void on_diagnosis_response(const std::string& diagnosis_text, const std::string& user_utf8, const std::string& error);
    void start_diagnostic_turn(const std::string& user_utf8);
    void launch_proposal_llm(const std::string& user_utf8, const OllamaDiagnosis& diagnosis,
                             std::vector<std::string> keys, const nlohmann::json& wiki_context,
                             const nlohmann::json& settings_analysis, int critic_attempt);
    void on_proposal_llm_response(const std::string& assistant_text, const std::string& error,
                                    const std::string& user_utf8, std::vector<std::string> keys,
                                    const nlohmann::json& wiki_context, int critic_attempt);
    void on_chat_response(const std::string& assistant_text, const std::string& error);
    void launch_single_chat_llm(std::string final_user_msg);
    void schedule_model_poll(int delay_ms);
    void trim_message_history();
    void trim_history_display();
    void reset_conversation();
    void set_assistant_mode(bool apply_mode);
    bool apply_mode() const { return m_apply_mode; }
    void on_agent_finished(const OllamaAgentRunResult& result);
    void refresh_mode_ui();
    wxString system_welcome_message() const;
    void set_status_text(const wxString& text);
    MakerWorldFlowUiCallbacks makerworld_flow_callbacks();
    void retry_last_chat_simple();
    bool run_symptom_fallback_turn(const std::string& user_utf8);
    void update_model_label_ui();
    std::string resolve_installed_model(const std::vector<std::string>& models, const std::string& want) const;

    wxPanel*      m_header{nullptr};
    wxPanel*      m_status_host{nullptr};
    wxButton*     m_collapse_btn{nullptr};
    wxStaticText* m_title{nullptr};
    bool          m_show_header{true};

    wxPanel*      m_body{nullptr};
    wxTextCtrl*   m_history_ctrl{nullptr};
    wxString      m_persistent_chat;
    wxString      m_thinking_block;
    TextInput*    m_input_field{nullptr};
    wxTextCtrl*   m_input_ctrl{nullptr};
    Button*       m_send_btn{nullptr};
    Button*       m_reset_btn{nullptr};
    wxChoice*     m_mode_choice{nullptr};
    wxStaticText* m_mode_label{nullptr};
    wxStaticText* m_model_label{nullptr};
    wxStaticText* m_status{nullptr};
    bool          m_apply_mode{true};
    bool          m_agent_mode{false};

    std::unique_ptr<OllamaAgentController> m_agent_controller;

    OllamaClient               m_client;
    std::string                m_model;
    std::vector<std::string>   m_available_models;
    std::vector<OllamaMessage> m_messages;
    std::shared_ptr<std::atomic<bool>> m_alive;
    uint64_t                   m_request_gen{0};
    wxTimer*                   m_poll_timer{nullptr};
    bool                       m_pull_in_progress{false};
    int                        m_model_poll_failures{0};
    bool                       m_busy{false};
    bool                       m_collapsed{false};
    int                        m_empty_reply_retries{0};
};

}} // namespace

#endif


#ifndef slic3r_OllamaChatPanel_hpp_
#define slic3r_OllamaChatPanel_hpp_

#include "../GUI_Utils.hpp"
#include "../MakerWorld/MakerWorldImportFlow.hpp"
#include "../AIPipeline/PrintJobState.hpp"
#include "OllamaClient.hpp"
#include "OllamaAgentController.hpp"
#include "OllamaExecutionPolicy.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Button;
class ComboBox;
class ProgressBar;
class TextInput;
class wxButton;
class wxPanel;
class wxStaticText;
class wxTextCtrl;
class wxTimer;

namespace Slic3r { namespace GUI {

class OllamaChatMessageList;
enum class ChatMessageRole;
enum class ChatMessageKind;

namespace AIPipeline {
class PrintJobOrchestrator;
struct PrintJobUiCallbacks;
}

class OllamaChatPanel : public wxPanel
{
public:
    explicit OllamaChatPanel(wxWindow* parent, bool show_header = true);
    ~OllamaChatPanel() override;

    void refresh_models(); // keeps Ollama warm / triggers auto-pull if needed
    void submit_text_and_send(const wxString& text);
    void set_input_text(const wxString& text);
    void focus_input();

    void set_collapsed(bool collapsed);
    bool is_collapsed() const { return m_collapsed; }

    /** Most recently created chat panel (nullptr when none is alive). Lets the
     *  agent loop route makerworld_find_and_print through the panel-owned
     *  orchestrator (with real chat/busy callbacks) instead of a detached one. */
    static OllamaChatPanel* active_panel();

    /** Start an end-to-end find-and-print job on the panel-owned orchestrator
     *  with this panel's UI callbacks. Returns false when the orchestrator flag
     *  is off, a job is already active, or start fails. Main thread only. */
    bool start_orchestrator_find_and_print(const std::string& query);

private:
    void load_settings();
    void save_settings();
    void ensure_ollama_running();
    void ensure_default_model_ready(const std::vector<std::string>& models);
    void append_chat(const wxString& role, const wxString& text);
    void append_chat_message(ChatMessageRole role, const wxString& text,
                             ChatMessageKind kind /* = Normal; see .cpp */);
    void begin_thinking_block();
    void append_thinking_line(const wxString& line);
    void append_thinking_text(const wxString& text);
    void clear_thinking_block();
    wxString thinking_role_label() const;
    void set_busy(bool busy);
    void on_send(wxCommandEvent& event);
    // on_send routing helpers (Extract-Method decomposition; behavior-preserving).
    // Each route_* returns true when it fully handled the turn (on_send then returns).
    bool current_plate_has_model() const;
    bool route_orchestrator_reply(const std::string& user_utf8);
    bool route_garbled_input(const std::string& user_utf8);
    bool route_acquisition(const std::string& user_utf8, bool plate_has_model);
    bool route_makerworld_bypass(const std::string& user_utf8);
    std::string build_send_user_message(const wxString& user_text, const std::string& user_utf8);
    bool route_assist_loop(const std::string& user_msg, const std::string& user_utf8, bool plate_has_model);
    void dispatch_single_shot_chat(const std::string& user_msg, const std::string& user_utf8);
    void on_models_loaded(const std::vector<std::string>& models, const std::string& error);
    void start_assist_loop_turn(const std::string& user_utf8);
    void on_assist_loop_finished(const OllamaAgentRunResult& result);
    void on_chat_response(const std::string& assistant_text, const std::string& error);
    void schedule_model_poll(int delay_ms);
    void trim_message_history();
    void reset_conversation();
    void set_assistant_mode(bool apply_mode);
    void update_system_welcome_in_chat();
    bool apply_mode() const { return m_apply_mode; }
    void refresh_mode_ui();
    // Re-apply the (possibly elided) input placeholder for the current width.
    void update_input_hint();
    wxString system_welcome_message() const;
    void set_status_text(const wxString& text);
    MakerWorldFlowUiCallbacks makerworld_flow_callbacks();
    // Orchestrator-facing callbacks: progress bubbles + step channel + stop UI.
    AIPipeline::PrintJobUiCallbacks orchestrator_ui_callbacks();
    void on_pipeline_step(AIPipeline::PrintJobState state, const wxString& detail);
    void hide_pipeline_progress();
    // Token streaming into the pending bubble (single-chat path).
    OllamaClient::StreamCallback make_stream_callback(uint64_t gen);
    void on_stream_chunk(const std::string& chunk);
    // Phase 3: when the orchestrator flag is on and the LLM proposes a full
    // "find and print" intent, start the end-to-end job instead of the legacy
    // single-shot MakerWorld flow. Consumed find_and_print actions are removed
    // from `root`. Returns true when a job was started.
    bool maybe_start_orchestrator_job(nlohmann::json& root, const std::string& user_req);
    void retry_last_chat_simple();
    void update_model_label_ui();
    std::string resolve_installed_model(const std::vector<std::string>& models, const std::string& want) const;

    wxPanel*      m_header{nullptr};
    wxPanel*      m_status_host{nullptr};
    wxButton*     m_collapse_btn{nullptr};
    wxStaticText* m_title{nullptr};
    bool          m_show_header{true};

    wxPanel*      m_body{nullptr};
    OllamaChatMessageList* m_history_list{nullptr};
    TextInput*    m_input_field{nullptr};
    wxTextCtrl*   m_input_ctrl{nullptr};
    Button*       m_send_btn{nullptr};
    Button*       m_reset_btn{nullptr};
    ComboBox*     m_mode_combo{nullptr};
    wxStaticText* m_mode_label{nullptr};
    ComboBox*     m_model_combo{nullptr};
    wxStaticText* m_status{nullptr};
    bool          m_apply_mode{true};

    // B3: compact pipeline progress strip (visible only during an active job).
    wxPanel*      m_pipeline_panel{nullptr};
    wxStaticText* m_pipeline_step_label{nullptr};
    ProgressBar*  m_pipeline_gauge{nullptr};
    Button*       m_pipeline_stop_btn{nullptr};

    // Streaming state for the in-flight request (guarded by m_request_gen).
    std::string   m_stream_buf;

    // Full placeholder text for the input field (elided to fit on narrow widths).
    wxString      m_input_hint_full;

    std::unique_ptr<OllamaAgentController>            m_assist_controller;
    std::unique_ptr<AIPipeline::PrintJobOrchestrator> m_orchestrator;

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

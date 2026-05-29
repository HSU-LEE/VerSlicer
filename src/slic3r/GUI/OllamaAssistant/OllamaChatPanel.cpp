#include "OllamaChatPanel.hpp"
#include "OllamaActionExecutor.hpp"

#include "../BambuSmartPrint/BambuSmartPrintUi.hpp"
#include "../GUI_App.hpp"
#include "../I18N.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include <wx/utils.h>
#include <wx/filefn.h>
#include <wx/settings.h>
#include <wx/weakref.h>

namespace Slic3r { namespace GUI {

namespace {

constexpr const char* kOllamaSection = "ollama";
constexpr const char* kDefaultModel  = "llama3.2";
constexpr const char* kDefaultHost   = "http://127.0.0.1:11434";

std::string last_user_request_text(const std::vector<OllamaMessage>& messages)
{
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->role != "user")
            continue;
        const std::string& s = it->content;
        const std::string marker = "\n\nUser request:\n";
        const auto pos = s.rfind(marker);
        if (pos != std::string::npos)
            return s.substr(pos + marker.size());
        return s;
    }
    return {};
}

static bool contains_support_intent(const std::string& s)
{
    return s.find("서포트") != std::string::npos || s.find("support") != std::string::npos;
}

static bool contains_flip_intent(const std::string& s)
{
    return s.find("뒤집") != std::string::npos || s.find("flip") != std::string::npos;
}

static bool contains_file_intent(const std::string& s)
{
    // Only treat explicit file workflow as "file intent".
    return s.find("저장") != std::string::npos || s.find("열어") != std::string::npos || s.find("불러") != std::string::npos ||
           s.find("내보내") != std::string::npos || s.find("export") != std::string::npos || s.find("save") != std::string::npos ||
           s.find("open") != std::string::npos || s.find("import") != std::string::npos;
}

static void patch_common_intents(nlohmann::json& root, const std::string& user_req)
{
    if (!root.contains("actions") || !root["actions"].is_array())
        return;
    const bool wants_support = contains_support_intent(user_req);
    const bool wants_flip    = contains_flip_intent(user_req);
    const bool wants_file    = contains_file_intent(user_req);

    // Drop save/export actions unless user explicitly asked for file workflow.
    {
        nlohmann::json filtered = nlohmann::json::array();
        for (const auto& a : root["actions"]) {
            if (!a.is_object()) {
                filtered.push_back(a);
                continue;
            }
            const std::string type = a.value("type", "");
            if (type == "save_project")
                continue;
            if (!wants_file && type == "menu_item") {
                const std::string menu = a.value("menu", "");
                if (menu == "File" || menu == "파일")
                    continue;
            }
            filtered.push_back(a);
        }
        root["actions"] = filtered;
    }
    for (auto& a : root["actions"]) {
        if (!a.is_object())
            continue;
        const std::string type = a.value("type", "");
        if (type == "set_config") {
            if (a.contains("preset") && a["preset"].is_string() && a["preset"].get<std::string>().empty())
                a["preset"] = "print";
            if (!a.contains("options") || !a["options"].is_object())
                continue;
            if (a["options"].empty() && wants_support) {
                a["options"]["enable_support"] = true;
            }
        } else if ((type == "rotate" || type == "translate" || type == "scale") && wants_flip) {
            // If user asked to "flip", but the model didn't specify angles, assume 180deg around X.
            if (type == "rotate") {
                if (!a.contains("x")) a["x"] = 180.0;
                if (!a.contains("y")) a["y"] = 0.0;
                if (!a.contains("z")) a["z"] = 0.0;
            }
        }
    }

    // If user asked to flip but there is no rotate action, inject one.
    if (wants_flip) {
        bool has_rotate = false;
        for (const auto& a : root["actions"])
            if (a.is_object() && a.value("type", "") == "rotate")
                has_rotate = true;
        if (!has_rotate) {
            root["actions"].push_back({{"type","rotate"},{"x",180.0},{"y",0.0},{"z",0.0}});
        }
    }

    // If user asked for supports, ensure enable_support=true is present even if the model forgot it.
    if (wants_support) {
        bool has_enable_support = false;
        for (const auto& a : root["actions"]) {
            if (!a.is_object() || a.value("type", "") != "set_config")
                continue;
            if (a.contains("options") && a["options"].is_object() && a["options"].contains("enable_support"))
                has_enable_support = true;
        }
        if (!has_enable_support) {
            root["actions"].push_back({
                {"type","set_config"},
                {"preset","print"},
                {"options", {{"enable_support", true}}}
            });
        }
    }
}

} // namespace

OllamaChatPanel::OllamaChatPanel(wxWindow* parent, bool show_header)
    : wxPanel(parent, wxID_ANY)
    , m_client(kDefaultHost)
    , m_alive(std::make_shared<std::atomic<bool>>(true))
    , m_show_header(show_header)
{
    SlicePilotUi::apply_panel_chrome(this);

    auto* topsizer = new wxBoxSizer(wxVERTICAL);

    // Header (small, chat-like).
    if (m_show_header) {
        m_header = new wxPanel(this);
        m_header->SetBackgroundColour(SlicePilotUi::Theme::surface_alt());
        auto* header_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_collapse_btn     = new wxButton(m_header, wxID_ANY, "–", wxDefaultPosition, wxSize(FromDIP(26), FromDIP(22)));
        m_title            = new wxStaticText(m_header, wxID_ANY, _L("Ollama Assistant"));
        m_title->SetForegroundColour(SlicePilotUi::Theme::text());
        header_sizer->Add(m_collapse_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
        header_sizer->Add(m_title, 1, wxALIGN_CENTER_VERTICAL);
        m_header->SetSizer(header_sizer);
        topsizer->Add(m_header, 0, wxEXPAND);
    }

    // Body
    m_body = new wxPanel(this);
    m_body->SetBackgroundColour(SlicePilotUi::Theme::background());
    auto* body_sizer = new wxBoxSizer(wxVERTICAL);

    // Model is fixed (llama3.2) to make the UI hands-free.
    auto* settings_row = new wxBoxSizer(wxHORIZONTAL);
    auto* model_label  = new wxStaticText(m_body, wxID_ANY, wxString::Format("%s %s", _L("Model"), kDefaultModel));
    model_label->SetForegroundColour(SlicePilotUi::Theme::text_muted());
    model_label->SetFont(wxGetApp().small_font());
    settings_row->Add(model_label, 1, wxALIGN_CENTER_VERTICAL);
    body_sizer->Add(settings_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));

    m_history_ctrl = new wxTextCtrl(m_body, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize,
                                    wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxTE_DONTWRAP);
    m_history_ctrl->SetBackgroundColour(SlicePilotUi::Theme::surface());
    body_sizer->Add(m_history_ctrl, 1, wxEXPAND | wxALL, FromDIP(8));

    m_input_ctrl = new wxTextCtrl(m_body, wxID_ANY, {}, wxDefaultPosition, wxSize(-1, FromDIP(54)),
                                  wxTE_MULTILINE | wxTE_PROCESS_ENTER);
    m_input_ctrl->SetHint(_L("Ask for setting changes, e.g. “infill 20%” or “wall loops 3”."));
    body_sizer->Add(m_input_ctrl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

    auto* bottom_row = new wxBoxSizer(wxHORIZONTAL);
    m_status         = new wxStaticText(m_body, wxID_ANY, _L("Ready"));
    m_status->SetForegroundColour(SlicePilotUi::Theme::text_muted());
    m_status->SetFont(wxGetApp().small_font());
    bottom_row->Add(m_status, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    m_send_btn = new wxButton(m_body, wxID_ANY, _L("Send"), wxDefaultPosition, wxSize(FromDIP(88), FromDIP(30)));
    m_send_btn->SetMinSize(wxSize(FromDIP(88), FromDIP(30)));
    m_send_btn->SetFont(wxGetApp().small_font());
    m_send_btn->SetForegroundColour(SlicePilotUi::Theme::text());
    m_send_btn->SetBackgroundColour(SlicePilotUi::Theme::surface_alt());
    bottom_row->Add(m_send_btn, 0, wxALIGN_CENTER_VERTICAL);
    body_sizer->Add(bottom_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));

    m_body->SetSizer(body_sizer);
    topsizer->Add(m_body, 1, wxEXPAND);

    SetSizer(topsizer);

    m_send_btn->Bind(wxEVT_BUTTON, &OllamaChatPanel::on_send, this);
    m_input_ctrl->Bind(wxEVT_TEXT_ENTER, &OllamaChatPanel::on_send, this);
    if (m_collapse_btn)
        m_collapse_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { set_collapsed(!m_collapsed); });

    load_settings();
    m_messages.push_back({"system", OllamaActionExecutor::build_system_prompt()});
    append_chat(_L("System"), _L("Ask to change print settings or move/scale/rotate selected models."));

    m_poll_timer = new wxTimer(this);
    m_poll_timer->Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        if (!m_alive->load() || wxGetApp().is_closing())
            return;
        refresh_models();
    });

    ensure_ollama_running();
}

OllamaChatPanel::~OllamaChatPanel()
{
    m_alive->store(false);
    ++m_request_gen;
    if (m_poll_timer) {
        m_poll_timer->Stop();
        m_poll_timer = nullptr;
    }
}

void OllamaChatPanel::schedule_model_poll(int delay_ms)
{
    if (!m_poll_timer || !m_alive->load())
        return;
    m_poll_timer->StartOnce(delay_ms);
}

void OllamaChatPanel::trim_message_history()
{
    constexpr size_t kMaxTurns = 16;
    if (m_messages.size() <= 1)
        return;
    const size_t keep = 1 + kMaxTurns * 2;
    if (m_messages.size() <= keep)
        return;
    std::vector<OllamaMessage> trimmed;
    trimmed.reserve(keep);
    trimmed.push_back(m_messages.front());
    trimmed.insert(trimmed.end(), m_messages.end() - (keep - 1), m_messages.end());
    m_messages.swap(trimmed);
}

void OllamaChatPanel::submit_text_and_send(const wxString& text)
{
    if (!m_input_ctrl)
        return;
    m_input_ctrl->SetValue(text);
    wxCommandEvent evt;
    on_send(evt);
}

void OllamaChatPanel::set_collapsed(bool collapsed)
{
    m_collapsed = collapsed;
    if (m_body)
        m_body->Show(!collapsed);
    if (m_collapse_btn)
        m_collapse_btn->SetLabel(collapsed ? "+" : "–");
    Layout();
    if (GetParent())
        GetParent()->Layout();
}

void OllamaChatPanel::load_settings()
{
    if (!wxGetApp().app_config)
        return;
    const std::string model = wxGetApp().app_config->get(kOllamaSection, "model");
    // Always use local Ollama.
    m_client.set_base_url(kDefaultHost);
    (void)model;
}

void OllamaChatPanel::save_settings()
{
    if (!wxGetApp().app_config)
        return;
    wxGetApp().app_config->set(kOllamaSection, "model", kDefaultModel);
    wxGetApp().app_config->save();
}

void OllamaChatPanel::ensure_ollama_running()
{
    m_client.set_base_url(kDefaultHost);
    const auto alive = m_alive;
    m_client.list_models([alive, this](const std::vector<std::string>&, const std::string& error) {
        wxGetApp().CallAfter([alive, this, error]() {
            if (!alive->load() || wxGetApp().is_closing())
                return;
            if (error.empty()) {
                if (m_status)
                    m_status->SetLabel(_L("Ready"));
                return;
            }

            if (m_status)
                m_status->SetLabel(_L("Starting Ollama…"));
            const wxString candidates[] = {
                "/opt/homebrew/bin/ollama",
                "/usr/local/bin/ollama",
                "ollama"
            };
            wxString cmd;
            for (const auto& c : candidates) {
                if (c.Contains("/") && wxFileExists(c)) { cmd = c; break; }
                if (!c.Contains("/")) { cmd = c; break; }
            }
            if (cmd.empty()) cmd = "ollama";
            wxExecute(cmd + " serve", wxEXEC_ASYNC);
            schedule_model_poll(1200);
        });
    });
}

void OllamaChatPanel::refresh_models()
{
    if (!m_alive->load() || wxGetApp().is_closing())
        return;
    if (m_status)
        m_status->SetLabel(_L("Loading models…"));
    m_client.set_base_url(kDefaultHost);
    const auto alive = m_alive;
    m_client.list_models([alive, this](const std::vector<std::string>& models, const std::string& error) {
        wxGetApp().CallAfter([alive, this, models, error]() {
            if (!alive->load() || wxGetApp().is_closing())
                return;
            on_models_loaded(models, error);
        });
    });
}

void OllamaChatPanel::append_chat(const wxString& role, const wxString& text)
{
    if (!m_history_ctrl)
        return;
    m_history_ctrl->AppendText(role + ":\n" + text + "\n\n");
}

void OllamaChatPanel::set_busy(bool busy)
{
    m_busy = busy;
    if (m_send_btn)
        m_send_btn->Enable(!busy);
    if (m_input_ctrl)
        m_input_ctrl->Enable(!busy);
    if (busy)
        m_status->SetLabel(_L("Waiting for Ollama…"));
}

void OllamaChatPanel::on_send(wxCommandEvent&)
{
    if (m_busy)
        return;
    const wxString user_text = m_input_ctrl->GetValue().Trim();
    if (user_text.empty())
        return;

    save_settings();
    m_input_ctrl->Clear();
    append_chat(_L("You"), user_text);

    const std::string context = OllamaActionExecutor::build_context_json();
    const std::string user_msg =
        std::string("Current slicer context (JSON):\n") + context + "\n\nUser request:\n" + user_text.utf8_string();
    m_messages.push_back({"user", user_msg});
    trim_message_history();

    set_busy(true);
    const std::string model = kDefaultModel;
    const auto alive = m_alive;
    const uint64_t gen = ++m_request_gen;
    wxWeakRef<wxWindow> weak_panel(this);
    m_client.chat(model, m_messages, [alive, gen, weak_panel](const std::string& text, const std::string& error) {
        wxGetApp().CallAfter([alive, gen, weak_panel, text, error]() {
            if (!alive->load() || wxGetApp().is_closing())
                return;
            auto* panel = dynamic_cast<OllamaChatPanel*>(weak_panel.get());
            if (!panel || panel->m_request_gen != gen)
                return;
            panel->on_chat_response(text, error);
        });
    });
}

void OllamaChatPanel::on_models_loaded(const std::vector<std::string>& models, const std::string& error)
{
    if (!error.empty()) {
        m_status->SetLabel(wxString::Format(_L("Ollama: %s"), wxString::FromUTF8(error)));
        return;
    }

    ensure_default_model_ready(models);
    m_status->SetLabel(wxString::Format(_L("%u model(s)"), unsigned(models.size())));
}

void OllamaChatPanel::ensure_default_model_ready(const std::vector<std::string>& models)
{
    // If user didn't install a model yet, auto-pull the default Llama.
    bool has_default = false;
    for (const auto& m : models) {
        if (m == kDefaultModel || m.rfind(std::string(kDefaultModel) + ":", 0) == 0) {
            has_default = true;
            break;
        }
    }
    if (has_default) {
        m_pull_in_progress = false;
        return;
    }
    if (m_pull_in_progress)
        return;

    m_pull_in_progress = true;
    m_status->SetLabel(_L("Downloading llama3.2… (ollama pull)"));
    const wxString candidates[] = {
        "/opt/homebrew/bin/ollama",
        "/usr/local/bin/ollama",
        "ollama"
    };
    wxString cmd;
    for (const auto& c : candidates) {
        if (c.Contains("/") && wxFileExists(c)) { cmd = c; break; }
        if (!c.Contains("/")) { cmd = c; break; }
    }
    if (cmd.empty()) cmd = "ollama";
    wxExecute(cmd + " pull llama3.2", wxEXEC_ASYNC);
    schedule_model_poll(5000);
}

void OllamaChatPanel::on_chat_response(const std::string& assistant_text, const std::string& error)
{
    if (!m_alive->load() || wxGetApp().is_closing())
        return;

    set_busy(false);

    if (!error.empty()) {
        append_chat(_L("Error"), wxString::FromUTF8(error));
        m_status->SetLabel(wxString::FromUTF8(error));
        return;
    }

    wxString display;
    try {
        nlohmann::json root = OllamaActionExecutor::extract_action_json(assistant_text);
        patch_common_intents(root, last_user_request_text(m_messages));
        if (root.contains("message") && root["message"].is_string()) {
            display = wxString::FromUTF8(root["message"].get<std::string>());
        } else {
            // Don't dump raw JSON into the chat history; keep it readable.
            display = _L("OK.");
        }
        m_messages.push_back({"assistant", assistant_text});
        trim_message_history();
        const auto results = OllamaActionExecutor::execute(root);
        if (!results.empty()) {
            display += "\n\n" + _L("Applied:");
            for (const auto& r : results)
                display += "\n• " + wxString::FromUTF8(r.message);
        }
    } catch (const std::exception& e) {
        // If the model didn't follow the JSON-only contract, execute a safe local fallback for common intents.
        const std::string user_req = last_user_request_text(m_messages);
        if (contains_support_intent(user_req)) {
            nlohmann::json root;
            root["message"] = "Enabling supports.";
            root["actions"] = nlohmann::json::array({{
                {"type", "set_config"},
                {"preset", "print"},
                {"options", {{"enable_support", true}}}
            }});
            m_messages.push_back({"assistant", "Enabling supports."});
            const auto results = OllamaActionExecutor::execute(root);
            display = _L("Enabling supports.");
            if (!results.empty()) {
                display += "\n\n" + _L("Applied:");
                for (const auto& r : results)
                    display += "\n• " + wxString::FromUTF8(r.message);
            }
        } else {
            // Still show something short.
            display = wxString::FromUTF8(assistant_text);
            if (display.Length() > 600)
                display = display.Left(600) + "\n…";
            display += "\n\n" + wxString::Format(_L("(Could not parse actions: %s)"), e.what());
        }
    }

    append_chat(_L("Assistant"), display);
    m_status->SetLabel(_L("Ready"));
}

}} // namespace


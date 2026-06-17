#include "BBLCloudServiceAgent.hpp"
#include "BBLNetworkPlugin.hpp"

#include <boost/log/trivial.hpp>
#include "Http.hpp"
#include "slic3r/GUI/GUI_App.hpp"

#include <libslic3r/Utils.hpp>

#include <boost/filesystem.hpp>
#include <sstream>
#include <fstream>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <nlohmann/json.hpp>
namespace Slic3r {


namespace {

std::string convert_studio_language_to_api(std::string lang_code)
{
    boost::replace_all(lang_code, "_", "-");
    return lang_code;
}

std::string normalize_homepage_auth_command(std::string payload)
{
    if (payload.empty()) {
        return payload;
    }

    try {
        auto json = nlohmann::json::parse(payload);
        if (json.contains("command") && json["command"].is_string()) {
            std::string command = json["command"].get<std::string>();
            if (command == "studio_userlogin") {
                json["command"] = "studio_bambu_userlogin";
            } else if (command == "studio_useroffline") {
                json["command"] = "studio_bambu_useroffline";
            }
            return json.dump();
        }
    } catch (...) {
        boost::replace_first(payload, "\"command\":\"studio_userlogin\"", "\"command\":\"studio_bambu_userlogin\"");
        boost::replace_first(payload, "\"command\":\"studio_useroffline\"", "\"command\":\"studio_bambu_useroffline\"");
    }

    return payload;
}

} // namespace

std::map<std::string, std::string> BBLCloudServiceAgent::get_extra_header()
{
    std::map<std::string, std::string> extra_headers;
    extra_headers.emplace("X-BBL-Client-Type", "slicer");
    extra_headers.emplace("X-BBL-Client-Name", SLIC3R_APP_NAME);
    extra_headers.emplace("X-BBL-Client-Version", GUI::wxGetApp().get_bbl_client_version());
#if defined(__WINDOWS__)
#ifdef _M_X64
    extra_headers.emplace("X-BBL-OS-Type", "windows");
#else
    extra_headers.emplace("X-BBL-OS-Type", "windows_arm");
#endif
#elif defined(__APPLE__)
    extra_headers.emplace("X-BBL-OS-Type", "macos");
#elif defined(__LINUX__)
    extra_headers.emplace("X-BBL-OS-Type", "linux");
#endif

    int major = 0, minor = 0, micro = 0;
    wxGetOsVersion(&major, &minor, &micro);

    std::ostringstream os_version;
    os_version << major << "." << minor << "." << micro;
    extra_headers.emplace("X-BBL-OS-Version", os_version.str());

    auto& app = GUI::wxGetApp();
    if (app.app_config) {
        extra_headers.emplace("X-BBL-Device-ID", app.app_config->get("slicer_uuid"));
    }

    extra_headers.emplace("X-BBL-Language",
                          convert_studio_language_to_api(app.current_language_code_safe().ToStdString()));
    return extra_headers;
}

BBLCloudServiceAgent::BBLCloudServiceAgent() = default;

BBLCloudServiceAgent::~BBLCloudServiceAgent() = default;

// ============================================================================
// Lifecycle (merged from BBLAuthAgent)
// ============================================================================

int BBLCloudServiceAgent::init_log()
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_init_log();
    if (func && agent) {
        return func(agent);
    }
    return -1;
}

int BBLCloudServiceAgent::set_config_dir(std::string config_dir)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_set_config_dir();
    if (func && agent) {
        return func(agent, config_dir);
    }
    return -1;
}

int BBLCloudServiceAgent::set_cert_file(std::string folder, std::string filename)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_set_cert_file();
    if (func && agent) {
        return func(agent, folder, filename);
    }
    return -1;
}

int BBLCloudServiceAgent::set_country_code(std::string country_code)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_set_country_code();
    if (func && agent) {
        return func(agent, country_code);
    }
    return -1;
}

int BBLCloudServiceAgent::start()
{
    set_extra_http_header();

    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_start();
    if (func && agent) {
        return func(agent);
    }
    return -1;
}

// ============================================================================
// User Session Management (merged from BBLAuthAgent)
// ============================================================================

namespace {

std::string find_token_in_json(const nlohmann::json& j, int depth = 0)
{
    if (depth > 6 || !j.is_object())
        return {};
    for (const char* key : {"token", "access_token", "accessToken", "bearer_token"}) {
        if (j.contains(key) && j[key].is_string()) {
            const std::string tok = j[key].get<std::string>();
            if (!tok.empty())
                return tok;
        }
    }
    for (const auto& item : j.items()) {
        if (item.value().is_object()) {
            const std::string tok = find_token_in_json(item.value(), depth + 1);
            if (!tok.empty())
                return tok;
        }
    }
    return {};
}

std::string bbl_access_token_store_path()
{
    namespace fs = boost::filesystem;
    const fs::path dir = fs::path(data_dir()) / "cache";
    fs::create_directories(dir);
    return (dir / "bbl_access_token").string();
}

void persist_bbl_access_token(const std::string& token)
{
    if (token.empty())
        return;
    const std::string path = bbl_access_token_store_path();
    std::ofstream       out(path, std::ios::trunc);
    if (!out)
        return;
    out << token;
    out.close();
    boost::system::error_code ec;
    boost::filesystem::permissions(path, boost::filesystem::owner_read | boost::filesystem::owner_write, ec);
}

bool load_persisted_bbl_access_token(std::string& out)
{
    std::ifstream in(bbl_access_token_store_path());
    if (!in)
        return false;
    std::getline(in, out);
    boost::algorithm::trim(out);
    return !out.empty();
}

void clear_persisted_bbl_access_token()
{
    boost::system::error_code ec;
    boost::filesystem::remove(bbl_access_token_store_path(), ec);
}

bool login_info_indicates_offline(const std::string& user_info)
{
    if (user_info.empty())
        return true;
    try {
        const nlohmann::json j = nlohmann::json::parse(user_info);
        if (j.contains("command") && j["command"].is_string()) {
            const std::string cmd = j["command"].get<std::string>();
            return cmd.find("offline") != std::string::npos || cmd.find("logout") != std::string::npos;
        }
    } catch (...) {
    }
    return false;
}

std::string extract_access_token_from_user_info(const std::string& user_info)
{
    if (user_info.empty())
        return {};
    try {
        return find_token_in_json(nlohmann::json::parse(user_info));
    } catch (...) {
    }
    return {};
}

} // namespace

int BBLCloudServiceAgent::change_user(std::string user_info)
{
    const std::string tok = extract_access_token_from_user_info(user_info);
    if (!tok.empty()) {
        std::lock_guard<std::mutex> lock(m_token_mutex);
        m_cached_access_token = tok;
        persist_bbl_access_token(tok);
    }

    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_change_user();
    if (func && agent) {
        return func(agent, user_info);
    }
    return -1;
}

bool BBLCloudServiceAgent::is_user_login()
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_is_user_login();
    if (func && agent) {
        return func(agent);
    }
    return false;
}

int BBLCloudServiceAgent::user_logout(bool request)
{
    {
        std::lock_guard<std::mutex> lock(m_token_mutex);
        m_cached_access_token.clear();
    }
    clear_persisted_bbl_access_token();

    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_user_logout();
    if (func && agent) {
        return func(agent, request);
    }
    return -1;
}

std::string BBLCloudServiceAgent::get_user_id()
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_user_id();
    if (func && agent) {
        return func(agent);
    }
    return "";
}

std::string BBLCloudServiceAgent::get_user_name()
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_user_name();
    if (func && agent) {
        return func(agent);
    }
    return "";
}

std::string BBLCloudServiceAgent::get_user_avatar()
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_user_avatar();
    if (func && agent) {
        return func(agent);
    }
    return "";
}

std::string BBLCloudServiceAgent::get_user_nickname()
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_user_nickanme();
    if (func && agent) {
        return func(agent);
    }
    return "";
}

// ============================================================================
// Login UI Support (merged from BBLAuthAgent)
// ============================================================================

std::string BBLCloudServiceAgent::build_login_cmd()
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_build_login_cmd();
    if (func && agent) {
        return normalize_homepage_auth_command(func(agent));
    }
    return "";
}

std::string BBLCloudServiceAgent::build_logout_cmd()
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_build_logout_cmd();
    if (func && agent) {
        return normalize_homepage_auth_command(func(agent));
    }
    return "";
}

std::string BBLCloudServiceAgent::build_login_info()
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_build_login_info();
    if (func && agent) {
        return normalize_homepage_auth_command(func(agent));
    }
    return "";
}

// ============================================================================
// Token Access (merged from BBLAuthAgent)
// ============================================================================

std::string BBLCloudServiceAgent::get_access_token() const
{
    std::lock_guard<std::mutex> lock(m_token_mutex);
    if (!m_cached_access_token.empty())
        return m_cached_access_token;

    auto& plugin = BBLNetworkPlugin::instance();
    auto  agent  = plugin.get_agent();
    auto  func   = plugin.get_track_get_property();
    if (!func || !agent)
        return {};

    for (const char* key : {"access_token", "token", "accessToken", "bearer_token"}) {
        std::string tok;
        if (func(agent, key, tok, "string") == 0 && !tok.empty()) {
            m_cached_access_token = tok;
            persist_bbl_access_token(m_cached_access_token);
            return m_cached_access_token;
        }
    }
    std::string persisted;
    if (load_persisted_bbl_access_token(persisted)) {
        m_cached_access_token = std::move(persisted);
        return m_cached_access_token;
    }
    return {};
}

std::string BBLCloudServiceAgent::get_refresh_token() const
{
    // BBL DLL manages tokens internally, not exposed via function pointer
    return "";
}

bool BBLCloudServiceAgent::ensure_token_fresh(const std::string& reason)
{
    (void)reason;
    set_extra_http_header();

    // Reuse token cached by change_user(); get_access_token also reads track_get_property.
    if (!get_access_token().empty())
        return true;

    // Fall back: build_login_info JSON from the network plugin session store.
    const std::string info = build_login_info();
    if (!info.empty() && !login_info_indicates_offline(info)) {
        const std::string tok = extract_access_token_from_user_info(info);
        if (!tok.empty()) {
            std::lock_guard<std::mutex> lock(m_token_mutex);
            m_cached_access_token = tok;
            persist_bbl_access_token(tok);
            return true;
        }
    }

    std::string persisted;
    if (load_persisted_bbl_access_token(persisted)) {
        std::lock_guard<std::mutex> lock(m_token_mutex);
        m_cached_access_token = std::move(persisted);
        return true;
    }

    // Last resort: exchange bind ticket for access token (same path as OAuth redirect).
    if (is_user_login()) {
        auto& plugin = BBLNetworkPlugin::instance();
        auto  agent  = plugin.get_agent();
        auto  ticket_fn = plugin.get_request_bind_ticket();
        auto  token_fn  = plugin.get_get_my_token();
        if (agent && ticket_fn && token_fn) {
            std::string ticket;
            if (ticket_fn(agent, &ticket) == 0 && !ticket.empty()) {
                unsigned    http_code = 0;
                std::string body;
                if (get_my_token(std::move(ticket), &http_code, &body) == 0 && !body.empty()) {
                    try {
                        const nlohmann::json j = nlohmann::json::parse(body);
                        std::string          tok = find_token_in_json(j);
                        if (tok.empty() && j.contains("accessToken") && j["accessToken"].is_string())
                            tok = j["accessToken"].get<std::string>();
                        if (!tok.empty()) {
                            std::lock_guard<std::mutex> lock(m_token_mutex);
                            m_cached_access_token = tok;
                            persist_bbl_access_token(tok);
                            BOOST_LOG_TRIVIAL(info) << "[BBL] access token acquired via bind ticket";
                            return true;
                        }
                    } catch (...) {
                    }
                }
            }
        }
    }

    return is_user_login() && !get_access_token().empty();
}

// ============================================================================
// Server Connectivity
// ============================================================================

std::string BBLCloudServiceAgent::get_cloud_service_host()
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_bambulab_host();
    if (func && agent) {
        return func(agent);
    }
    return "";
}

std::string BBLCloudServiceAgent::get_cloud_login_url(const std::string& language)
{
    std::string host_url = get_cloud_service_host();
    if (host_url.empty()) {
        return "";
    }

    if (language.empty()) {
        return host_url + "/sign-in";
    }
    return host_url + "/" + language + "/sign-in";
}

int BBLCloudServiceAgent::connect_server()
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_connect_server();
    if (func && agent) {
        return func(agent);
    }
    return -1;
}

bool BBLCloudServiceAgent::is_server_connected()
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_is_server_connected();
    if (func && agent) {
        return func(agent);
    }
    return false;
}

int BBLCloudServiceAgent::refresh_connection()
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_refresh_connection();
    if (func && agent) {
        return func(agent);
    }
    return -1;
}

int BBLCloudServiceAgent::start_subscribe(std::string module)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_start_subscribe();
    if (func && agent) {
        return func(agent, module);
    }
    return -1;
}

int BBLCloudServiceAgent::stop_subscribe(std::string module)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_stop_subscribe();
    if (func && agent) {
        return func(agent, module);
    }
    return -1;
}

int BBLCloudServiceAgent::add_subscribe(std::vector<std::string> dev_list)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_add_subscribe();
    if (func && agent) {
        return func(agent, dev_list);
    }
    return -1;
}

int BBLCloudServiceAgent::del_subscribe(std::vector<std::string> dev_list)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_del_subscribe();
    if (func && agent) {
        return func(agent, dev_list);
    }
    return -1;
}

void BBLCloudServiceAgent::enable_multi_machine(bool enable)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_enable_multi_machine();
    if (func && agent) {
        func(agent, enable);
    }
}

// ============================================================================
// Settings Synchronization
// ============================================================================

int BBLCloudServiceAgent::get_user_presets(std::map<std::string, std::map<std::string, std::string>>* user_presets)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_user_presets();
    if (func && agent) {
        return func(agent, user_presets);
    }
    return -1;
}

std::string BBLCloudServiceAgent::request_setting_id(std::string name, std::map<std::string, std::string>* values_map, unsigned int* http_code)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_request_setting_id();
    if (func && agent) {
        return func(agent, name, values_map, http_code);
    }
    return "";
}

int BBLCloudServiceAgent::put_setting(std::string setting_id, std::string name, std::map<std::string, std::string>* values_map, unsigned int* http_code)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_put_setting();
    if (func && agent) {
        return func(agent, setting_id, name, values_map, http_code);
    }
    return -1;
}

int BBLCloudServiceAgent::get_setting_list(std::string bundle_version, ProgressFn pro_fn, WasCancelledFn cancel_fn)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_setting_list();
    if (func && agent) {
        return func(agent, bundle_version, pro_fn, cancel_fn);
    }
    return -1;
}

int BBLCloudServiceAgent::get_setting_list2(std::string bundle_version, CheckFn chk_fn, ProgressFn pro_fn, WasCancelledFn cancel_fn)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_setting_list2();
    if (func && agent) {
        return func(agent, bundle_version, chk_fn, pro_fn, cancel_fn);
    }
    return -1;
}

int BBLCloudServiceAgent::delete_setting(std::string setting_id)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_delete_setting();
    if (func && agent) {
        return func(agent, setting_id);
    }
    return -1;
}

// ============================================================================
// Cloud User Services
// ============================================================================

int BBLCloudServiceAgent::get_my_message(int type, int after, int limit, unsigned int* http_code, std::string* http_body)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_my_message();
    if (func && agent) {
        return func(agent, type, after, limit, http_code, http_body);
    }
    return -1;
}

int BBLCloudServiceAgent::check_user_task_report(int* task_id, bool* printable)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_check_user_task_report();
    if (func && agent) {
        return func(agent, task_id, printable);
    }
    return -1;
}

int BBLCloudServiceAgent::get_user_print_info(unsigned int* http_code, std::string* http_body)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_user_print_info();
    if (func && agent) {
        return func(agent, http_code, http_body);
    }
    return -1;
}

int BBLCloudServiceAgent::get_user_tasks(TaskQueryParams params, std::string* http_body)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_user_tasks();
    if (func && agent) {
        return func(agent, params, http_body);
    }
    return -1;
}

int BBLCloudServiceAgent::get_printer_firmware(std::string dev_id, unsigned* http_code, std::string* http_body)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_printer_firmware();
    if (func && agent) {
        return func(agent, dev_id, http_code, http_body);
    }
    return -1;
}

int BBLCloudServiceAgent::get_task_plate_index(std::string task_id, int* plate_index)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_task_plate_index();
    if (func && agent) {
        return func(agent, task_id, plate_index);
    }
    return -1;
}

int BBLCloudServiceAgent::get_user_info(int* identifier)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_user_info();
    if (func && agent) {
        return func(agent, identifier);
    }
    return -1;
}

int BBLCloudServiceAgent::get_subtask_info(std::string subtask_id, std::string* task_json, unsigned int* http_code, std::string* http_body)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_subtask_info();
    if (func && agent) {
        return func(agent, subtask_id, task_json, http_code, http_body);
    }
    return -1;
}

int BBLCloudServiceAgent::get_slice_info(std::string project_id, std::string profile_id, int plate_index, std::string* slice_json)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_slice_info();
    if (func && agent) {
        return func(agent, project_id, profile_id, plate_index, slice_json);
    }
    return -1;
}

int BBLCloudServiceAgent::query_bind_status(std::vector<std::string> query_list, unsigned int* http_code, std::string* http_body)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_query_bind_status();
    if (func && agent) {
        return func(agent, query_list, http_code, http_body);
    }
    return -1;
}

int BBLCloudServiceAgent::modify_printer_name(std::string dev_id, std::string dev_name)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_modify_printer_name();
    if (func && agent) {
        return func(agent, dev_id, dev_name);
    }
    return -1;
}

// ============================================================================
// Model Mall & Publishing
// ============================================================================

int BBLCloudServiceAgent::get_camera_url(std::string dev_id, std::function<void(std::string)> callback)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_camera_url();
    if (func && agent) {
        return func(agent, dev_id, callback);
    }
    if (callback)
        callback("");
    return -1;
}

int BBLCloudServiceAgent::get_design_staffpick(int offset, int limit, std::function<void(std::string)> callback)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_design_staffpick();
    if (func && agent) {
        return func(agent, offset, limit, callback);
    }
    return -1;
}

int BBLCloudServiceAgent::start_publish(PublishParams params, OnUpdateStatusFn update_fn, WasCancelledFn cancel_fn, std::string* out)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_start_publish();
    if (func && agent) {
        return func(agent, params, update_fn, cancel_fn, out);
    }
    return -1;
}

int BBLCloudServiceAgent::get_model_publish_url(std::string* url)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_model_publish_url();
    if (func && agent) {
        return func(agent, url);
    }
    return -1;
}

int BBLCloudServiceAgent::get_subtask(BBLModelTask* task, OnGetSubTaskFn getsub_fn)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_subtask();
    if (func && agent) {
        return func(agent, task, getsub_fn);
    }
    return -1;
}

int BBLCloudServiceAgent::get_model_mall_home_url(std::string* url)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_model_mall_home_url();
    if (func && agent) {
        return func(agent, url);
    }
    return -1;
}

int BBLCloudServiceAgent::get_model_mall_detail_url(std::string* url, std::string id)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_model_mall_detail_url();
    if (func && agent) {
        return func(agent, url, id);
    }
    return -1;
}

int BBLCloudServiceAgent::search_makerworld(const std::string& query, int limit, const std::string& locale,
                                            const std::string& printer_model, std::string* json_body,
                                            unsigned int* http_code, std::string* http_error)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_search_makerworld();
    if (func && agent && json_body && http_code && http_error) {
        return func(agent, query.c_str(), limit, locale.c_str(), printer_model.c_str(), json_body, http_code,
                      http_error);
    }
    return -1;
}

int BBLCloudServiceAgent::get_makerworld_download_url(const std::string& design_id, std::string* download_url,
                                                      std::string* filename, unsigned int* http_code,
                                                      std::string* http_error)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_makerworld_download_url();
    if (func && agent && download_url && filename && http_code && http_error) {
        return func(agent, design_id.c_str(), download_url, filename, http_code, http_error);
    }
    return -1;
}

int BBLCloudServiceAgent::get_my_profile(std::string token, unsigned int* http_code, std::string* http_body)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_my_profile();
    if (func && agent) {
        return func(agent, token, http_code, http_body);
    }
    return -1;
}

int BBLCloudServiceAgent::get_my_token(std::string ticket, unsigned int* http_code, std::string* http_body)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_my_token();
    if (func && agent) {
        return func(agent, ticket, http_code, http_body);
    }
    return -1;
}

// ============================================================================
// Analytics & Tracking
// ============================================================================

int BBLCloudServiceAgent::track_enable(bool enable)
{
    m_enable_track = enable;
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_track_enable();
    if (func && agent) {
        return func(agent, enable);
    }
    return -1;
}

int BBLCloudServiceAgent::track_remove_files()
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_track_remove_files();
    if (func && agent) {
        return func(agent);
    }
    return -1;
}

int BBLCloudServiceAgent::track_event(std::string evt_key, std::string content)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_track_event();
    if (func && agent) {
        return func(agent, evt_key, content);
    }
    return -1;
}

int BBLCloudServiceAgent::track_header(std::string header)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_track_header();
    if (func && agent) {
        return func(agent, header);
    }
    return -1;
}

int BBLCloudServiceAgent::track_update_property(std::string name, std::string value, std::string type)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_track_update_property();
    if (func && agent) {
        return func(agent, name, value, type);
    }
    return -1;
}

int BBLCloudServiceAgent::track_get_property(std::string name, std::string& value, std::string type)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_track_get_property();
    if (func && agent) {
        return func(agent, name, value, type);
    }
    return -1;
}

bool BBLCloudServiceAgent::get_track_enable()
{
    return m_enable_track;
}

// ============================================================================
// Ratings & Reviews
// ============================================================================

int BBLCloudServiceAgent::put_model_mall_rating(int design_id, int score, std::string content, std::vector<std::string> images, unsigned int& http_code, std::string& http_error)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_put_model_mall_rating();
    if (func && agent) {
        return func(agent, design_id, score, content, images, http_code, http_error);
    }
    return -1;
}

int BBLCloudServiceAgent::get_oss_config(std::string& config, std::string country_code, unsigned int& http_code, std::string& http_error)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_oss_config();
    if (func && agent) {
        return func(agent, config, country_code, http_code, http_error);
    }
    return -1;
}

int BBLCloudServiceAgent::put_rating_picture_oss(std::string& config, std::string& pic_oss_path, std::string model_id, int profile_id, unsigned int& http_code, std::string& http_error)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_put_rating_picture_oss();
    if (func && agent) {
        return func(agent, config, pic_oss_path, model_id, profile_id, http_code, http_error);
    }
    return -1;
}

int BBLCloudServiceAgent::get_model_mall_rating_result(int job_id, std::string& rating_result, unsigned int& http_code, std::string& http_error)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_model_mall_rating_result();
    if (func && agent) {
        return func(agent, job_id, rating_result, http_code, http_error);
    }
    return -1;
}

// ============================================================================
// Extra Features
// ============================================================================

int BBLCloudServiceAgent::set_extra_http_header()
{
    auto extra_headers = get_extra_header();
    Slic3r::Http::set_extra_headers(extra_headers);

    auto& plugin = BBLNetworkPlugin::instance();
    auto  agent  = plugin.get_agent();

    auto func = plugin.get_set_extra_http_header();
    if (func && agent) {
        return func(agent, extra_headers);
    }
    return -1;
}

int BBLCloudServiceAgent::get_mw_user_preference(std::function<void(std::string)> callback)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_mw_user_preference();
    if (func && agent) {
        return func(agent, callback);
    }
    return -1;
}

int BBLCloudServiceAgent::get_mw_user_4ulist(int seed, int limit, std::function<void(std::string)> callback)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_get_mw_user_4ulist();
    if (func && agent) {
        return func(agent, seed, limit, callback);
    }
    return -1;
}

std::string BBLCloudServiceAgent::get_version()
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto func = plugin.get_get_version();
    if (func) {
        return func();
    }
    return "";
}

// ============================================================================
// Cloud Callbacks
// ============================================================================

int BBLCloudServiceAgent::set_on_server_connected_fn(AppOnServerConnectedFn fn)
{
    m_app_on_server_connected_fn = fn;
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_set_on_server_connected_fn();
    if (func && agent) {
        // Register raw callback with DLL, wrap to inject CloudEvent
        return func(agent, [this](int return_code, int reason_code) {
            if (m_app_on_server_connected_fn) {
                m_app_on_server_connected_fn(CloudEvent{BBL_CLOUD_PROVIDER}, return_code, reason_code);
            }
        });
    }
    return -1;
}

int BBLCloudServiceAgent::set_on_http_error_fn(AppOnHttpErrorFn fn)
{
    m_app_on_http_error_fn = fn;
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_set_on_http_error_fn();
    if (func && agent) {
        // Register raw callback with DLL, wrap to inject CloudEvent
        return func(agent, [this](unsigned http_code, std::string http_body) {
            if (m_app_on_http_error_fn) {
                m_app_on_http_error_fn(CloudEvent{BBL_CLOUD_PROVIDER}, http_code, http_body);
            }
        });
    }
    return -1;
}

int BBLCloudServiceAgent::set_get_country_code_fn(GetCountryCodeFn fn)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_set_get_country_code_fn();
    if (func && agent) {
        return func(agent, fn);
    }
    return -1;
}

int BBLCloudServiceAgent::set_queue_on_main_fn(QueueOnMainFn fn)
{
    auto& plugin = BBLNetworkPlugin::instance();
    auto agent = plugin.get_agent();
    auto func = plugin.get_set_queue_on_main_fn();
    if (func && agent) {
        return func(agent, fn);
    }
    return -1;
}

} // namespace Slic3r

#include "OllamaMcpServer.hpp"

#include "../UiDispatch.hpp"
#include "OllamaConfig.hpp"
#include "OllamaToolGateway.hpp"

#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <future>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstdlib>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace Slic3r { namespace GUI {

namespace {

std::atomic<bool> g_stop{false};
std::thread       g_thread;
int               g_listen_fd{-1};
std::mutex        g_listen_mutex;
std::string       g_token;
std::atomic<int>  g_clients{0};
constexpr int     kMaxClients = 8;

std::string random_token()
{
    // arc4random_buf is a CSPRNG on macOS; std::mt19937 is not.
    unsigned char bytes[32];
    arc4random_buf(bytes, sizeof(bytes));
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char b : bytes)
        oss << std::setw(2) << static_cast<int>(b);
    return oss.str();
}

void write_cursor_config(int port)
{
    const std::string path = Slic3r::data_dir() + "/mcp.json";
    nlohmann::json    cfg  = {
        {"mcpServers",
         {{"verslicer",
           {{"url", "http://127.0.0.1:" + std::to_string(port) + "/mcp"},
            {"headers", {{"Authorization", "Bearer " + g_token}}}}}}},
    };
    // The file holds the bearer token, so keep it owner-only.
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return;
    ::fchmod(fd, 0600);
    const std::string body = cfg.dump(2);
    const bool ok = ::write(fd, body.data(), body.size()) == static_cast<ssize_t>(body.size());
    ::close(fd);
    if (!ok)
        return;
    BOOST_LOG_TRIVIAL(info) << "VerSlicer MCP config written to " << path;
}

/** Value of an HTTP header. Header names are case-insensitive (Node-based MCP
 *  clients send them lowercase), so match the name without regard to case. */
std::string header_value(const std::string& headers, const std::string& name)
{
    std::istringstream in(headers);
    std::string        line;
    while (std::getline(in, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        if (boost::iequals(boost::trim_copy(line.substr(0, colon)), name))
            return boost::trim_copy(line.substr(colon + 1));
    }
    return {};
}

bool authorized(const std::string& headers)
{
    const std::string auth = header_value(headers, "Authorization");
    if (auth.size() > 7 && boost::iequals(auth.substr(0, 7), "Bearer ") && auth.substr(7) == g_token)
        return true;
    return header_value(headers, "X-VerSlicer-Token") == g_token;
}

std::string http_response(int status, const std::string& body, const char* content_type)
{
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << (status == 200 ? " OK" : status == 202 ? " Accepted" : status == 401 ? " Unauthorized" : " Error")
        << "\r\nContent-Type: " << content_type
        << "\r\nContent-Length: " << body.size()
        << "\r\nAccess-Control-Allow-Origin: http://127.0.0.1"
        << "\r\nAccess-Control-Allow-Headers: Content-Type, Authorization, X-VerSlicer-Token"
        << "\r\nConnection: close\r\n\r\n"
        << body;
    return oss.str();
}

nlohmann::json rpc_result(const nlohmann::json& id, nlohmann::json result)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

nlohmann::json rpc_error(const nlohmann::json& id, int code, const std::string& message)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

nlohmann::json call_on_ui(const std::string& name, const nlohmann::json& arguments)
{
    auto done = std::make_shared<std::promise<nlohmann::json>>();
    std::future<nlohmann::json> fut = done->get_future();
    if (!ui_dispatch_open())
        return rpc_error(nullptr, -32000, "Application is closing");
    post_to_ui([name, arguments, done]() {
        if (wxGetApp().is_closing()) {
            done->set_value(rpc_error(nullptr, -32000, "Application is closing"));
            return;
        }
        done->set_value(OllamaToolGateway::call(name, arguments));
    });
    if (fut.wait_for(std::chrono::seconds(60)) != std::future_status::ready)
        return {{"content", nlohmann::json::array({{{"type", "text"}, {"text", "Timed out waiting for the slicer"}}})},
                {"isError", true}};
    return fut.get();
}

nlohmann::json handle_rpc(const nlohmann::json& msg)
{
    const std::string method = msg.value("method", "");
    const nlohmann::json id = msg.contains("id") ? msg["id"] : nlohmann::json();
    const nlohmann::json params = msg.contains("params") && msg["params"].is_object() ? msg["params"] : nlohmann::json::object();

    if (method == "initialize") {
        std::string version = params.value("protocolVersion", "2024-11-05");
        if (version.rfind("2024-", 0) != 0 && version.rfind("2025-", 0) != 0)
            version = "2024-11-05";
        return rpc_result(id, {
            {"protocolVersion", version},
            {"capabilities", {{"tools", {{"listChanged", false}}}}},
            {"serverInfo", {{"name", "verslicer"}, {"version", "1.0.0"}}},
        });
    }
    if (method == "ping")
        return rpc_result(id, nlohmann::json::object());
    if (method == "tools/list")
        return rpc_result(id, {{"tools", OllamaToolGateway::mcp_tools()}});
    if (method == "tools/call") {
        const std::string name = params.value("name", "");
        const nlohmann::json arguments = params.contains("arguments") ? params["arguments"] : nlohmann::json::object();
        if (name.empty())
            return rpc_error(id, -32602, "tools/call requires name");
        return rpc_result(id, call_on_ui(name, arguments));
    }
    if (!msg.contains("id"))
        return {};
    return rpc_error(id, -32601, "Method not found: " + method);
}

void handle_client(int fd)
{
    // A client that connects and goes quiet must not hold this thread forever.
    timeval rcv{10, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcv, sizeof(rcv));
    struct Slot { ~Slot() { --g_clients; } } slot;
    std::string data;
    char buf[4096];
    while (data.find("\r\n\r\n") == std::string::npos) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0)
            break;
        data.append(buf, static_cast<size_t>(n));
        if (data.size() > 1024 * 1024)
            break;
    }
    const auto header_end = data.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        ::close(fd);
        return;
    }
    const std::string headers = data.substr(0, header_end);
    std::string body = data.substr(header_end + 4);
    const size_t content_length =
        std::min<size_t>(std::strtoul(header_value(headers, "Content-Length").c_str(), nullptr, 10), 4 * 1024 * 1024);
    while (body.size() < content_length) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0)
            break;
        body.append(buf, static_cast<size_t>(n));
    }

    if (headers.rfind("OPTIONS ", 0) == 0) {
        const std::string resp = http_response(204, "", "text/plain");
        ::send(fd, resp.data(), resp.size(), 0);
        ::close(fd);
        return;
    }
    if (headers.find("POST /mcp") == std::string::npos) {
        const std::string resp = http_response(404, "{\"error\":\"POST /mcp\"}", "application/json");
        ::send(fd, resp.data(), resp.size(), 0);
        ::close(fd);
        return;
    }
    if (!authorized(headers)) {
        const std::string resp = http_response(401, "{\"error\":\"unauthorized\"}", "application/json");
        ::send(fd, resp.data(), resp.size(), 0);
        ::close(fd);
        return;
    }

    nlohmann::json reply;
    bool notification = false;
    try {
        const nlohmann::json msg = nlohmann::json::parse(body);
        notification = !msg.contains("id") && msg.value("method", "").rfind("notifications/", 0) == 0;
        reply = handle_rpc(msg);
    } catch (const std::exception& e) {
        reply = rpc_error(nullptr, -32700, e.what());
    }

    if (notification || reply.is_null() || reply.empty()) {
        const std::string resp = http_response(202, "", "application/json");
        ::send(fd, resp.data(), resp.size(), 0);
    } else {
        const std::string dumped = reply.dump();
        const std::string resp = http_response(200, dumped, "application/json");
        ::send(fd, resp.data(), resp.size(), 0);
    }
    ::close(fd);
}

void serve(int port)
{
    while (!g_stop.load()) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(g_listen_fd, &fds);
        timeval tv{0, 200000};
        const int ready = ::select(g_listen_fd + 1, &fds, nullptr, nullptr, &tv);
        if (ready <= 0)
            continue;
        sockaddr_in client{};
        socklen_t len = sizeof(client);
        const int fd = ::accept(g_listen_fd, reinterpret_cast<sockaddr*>(&client), &len);
        if (fd < 0)
            continue;
        if (g_clients.load() >= kMaxClients) {
            ::close(fd); // too many open requests at once
            continue;
        }
        ++g_clients;
        std::thread(handle_client, fd).detach();
    }
    (void) port;
}

} // namespace

void OllamaMcpServer::start()
{
    if (g_thread.joinable())
        return;
    if (!ollama_mcp_server_enabled()) {
        BOOST_LOG_TRIVIAL(info) << "VerSlicer MCP server is off (set ollama.mcp_server or VERSLICER_MCP_SERVER=1)";
        return;
    }
    g_stop = false;
    g_token = random_token();

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return;
    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int bound_port = 0;
    for (int port = 8765; port < 8780; ++port) {
        addr.sin_port = htons(static_cast<uint16_t>(port));
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            bound_port = port;
            break;
        }
    }
    if (bound_port == 0 || ::listen(fd, 8) != 0) {
        ::close(fd);
        BOOST_LOG_TRIVIAL(error) << "VerSlicer MCP server failed to bind 127.0.0.1";
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_listen_mutex);
        g_listen_fd = fd;
    }
    write_cursor_config(bound_port);
    BOOST_LOG_TRIVIAL(info) << "VerSlicer MCP listening on http://127.0.0.1:" << bound_port << "/mcp";
    g_thread = std::thread([bound_port]() { serve(bound_port); });
}

void OllamaMcpServer::stop()
{
    g_stop = true;
    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(g_listen_mutex);
        fd = g_listen_fd;
        g_listen_fd = -1;
    }
    if (fd >= 0)
        ::shutdown(fd, SHUT_RDWR);
    if (g_thread.joinable())
        g_thread.join();
    if (fd >= 0)
        ::close(fd);
}

}} // namespace

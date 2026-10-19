#include "ChatGptAccount.hpp"

#include "../UiDispatch.hpp"
#include "AiPrivacyConsent.hpp"
#include "OllamaToolGateway.hpp"

#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/Http.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <nlohmann/json.hpp>

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>

#include <wx/utils.h>

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>

namespace Slic3r { namespace GUI {

namespace {

constexpr const char* kAuthAuthorize = "https://auth.openai.com/api/accounts/authorize";
constexpr const char* kAuthToken     = "https://auth.openai.com/api/accounts/oauth/token";
constexpr const char* kOidcDiscovery = "https://auth.openai.com/.well-known/openid-configuration";
constexpr const char* kIssuer        = "https://auth.openai.com";
constexpr const char* kResource      = "https://api.openai.com/v1";
constexpr const char* kResponsesUrl  = "https://api.openai.com/v1/responses";
constexpr const char* kModelsUrl     = "https://api.openai.com/v1/models";
constexpr const char* kAgentName     = "VerSlicer";
constexpr const char* kScope =
    "openid profile email offline_access resource.invoke chatgpt.tokens.use.direct";

std::mutex& session_mutex()
{
    static std::mutex m;
    return m;
}

struct Session
{
    bool             loaded{false};
    nlohmann::json   doc{nlohmann::json::object()};
};

Session& account_session()
{
    static Session s;
    return s;
}

std::string account_path()
{
    return (boost::filesystem::path(data_dir()) / "chatgpt_account.json").string();
}

std::string host_id_path()
{
    return (boost::filesystem::path(data_dir()) / "chatgpt_host_id").string();
}

// Tokens live in the login Keychain; chatgpt_account.json is only read once
// to migrate sessions saved by older builds.
constexpr const char* kKeychainService = "com.verslicer.chatgpt";
constexpr const char* kKeychainAccount = "session";

CFMutableDictionaryRef keychain_query()
{
    CFMutableDictionaryRef q = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                         &kCFTypeDictionaryValueCallBacks);
    CFStringRef service = CFStringCreateWithCString(nullptr, kKeychainService, kCFStringEncodingUTF8);
    CFStringRef account = CFStringCreateWithCString(nullptr, kKeychainAccount, kCFStringEncodingUTF8);
    CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(q, kSecAttrService, service);
    CFDictionarySetValue(q, kSecAttrAccount, account);
    CFRelease(service);
    CFRelease(account);
    return q;
}

std::string keychain_read()
{
    CFMutableDictionaryRef q = keychain_query();
    CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(q, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef   data = nullptr;
    std::string out;
    if (SecItemCopyMatching(q, &data) == errSecSuccess && data) {
        CFDataRef bytes = static_cast<CFDataRef>(data);
        out.assign(reinterpret_cast<const char*>(CFDataGetBytePtr(bytes)), static_cast<size_t>(CFDataGetLength(bytes)));
        CFRelease(data);
    }
    CFRelease(q);
    return out;
}

bool keychain_write(const std::string& body)
{
    CFDataRef              data = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(body.data()), static_cast<CFIndex>(body.size()));
    CFMutableDictionaryRef q    = keychain_query();
    CFMutableDictionaryRef upd  = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                            &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(upd, kSecValueData, data);
    OSStatus st = SecItemUpdate(q, upd);
    if (st == errSecItemNotFound) {
        CFDictionarySetValue(q, kSecValueData, data);
        CFDictionarySetValue(q, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
        st = SecItemAdd(q, nullptr);
    }
    CFRelease(upd);
    CFRelease(q);
    CFRelease(data);
    return st == errSecSuccess;
}

void keychain_delete()
{
    CFMutableDictionaryRef q = keychain_query();
    SecItemDelete(q);
    CFRelease(q);
}

std::string url_encode(const std::string& value)
{
    std::ostringstream out;
    out << std::hex << std::uppercase;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out << static_cast<char>(c);
        else
            out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }
    return out.str();
}

std::string base64url_encode(const unsigned char* data, size_t len)
{
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        const unsigned int n = (static_cast<unsigned int>(data[i]) << 16)
            | ((i + 1 < len ? data[i + 1] : 0) << 8)
            | (i + 2 < len ? data[i + 2] : 0);
        out.push_back(table[(n >> 18) & 63]);
        out.push_back(table[(n >> 12) & 63]);
        if (i + 1 < len)
            out.push_back(table[(n >> 6) & 63]);
        if (i + 2 < len)
            out.push_back(table[n & 63]);
    }
    return out;
}

std::string base64url_decode(std::string in)
{
    for (char& c : in) {
        if (c == '-')
            c = '+';
        else if (c == '_')
            c = '/';
    }
    while (in.size() % 4)
        in.push_back('=');
    std::string out;
    out.reserve(in.size() * 3 / 4);
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    for (size_t i = 0; i + 3 < in.size(); i += 4) {
        const int a = val(in[i]);
        const int b = val(in[i + 1]);
        const int c = val(in[i + 2]);
        const int d = val(in[i + 3]);
        if (a < 0 || b < 0)
            break;
        out.push_back(static_cast<char>((a << 2) | (b >> 4)));
        if (in[i + 2] != '=' && c >= 0)
            out.push_back(static_cast<char>(((b & 15) << 4) | (c >> 2)));
        if (in[i + 3] != '=' && c >= 0 && d >= 0)
            out.push_back(static_cast<char>(((c & 3) << 6) | d));
    }
    return out;
}

std::string random_token(size_t nbytes)
{
    std::vector<unsigned char> buf(nbytes);
    if (RAND_bytes(buf.data(), static_cast<int>(buf.size())) != 1)
        return {};
    return base64url_encode(buf.data(), buf.size());
}

std::string sha256_b64url(const std::string& input)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  len = 0;
    if (EVP_Digest(input.data(), input.size(), digest, &len, EVP_sha256(), nullptr) != 1)
        return {};
    return base64url_encode(digest, len);
}

std::string uuid_urn()
{
    unsigned char b[16];
    if (RAND_bytes(b, sizeof(b)) != 1)
        return {};
    b[6] = static_cast<unsigned char>((b[6] & 0x0f) | 0x40);
    b[8] = static_cast<unsigned char>((b[8] & 0x3f) | 0x80);
    std::ostringstream out;
    out << "urn:uuid:" << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            out << '-';
        out << std::setw(2) << static_cast<int>(b[i]);
    }
    return out.str();
}

bool write_private_file(const std::string& path, const std::string& body)
{
    const std::string tmp = path + ".tmp";
    {
        boost::nowide::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file)
            return false;
        file << body;
        if (!file)
            return false;
    }
    boost::system::error_code ec;
    boost::filesystem::permissions(tmp, boost::filesystem::owner_read | boost::filesystem::owner_write, ec);
    boost::filesystem::rename(tmp, path, ec);
    if (ec)
        return false;
    boost::filesystem::permissions(path, boost::filesystem::owner_read | boost::filesystem::owner_write, ec);
    return true;
}

std::string read_file(const std::string& path)
{
    boost::nowide::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::string host_id()
{
    std::string existing = read_file(host_id_path());
    boost::trim(existing);
    if (existing.rfind("urn:uuid:", 0) == 0)
        return existing;
    const std::string created = uuid_urn();
    if (created.empty() || !write_private_file(host_id_path(), created + "\n"))
        return {};
    return created;
}

void load_session_locked()
{
    Session& s = account_session();
    if (s.loaded)
        return;
    s.loaded = true;
    s.doc    = nlohmann::json::object();
    std::string raw = keychain_read();
    bool        migrate = false;
    if (raw.empty()) {
        raw     = read_file(account_path());
        migrate = !raw.empty();
    }
    if (raw.empty())
        return;
    try {
        nlohmann::json parsed = nlohmann::json::parse(raw);
        if (parsed.is_object())
            s.doc = std::move(parsed);
    } catch (...) {
    }
    if (migrate && keychain_write(raw)) {
        boost::system::error_code ec;
        boost::filesystem::remove(account_path(), ec);
    }
}

bool save_session_locked()
{
    if (keychain_write(account_session().doc.dump()))
        return true;
    // No usable Keychain (rare, e.g. a locked headless session): keep the owner-only file.
    return write_private_file(account_path(), account_session().doc.dump(2));
}

struct HttpResult
{
    std::string body;
    std::string error;
    unsigned    status{0};
};

HttpResult http_request(const std::string& method, const std::string& url, const std::string& body,
                        const std::vector<std::pair<std::string, std::string>>& headers, long timeout_sec,
                        const std::shared_ptr<std::atomic<bool>>& cancel,
                        const std::function<void(Http*)>& bind_http)
{
    HttpResult out;
    Http http = method == "GET" ? Http::get(url) : Http::post(url);
    if (bind_http)
        bind_http(&http);
    http.timeout_connect(20).timeout_max(timeout_sec).size_limit(16 * 1024 * 1024);
    for (const auto& header : headers)
        http.header(header.first, header.second);
    if (method != "GET")
        http.set_post_body(body);
    if (cancel) {
        http.on_progress([&](Http::Progress, bool& stop) {
            if (cancel->load())
                stop = true;
        });
    }
    http.on_error([&](std::string response, std::string error, unsigned status) {
            out.body   = std::move(response);
            out.error  = std::move(error);
            out.status = status;
        })
        .on_complete([&](std::string response, unsigned status) {
            out.body   = std::move(response);
            out.status = status;
        })
        .perform_sync();
    if (bind_http)
        bind_http(nullptr);
    return out;
}

std::string json_error_message(const std::string& body)
{
    try {
        const nlohmann::json j = nlohmann::json::parse(body);
        if (j.contains("error")) {
            if (j["error"].is_string())
                return j["error"].get<std::string>();
            if (j["error"].is_object()) {
                if (j["error"].contains("message") && j["error"]["message"].is_string())
                    return j["error"]["message"].get<std::string>();
                if (j["error"].contains("code") && j["error"]["code"].is_string())
                    return j["error"]["code"].get<std::string>();
            }
        }
    } catch (...) {
    }
    return {};
}

std::string& jwks_cache()
{
    static std::string raw;
    return raw;
}

bool verify_rs256(const std::string& signing_input, const std::string& signature, const std::string& n_b64,
                  const std::string& e_b64)
{
    const std::string n_raw = base64url_decode(n_b64);
    const std::string e_raw = base64url_decode(e_b64);
    if (n_raw.empty() || e_raw.empty())
        return false;

    BIGNUM* n = BN_bin2bn(reinterpret_cast<const unsigned char*>(n_raw.data()), static_cast<int>(n_raw.size()), nullptr);
    BIGNUM* e = BN_bin2bn(reinterpret_cast<const unsigned char*>(e_raw.data()), static_cast<int>(e_raw.size()), nullptr);
    if (!n || !e) {
        BN_free(n);
        BN_free(e);
        return false;
    }

    RSA* rsa = RSA_new();
    if (!rsa || RSA_set0_key(rsa, n, e, nullptr) != 1) {
        RSA_free(rsa);
        BN_free(n);
        BN_free(e);
        return false;
    }

    EVP_PKEY* pkey = EVP_PKEY_new();
    if (!pkey || EVP_PKEY_assign_RSA(pkey, rsa) != 1) {
        RSA_free(rsa);
        EVP_PKEY_free(pkey);
        return false;
    }

    EVP_MD_CTX* md = EVP_MD_CTX_new();
    const bool ok = md && EVP_DigestVerifyInit(md, nullptr, EVP_sha256(), nullptr, pkey) == 1
        && EVP_DigestVerifyUpdate(md, signing_input.data(), signing_input.size()) == 1
        && EVP_DigestVerifyFinal(md, reinterpret_cast<const unsigned char*>(signature.data()), signature.size()) == 1;
    EVP_MD_CTX_free(md);
    EVP_PKEY_free(pkey);
    return ok;
}

bool audience_matches(const nlohmann::json& payload, const std::string& client_id)
{
    if (!payload.contains("aud"))
        return false;
    if (payload["aud"].is_string())
        return payload["aud"].get<std::string>() == client_id;
    if (payload["aud"].is_array()) {
        for (const auto& item : payload["aud"]) {
            if (item.is_string() && item.get<std::string>() == client_id)
                return true;
        }
    }
    return false;
}

std::string validate_id_token(const std::string& id_token, const std::string& client_id, const std::string& nonce,
                              std::string* email_out, std::string* subject_out)
{
    const auto first = id_token.find('.');
    const auto second = first == std::string::npos ? std::string::npos : id_token.find('.', first + 1);
    if (first == std::string::npos || second == std::string::npos)
        return "ChatGPT ID token is malformed.";
    const std::string signing_input = id_token.substr(0, second);
    const std::string signature     = base64url_decode(id_token.substr(second + 1));
    nlohmann::json    header;
    nlohmann::json    payload;
    try {
        header  = nlohmann::json::parse(base64url_decode(id_token.substr(0, first)));
        payload = nlohmann::json::parse(base64url_decode(id_token.substr(first + 1, second - first - 1)));
    } catch (...) {
        return "ChatGPT ID token could not be read.";
    }
    if (header.value("alg", "") != "RS256")
        return "ChatGPT ID token algorithm is not RS256.";
    const std::string kid = header.value("kid", "");
    if (kid.empty() || signature.empty())
        return "ChatGPT ID token is missing a signing key.";

    if (jwks_cache().empty()) {
        const HttpResult discovered = http_request("GET", kOidcDiscovery, {}, {}, 20, nullptr, nullptr);
        std::string      jwks_uri   = "https://auth.openai.com/.well-known/jwks.json";
        try {
            const nlohmann::json meta = nlohmann::json::parse(discovered.body);
            if (meta.contains("jwks_uri") && meta["jwks_uri"].is_string())
                jwks_uri = meta["jwks_uri"].get<std::string>();
        } catch (...) {
        }
        const HttpResult keys = http_request("GET", jwks_uri, {}, {}, 20, nullptr, nullptr);
        if (keys.status < 200 || keys.status >= 300 || keys.body.empty())
            return "Could not download OpenAI signing keys.";
        jwks_cache() = keys.body;
    }

    std::string n_b64;
    std::string e_b64;
    try {
        const nlohmann::json jwks = nlohmann::json::parse(jwks_cache());
        for (const auto& key : jwks.value("keys", nlohmann::json::array())) {
            if (key.value("kid", "") == kid && key.value("kty", "") == "RSA") {
                n_b64 = key.value("n", "");
                e_b64 = key.value("e", "");
                break;
            }
        }
    } catch (...) {
        return "OpenAI signing keys could not be read.";
    }
    if (n_b64.empty() || !verify_rs256(signing_input, signature, n_b64, e_b64))
        return "ChatGPT ID token signature is invalid.";
    if (payload.value("iss", "") != kIssuer)
        return "ChatGPT ID token issuer is invalid.";
    if (!audience_matches(payload, client_id))
        return "ChatGPT ID token audience does not match this app.";
    if (!nonce.empty() && payload.value("nonce", "") != nonce)
        return "ChatGPT ID token nonce does not match.";
    const auto exp = payload.value("exp", static_cast<int64_t>(0));
    if (exp > 0 && exp <= static_cast<int64_t>(std::time(nullptr)))
        return "ChatGPT ID token has expired.";
    const std::string subject = payload.value("sub", "");
    if (subject.empty())
        return "ChatGPT ID token has no account id.";
    if (subject_out)
        *subject_out = subject;
    if (email_out && payload.contains("email") && payload["email"].is_string())
        *email_out = payload["email"].get<std::string>();
    return {};
}

std::string query_value(const std::string& query, const std::string& key)
{
    const std::string needle = key + "=";
    size_t            pos    = 0;
    while (pos < query.size()) {
        if (query.compare(pos, needle.size(), needle) == 0) {
            const size_t start = pos + needle.size();
            const size_t end   = query.find('&', start);
            std::string  raw   = query.substr(start, end == std::string::npos ? std::string::npos : end - start);
            std::string  out;
            for (size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] == '%' && i + 2 < raw.size()) {
                    out.push_back(static_cast<char>(std::strtol(raw.substr(i + 1, 2).c_str(), nullptr, 16)));
                    i += 2;
                } else if (raw[i] == '+') {
                    out.push_back(' ');
                } else {
                    out.push_back(raw[i]);
                }
            }
            return out;
        }
        const size_t next = query.find('&', pos);
        if (next == std::string::npos)
            break;
        pos = next + 1;
    }
    return {};
}

struct CallbackResult
{
    std::string error;
    std::string code;
    std::string state;
    std::string client_id;
    std::string scope;
};

CallbackResult wait_for_callback(int listen_fd)
{
    CallbackResult out;
    struct timeval tv;
    tv.tv_sec  = 180;
    tv.tv_usec = 0;
    setsockopt(listen_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    const int client = ::accept(listen_fd, nullptr, nullptr);
    if (client < 0) {
        out.error = "ChatGPT sign-in timed out.";
        return out;
    }
    std::string request;
    char        buf[1024];
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192) {
        const ssize_t n = ::recv(client, buf, sizeof(buf), 0);
        if (n <= 0)
            break;
        request.append(buf, buf + n);
    }
    const bool denied = request.find("error=") != std::string::npos;
    const char* title = denied ? "연결이 취소되었습니다" : "연결되었습니다";
    const char* body  = denied
        ? "브라우저에서 허용을 누르지 않았습니다. 이 창을 닫고 VerSlicer에서 다시 연결해 주세요."
        : "ChatGPT 계정이 VerSlicer에 연결되었습니다. 이 창을 닫고 앱으로 돌아가 주세요.";
    const char* mark  = denied ? "!" : "✓";
    const char* tone  = denied ? "#E14747" : "#FF8F4A";
    const std::string html = std::string(
        "<!doctype html><html lang=\"ko\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>VerSlicer</title>"
        "<style>"
        "html,body{margin:0;height:100%;background:#f6f6f6;color:#363636;"
        "font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;}"
        "main{min-height:100%;display:flex;align-items:center;justify-content:center;padding:24px;}"
        "section{width:min(420px,100%);background:#fff;border:1px solid #eee;border-radius:16px;"
        "padding:36px 32px 32px;text-align:center;box-shadow:0 8px 28px rgba(0,0,0,.06);}"
        ".mark{width:56px;height:56px;margin:0 auto 18px;border-radius:50%;background:") + tone + ";"
        "color:#fff;font-size:28px;line-height:56px;font-weight:700;}"
        "h1{margin:0 0 10px;font-size:22px;font-weight:650;letter-spacing:-.02em;}"
        "p{margin:0;color:#6b6b6b;font-size:15px;line-height:1.55;}"
        ".brand{margin-top:22px;color:#acacac;font-size:12px;letter-spacing:.08em;}"
        "</style></head><body><main><section><div class=\"mark\">"
        + mark + "</div><h1>" + title + "</h1><p>" + body
        + "</p><div class=\"brand\">VERSLICER</div></section></main></body></html>";
    const std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: "
        + std::to_string(html.size()) + "\r\n\r\n" + html;
    ::send(client, response.data(), response.size(), 0);
    ::close(client);

    const auto line_end = request.find("\r\n");
    const std::string line = line_end == std::string::npos ? request : request.substr(0, line_end);
    const auto qpos = line.find('?');
    const auto sp   = line.find(' ', qpos == std::string::npos ? 0 : qpos);
    if (qpos == std::string::npos || sp == std::string::npos) {
        out.error = "ChatGPT callback was empty.";
        return out;
    }
    const std::string query = line.substr(qpos + 1, sp - qpos - 1);
    out.code      = query_value(query, "code");
    out.state     = query_value(query, "state");
    out.client_id = query_value(query, "client_id");
    out.scope     = query_value(query, "scope");
    out.error     = query_value(query, "error");
    if (!out.error.empty()) {
        const std::string desc = query_value(query, "error_description");
        if (!desc.empty())
            out.error = desc;
        return out;
    }
    if (out.code.empty())
        out.error = "ChatGPT did not return an authorization code.";
    return out;
}

bool scope_allows_inference(const std::string& scope)
{
    return scope.find("chatgpt.tokens.use.direct") != std::string::npos;
}

std::string apply_token_response(const nlohmann::json& token, const std::string& client_id, const std::string& nonce,
                                 const std::string& host, bool check_nonce)
{
    const std::string access  = token.value("access_token", "");
    const std::string refresh = token.value("refresh_token", "");
    const std::string id_token = token.value("id_token", "");
    const std::string scope   = token.value("scope", "");
    if (access.empty() || id_token.empty())
        return "ChatGPT token response was incomplete.";
    if (!scope_allows_inference(scope))
        return "ChatGPT plan use was not granted. Allow it in the browser and try again.";

    std::string email;
    std::string subject;
    const std::string id_error = validate_id_token(id_token, client_id, check_nonce ? nonce : "", &email, &subject);
    if (!id_error.empty())
        return id_error;

    std::lock_guard<std::mutex> lock(session_mutex());
    load_session_locked();
    const std::string previous_subject = account_session().doc.value("subject", "");
    if (!previous_subject.empty() && previous_subject != subject)
        return "ChatGPT account does not match the saved sign-in.";

    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    account_session().doc["email"]             = email;
    account_session().doc["issuer"]            = kIssuer;
    account_session().doc["subject"]           = subject;
    account_session().doc["client_id"]         = client_id;
    account_session().doc["ext_agent_host_id"] = host;
    account_session().doc["id_token"]          = id_token;
    account_session().doc["access_token"]      = access;
    if (!refresh.empty())
        account_session().doc["refresh_token"] = refresh;
    account_session().doc["token_type"] = token.value("token_type", "Bearer");
    account_session().doc["expires_in"] = token.value("expires_in", 3600);
    account_session().doc["expires_at"] = now + token.value("expires_in", 3600);
    account_session().doc["scope"]      = scope;
    account_session().doc["saved_at"]   = now;
    if (!save_session_locked())
        return "Could not store the ChatGPT sign-in on this Mac.";
    return {};
}

std::string refresh_locked_account_session()
{
    load_session_locked();
    const std::string refresh   = account_session().doc.value("refresh_token", "");
    const std::string client_id = account_session().doc.value("client_id", "");
    if (refresh.empty() || client_id.empty())
        return "ChatGPT sign-in has expired. Connect again.";
    const std::string form = "grant_type=refresh_token&client_id=" + url_encode(client_id)
        + "&refresh_token=" + url_encode(refresh) + "&resource=" + url_encode(kResource);
    const HttpResult response = http_request(
        "POST", kAuthToken, form, {{"Content-Type", "application/x-www-form-urlencoded"}}, 30, nullptr, nullptr);
    if (response.status == 400 || response.status == 401) {
        // The refresh token was revoked or expired (e.g. invalid_grant). Keeping it
        // would show the account as connected while every request fails, so forget
        // it and let the panel offer Connect again.
        BOOST_LOG_TRIVIAL(warning) << "ChatGPT refresh rejected (HTTP " << response.status << "): "
                                   << json_error_message(response.body) << "; clearing saved sign-in";
        account_session().doc = nlohmann::json::object();
        keychain_delete();
        boost::system::error_code ec;
        boost::filesystem::remove(account_path(), ec);
        return "ChatGPT sign-in has expired. Connect again.";
    }
    if (response.status < 200 || response.status >= 300) {
        const std::string message = json_error_message(response.body);
        return message.empty() ? "ChatGPT sign-in could not be renewed." : message;
    }
    try {
        const nlohmann::json token = nlohmann::json::parse(response.body);
        std::string          email;
        std::string          subject;
        const std::string    id_token = token.value("id_token", account_session().doc.value("id_token", ""));
        if (token.contains("id_token")) {
            const std::string id_error = validate_id_token(id_token, client_id, "", &email, &subject);
            if (!id_error.empty())
                return id_error;
            if (subject != account_session().doc.value("subject", ""))
                return "Renewed ChatGPT account does not match.";
        }
        if (!token.value("access_token", "").empty())
            account_session().doc["access_token"] = token["access_token"];
        if (token.contains("refresh_token") && token["refresh_token"].is_string())
            account_session().doc["refresh_token"] = token["refresh_token"];
        if (token.contains("id_token"))
            account_session().doc["id_token"] = id_token;
        if (!email.empty())
            account_session().doc["email"] = email;
        const int64_t now = static_cast<int64_t>(std::time(nullptr));
        account_session().doc["expires_at"] = now + token.value("expires_in", 3600);
        if (token.contains("scope") && token["scope"].is_string())
            account_session().doc["scope"] = token["scope"];
        if (!save_session_locked())
            return "Could not store the renewed ChatGPT sign-in.";
    } catch (...) {
        return "ChatGPT renewal response could not be read.";
    }
    return {};
}

std::string access_token(std::string* error)
{
    std::lock_guard<std::mutex> lock(session_mutex());
    load_session_locked();
    if (account_session().doc.value("access_token", "").empty()) {
        if (error)
            *error = "Connect a ChatGPT account first.";
        return {};
    }
    const int64_t expires = account_session().doc.value("expires_at", static_cast<int64_t>(0));
    if (expires > 0 && static_cast<int64_t>(std::time(nullptr)) + 120 >= expires) {
        const std::string refresh_error = refresh_locked_account_session();
        if (!refresh_error.empty()) {
            if (error)
                *error = refresh_error;
            return {};
        }
    }
    return account_session().doc.value("access_token", "");
}

nlohmann::json responses_tools(const std::string& tools_json)
{
    nlohmann::json functions = nlohmann::json::array();
    if (tools_json.empty())
        return functions;
    try {
        const nlohmann::json tools = nlohmann::json::parse(tools_json);
        if (!tools.is_array())
            return functions;
        for (const auto& tool : tools) {
            const nlohmann::json& fn = tool.contains("function") && tool["function"].is_object() ? tool["function"] : tool;
            if (!fn.contains("name") || !fn["name"].is_string())
                continue;
            nlohmann::json parameters = fn.contains("parameters") ? fn["parameters"] : nlohmann::json::object();
            if (!parameters.is_object())
                parameters = nlohmann::json::object();
            if (!parameters.contains("type") || !parameters["type"].is_string())
                parameters["type"] = "object";
            if (!parameters.contains("properties") || !parameters["properties"].is_object())
                parameters["properties"] = nlohmann::json::object();
            functions.push_back({
                {"type", "function"},
                {"name", fn["name"]},
                {"description", fn.value("description", "Slicer action")},
                {"parameters", std::move(parameters)},
            });
        }
    } catch (...) {
    }
    return functions;
}

std::string text_from_output_item(const nlohmann::json& item)
{
    std::string text;
    if (!item.contains("content") || !item["content"].is_array())
        return text;
    for (const auto& part : item["content"]) {
        if (part.value("type", "") == "output_text" && part.contains("text") && part["text"].is_string())
            text += part["text"].get<std::string>();
        else if (part.contains("text") && part["text"].is_string())
            text += part["text"].get<std::string>();
    }
    return text;
}

std::string actions_from_calls(const std::string& prose, const std::vector<std::pair<std::string, std::string>>& calls)
{
    if (calls.empty())
        return prose;
    nlohmann::json actions = nlohmann::json::array();
    std::string    errors;
    for (const auto& call : calls) {
        nlohmann::json args = nlohmann::json::object();
        try {
            nlohmann::json parsed = nlohmann::json::parse(call.second);
            if (parsed.is_object())
                args = std::move(parsed);
        } catch (...) {
        }
        const OllamaNormalizedTool normalized = OllamaToolGateway::normalize(call.first, args);
        if (!normalized.ok) {
            if (!errors.empty())
                errors += " ";
            errors += normalized.error;
            continue;
        }
        actions.push_back(normalized.action);
    }
    if (actions.empty())
        return prose.empty() ? errors : prose;
    nlohmann::json root = {{"message", prose.empty() ? errors : prose}, {"done", false}, {"actions", std::move(actions)}};
    return root.dump();
}

struct SseParse
{
    std::string text;
    std::string error;
    bool        completed{false};
    std::vector<std::pair<std::string, std::string>> calls;
};

void collect_output(const nlohmann::json& output, SseParse& parsed)
{
    if (!output.is_array())
        return;
    for (const auto& item : output) {
        const std::string type = item.value("type", "");
        if (type == "function_call") {
            parsed.calls.emplace_back(item.value("name", ""), item.value("arguments", ""));
        } else if (type == "message") {
            const std::string text = text_from_output_item(item);
            if (!text.empty())
                parsed.text = text;
        }
    }
}

SseParse parse_responses_sse(const std::string& body, const std::function<void(const std::string&)>& on_delta)
{
    SseParse parsed;
    std::istringstream stream(body);
    std::string        line;
    while (std::getline(stream, line)) {
        boost::trim_right_if(line, boost::is_any_of("\r"));
        if (line.rfind("data:", 0) != 0)
            continue;
        std::string payload = line.substr(5);
        boost::trim(payload);
        if (payload.empty() || payload == "[DONE]")
            continue;
        nlohmann::json event;
        try {
            event = nlohmann::json::parse(payload);
        } catch (...) {
            continue;
        }
        const std::string type = event.value("type", "");
        if (type == "response.output_text.delta") {
            const std::string delta = event.value("delta", "");
            if (!delta.empty()) {
                parsed.text += delta;
                if (on_delta)
                    on_delta(delta);
            }
        } else if (type == "response.output_item.done" && event.contains("item")) {
            collect_output(nlohmann::json::array({event["item"]}), parsed);
        } else if (type == "response.completed" && event.contains("response")) {
            parsed.completed = true;
            if (event["response"].contains("output"))
                collect_output(event["response"]["output"], parsed);
        } else if (type == "response.failed" || type == "response.incomplete" || type == "error") {
            parsed.error = json_error_message(payload);
            if (parsed.error.empty() && event.contains("response") && event["response"].contains("error"))
                parsed.error = event["response"]["error"].dump();
            if (parsed.error.empty())
                parsed.error = type;
        }
    }
    return parsed;
}

std::string pick_model(const std::string& want, const std::vector<std::string>& models)
{
    if (models.empty())
        return want;
    for (const auto& model : models) {
        if (model == want)
            return model;
    }
    const bool ollama_tag = want.empty() || want.find(':') != std::string::npos || want.find("qwen") != std::string::npos;
    if (ollama_tag)
        return models.front();
    return want;
}

} // namespace

bool ChatGptAccount::linked()
{
    std::lock_guard<std::mutex> lock(session_mutex());
    load_session_locked();
    return !account_session().doc.value("access_token", "").empty()
        && scope_allows_inference(account_session().doc.value("scope", ""));
}

std::string ChatGptAccount::email()
{
    std::lock_guard<std::mutex> lock(session_mutex());
    load_session_locked();
    return account_session().doc.value("email", "");
}

void ChatGptAccount::sign_out()
{
    std::lock_guard<std::mutex> lock(session_mutex());
    account_session().loaded = true;
    account_session().doc    = nlohmann::json::object();
    keychain_delete();
    boost::system::error_code ec;
    boost::filesystem::remove(account_path(), ec);
}

std::string ChatGptAccount::sign_in()
{
    const std::string host = host_id();
    if (host.empty())
        return "Could not create a host id for ChatGPT sign-in.";

    std::string saved_client;
    std::string id_hint;
    std::string login_hint;
    {
        std::lock_guard<std::mutex> lock(session_mutex());
        load_session_locked();
        saved_client = account_session().doc.value("client_id", "");
        id_hint      = account_session().doc.value("id_token", "");
        login_hint   = account_session().doc.value("email", "");
    }
    const bool        fresh = saved_client.empty();
    const std::string client_id = fresh ? "dynamic_agent_client" : saved_client;

    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)
        return "Could not open the ChatGPT sign-in callback.";
    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;
    if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(listen_fd, 1) != 0) {
        ::close(listen_fd);
        return "Could not listen for the ChatGPT sign-in callback.";
    }
    sockaddr_in bound{};
    socklen_t   bound_len = sizeof(bound);
    ::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bound), &bound_len);
    const int         port = ntohs(bound.sin_port);
    const std::string redirect = "http://127.0.0.1:" + std::to_string(port) + "/auth/callback";
    const std::string state    = random_token(24);
    const std::string nonce    = random_token(24);
    const std::string verifier = random_token(48);
    const std::string challenge = sha256_b64url(verifier);
    if (state.empty() || nonce.empty() || verifier.empty() || challenge.empty()) {
        ::close(listen_fd);
        return "Could not start ChatGPT sign-in.";
    }

    std::ostringstream url;
    url << kAuthAuthorize
        << "?client_id=" << url_encode(client_id)
        << "&response_type=code"
        << "&redirect_uri=" << url_encode(redirect)
        << "&scope=" << url_encode(kScope)
        << "&resource=" << url_encode(kResource)
        << "&state=" << url_encode(state)
        << "&nonce=" << url_encode(nonce)
        << "&code_challenge_method=S256"
        << "&code_challenge=" << url_encode(challenge)
        << "&ext_agent_host_id=" << url_encode(host);
    if (fresh)
        url << "&agent_name_hint=" << url_encode(kAgentName);
    else {
        if (!id_hint.empty())
            url << "&id_token_hint=" << url_encode(id_hint);
        if (!login_hint.empty())
            url << "&login_hint=" << url_encode(login_hint);
    }

    const std::string authorize = url.str();
    if (wxTheApp) {
        post_to_ui([authorize]() {
            if (!wxGetApp().is_closing())
                wxLaunchDefaultBrowser(wxString::FromUTF8(authorize));
        });
    }

    const CallbackResult callback = wait_for_callback(listen_fd);
    ::close(listen_fd);
    if (!callback.error.empty())
        return callback.error;
    if (callback.state != state)
        return "ChatGPT sign-in state did not match.";

    std::string issued = callback.client_id.empty() ? saved_client : callback.client_id;
    if (fresh && (issued.empty() || issued == "dynamic_agent_client"))
        return "ChatGPT did not finish registering VerSlicer.";
    if (!fresh && !callback.client_id.empty() && callback.client_id != saved_client)
        return "ChatGPT returned a different app registration.";

    const std::string form = "grant_type=authorization_code&client_id=" + url_encode(issued)
        + "&code=" + url_encode(callback.code)
        + "&code_verifier=" + url_encode(verifier)
        + "&redirect_uri=" + url_encode(redirect)
        + "&resource=" + url_encode(kResource);
    const HttpResult token_http = http_request(
        "POST", kAuthToken, form, {{"Content-Type", "application/x-www-form-urlencoded"}}, 30, nullptr, nullptr);
    if (token_http.status < 200 || token_http.status >= 300) {
        const std::string message = json_error_message(token_http.body);
        return message.empty() ? "ChatGPT token exchange failed." : message;
    }
    try {
        return apply_token_response(nlohmann::json::parse(token_http.body), issued, nonce, host, true);
    } catch (...) {
        return "ChatGPT token response could not be read.";
    }
}

std::vector<std::string> ChatGptAccount::list_models(std::string* error)
{
    std::string token_error;
    const std::string authorization = [&]() {
        const std::string token = access_token(&token_error);
        return token.empty() ? std::string{} : "Bearer " + token;
    }();
    if (authorization.empty()) {
        if (error)
            *error = token_error.empty() ? "Connect a ChatGPT account first." : token_error;
        return {};
    }
    const HttpResult response = http_request(
        "GET", kModelsUrl, {}, {{"Authorization", authorization}}, 30, nullptr, nullptr);
    if (response.status < 200 || response.status >= 300) {
        if (error) {
            const std::string message = json_error_message(response.body);
            *error = message.empty() ? "Could not list ChatGPT models." : message;
        }
        return {};
    }
    std::vector<std::string> names;
    try {
        const nlohmann::json j = nlohmann::json::parse(response.body);
        const nlohmann::json list = j.contains("models") ? j["models"] : j.value("data", nlohmann::json::array());
        for (const auto& model : list) {
            if (model.contains("visibility") && model.value("visibility", "") != "list")
                continue;
            if (model.contains("slug") && model["slug"].is_string())
                names.push_back(model["slug"].get<std::string>());
            else if (model.contains("id") && model["id"].is_string())
                names.push_back(model["id"].get<std::string>());
        }
    } catch (...) {
        if (error)
            *error = "ChatGPT model list could not be read.";
        return {};
    }
    if (names.empty() && error)
        *error = "This ChatGPT account has no models for VerSlicer.";
    return names;
}

ChatGptCompletion ChatGptAccount::complete(const std::string& model, const std::vector<ChatGptTurn>& messages,
                                           const std::string& tools_json,
                                           const std::shared_ptr<std::atomic<bool>>& cancel,
                                           const std::function<void(const std::string&)>& on_delta,
                                           const std::function<void(Http*)>& bind_http)
{
    if (!ai_privacy_consent_given()) {
        ChatGptCompletion refused;
        refused.error = "AI privacy notice not accepted yet.";
        return refused;
    }
    ChatGptCompletion out;
    std::string       token_error;
    const std::string token = access_token(&token_error);
    if (token.empty()) {
        out.error = token_error.empty() ? "Connect a ChatGPT account first." : token_error;
        return out;
    }

    std::string list_error;
    const std::vector<std::string> catalog = list_models(&list_error);
    if (catalog.empty()) {
        out.error = list_error.empty() ? "Connect a ChatGPT account first." : list_error;
        return out;
    }
    out.model = pick_model(model, catalog);

    std::string    instructions;
    nlohmann::json input = nlohmann::json::array();
    const nlohmann::json functions = responses_tools(tools_json);
    if (!functions.empty()) {
        input.push_back({
            {"type", "additional_tools"},
            {"role", "developer"},
            {"tools",
             nlohmann::json::array({{{"type", "namespace"},
                                     {"name", "functions"},
                                     {"description", "Slicer actions VerSlicer can run."},
                                     {"tools", functions}}})},
        });
    }
    for (const ChatGptTurn& message : messages) {
        if (message.role == "system") {
            if (!instructions.empty())
                instructions += "\n";
            instructions += message.content;
        } else if (!message.content.empty()) {
            const std::string role = message.role == "assistant" ? "assistant" : "user";
            input.push_back({{"role", role}, {"content", message.content}});
        }
    }
    if (input.empty() && instructions.empty()) {
        out.error = "Nothing to send to ChatGPT.";
        return out;
    }

    nlohmann::json body = {{"model", out.model}, {"store", false}, {"stream", true}, {"input", std::move(input)}};
    if (!instructions.empty())
        body["instructions"] = instructions;

    if (cancel && cancel->load()) {
        out.error = "Request cancelled";
        return out;
    }

    const HttpResult response = http_request(
        "POST", kResponsesUrl, body.dump(),
        {{"Authorization", "Bearer " + token}, {"Content-Type", "application/json"}, {"Accept", "text/event-stream"}},
        180, cancel, bind_http);
    if (cancel && cancel->load()) {
        out.error = "Request cancelled";
        return out;
    }
    if (!response.error.empty() && response.status == 0) {
        out.error = response.error;
        return out;
    }
    if (response.status < 200 || response.status >= 300) {
        out.http_status = response.status;
        const std::string message = json_error_message(response.body);
        // Keep the status in the text: the chat panel maps 401/403/429/5xx to advice.
        out.error = "ChatGPT HTTP " + std::to_string(response.status) + (message.empty() ? "" : ": " + message);
        return out;
    }

    const SseParse parsed = parse_responses_sse(response.body, on_delta);
    out.http_status = response.status;
    if (!parsed.error.empty()) {
        out.error = parsed.error;
        return out;
    }
    if (!parsed.completed) {
        out.error = "ChatGPT stream ended before the reply finished.";
        return out;
    }
    out.text = actions_from_calls(parsed.text, parsed.calls);
    if (out.text.empty())
        out.error = "ChatGPT returned an empty reply.";
    return out;
}

}} // namespace

#include "MakerWorldQueryTranslator.hpp"

#include "../OllamaAssistant/OllamaConfig.hpp"
#include "MakerWorldTelemetry.hpp"

#include "slic3r/Utils/Http.hpp"

#include <boost/algorithm/string.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <map>
#include <mutex>

namespace Slic3r { namespace GUI {

namespace {

bool utf8_decode_next(const std::string& s, size_t& i, uint32_t& cp)
{
    if (i >= s.size())
        return false;
    const unsigned char c0 = static_cast<unsigned char>(s[i]);
    if (c0 < 0x80) {
        cp = c0;
        ++i;
        return true;
    }
    if ((c0 & 0xE0) == 0xC0 && i + 1 < s.size()) {
        cp = ((c0 & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
        i += 2;
        return true;
    }
    if ((c0 & 0xF0) == 0xE0 && i + 2 < s.size()) {
        cp = ((c0 & 0x0F) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6)
            | (static_cast<unsigned char>(s[i + 2]) & 0x3F);
        i += 3;
        return true;
    }
    if ((c0 & 0xF8) == 0xF0 && i + 3 < s.size()) {
        cp = ((c0 & 0x07) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12)
            | ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3F);
        i += 4;
        return true;
    }
    cp = c0;
    ++i;
    return true;
}

bool is_cjk_codepoint(uint32_t cp)
{
    return (cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF)
        || (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0x1100 && cp <= 0x11FF);
}

std::string trim_trailing_slash(std::string url)
{
    while (!url.empty() && url.back() == '/')
        url.pop_back();
    return url;
}

std::string extract_chat_content(const nlohmann::json& j)
{
    if (j.contains("message") && j["message"].is_object()) {
        const auto& msg = j["message"];
        if (msg.contains("content") && msg["content"].is_string())
            return msg["content"].get<std::string>();
    }
    if (j.contains("response") && j["response"].is_string())
        return j["response"].get<std::string>();
    return {};
}

std::string sanitize_translation_output(std::string text)
{
    boost::algorithm::trim(text);
    if (text.empty())
        return {};
    const auto line_end = text.find_first_of("\r\n");
    if (line_end != std::string::npos)
        text = text.substr(0, line_end);
    if (!text.empty() && (text.front() == '"' || text.front() == '\''))
        text.erase(text.begin());
    if (!text.empty() && (text.back() == '"' || text.back() == '\''))
        text.pop_back();
    boost::algorithm::trim(text);
    while (text.find("  ") != std::string::npos)
        boost::algorithm::replace_all(text, "  ", " ");
    return text;
}

struct TranslationCacheEntry
{
    std::string                           value;
    std::chrono::steady_clock::time_point at{};
};

constexpr auto kTranslationCacheTtl = std::chrono::hours(6);

std::mutex& translation_cache_mutex()
{
    static std::mutex m;
    return m;
}

std::map<std::string, TranslationCacheEntry>& translation_cache()
{
    static std::map<std::string, TranslationCacheEntry> cache;
    return cache;
}

std::string ollama_translate_sync(const std::string& host, const std::string& model, const std::string& query)
{
    nlohmann::json body;
    body["model"]  = model;
    body["stream"] = false;
    body["options"] = {{"temperature", 0.0}, {"top_p", 0.9}, {"num_predict", 48}};
    body["messages"] = nlohmann::json::array({
        {{"role", "system"},
         {"content",
          "Translate 3D-print model search queries into concise English keywords for MakerWorld. "
          "Output ONLY 2-6 English nouns/adjectives. No punctuation, quotes, or explanation."}},
        {{"role", "user"}, {"content", query}},
    });

    const std::string url  = trim_trailing_slash(host) + "/api/chat";
    std::string       response;
    std::string       http_error;
    unsigned          http_status = 0;

    Http::post(url)
        .header("Content-Type", "application/json")
        .timeout_connect(5)
        .timeout_max(20)
        .set_post_body(body.dump())
        .on_error([&](std::string body, std::string error, unsigned status) {
            response    = std::move(body);
            http_error  = std::move(error);
            http_status = status;
        })
        .on_complete([&](std::string body, unsigned status) {
            response    = std::move(body);
            http_status = status;
        })
        .perform_sync();

    if (!http_error.empty() || http_status < 200 || http_status >= 300 || response.empty())
        return {};

    try {
        const nlohmann::json j = nlohmann::json::parse(response);
        if (j.contains("error"))
            return {};
        return sanitize_translation_output(extract_chat_content(j));
    } catch (...) {
        return {};
    }
}

} // namespace

bool search_text_contains_cjk(const std::string& text)
{
    for (size_t i = 0; i < text.size();) {
        uint32_t cp = 0;
        if (!utf8_decode_next(text, i, cp))
            break;
        if (is_cjk_codepoint(cp))
            return true;
    }
    return false;
}

std::string translate_search_query_to_english(const std::string& query)
{
    const std::string trimmed = boost::algorithm::trim_copy(query);
    if (trimmed.empty() || !search_text_contains_cjk(trimmed))
        return {};

    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(translation_cache_mutex());
        const auto                  it = translation_cache().find(trimmed);
        if (it != translation_cache().end() && now - it->second.at < kTranslationCacheTtl)
            return it->second.value;
    }

    const std::string translated = ollama_translate_sync(ollama_host_from_config(), ollama_model_from_config(), trimmed);
    if (translated.empty())
        MakerWorldTelemetry::translation_failed(trimmed);
    {
        std::lock_guard<std::mutex> lock(translation_cache_mutex());
        translation_cache()[trimmed] = {translated, now};
    }
    return translated;
}

void invalidate_search_translation_cache()
{
    std::lock_guard<std::mutex> lock(translation_cache_mutex());
    translation_cache().clear();
}

}} // namespace

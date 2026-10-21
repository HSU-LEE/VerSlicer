#include "MakerWorldQueryTranslator.hpp"

#include "../Assistant/ChatGptAccount.hpp"
#include "../Assistant/AssistantConfig.hpp"
#include "MakerWorldTelemetry.hpp"

#include <boost/algorithm/string.hpp>

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

std::string assistant_translate_sync(const std::string& host, const std::string& model, const std::string& query)
{
    (void) host;
    const ChatGptCompletion completion = ChatGptAccount::complete(
        model,
        {{"system",
          "Translate 3D-print model search queries into concise English keywords for MakerWorld. "
          "Output ONLY 2-6 English nouns/adjectives. No punctuation, quotes, or explanation."},
         {"user", query}},
        {}, nullptr, nullptr, nullptr);
    if (!completion.error.empty())
        return {};
    return sanitize_translation_output(completion.text);
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

    const std::string translated = assistant_translate_sync(assistant_host_from_config(), assistant_model_from_config(), trimmed);
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

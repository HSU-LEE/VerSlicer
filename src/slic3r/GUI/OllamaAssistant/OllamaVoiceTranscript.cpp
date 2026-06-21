#include "OllamaVoiceTranscript.hpp"

#include "OllamaIntentRules.hpp"
#include "OllamaSettingSearch.hpp"

#include <boost/algorithm/string.hpp>

#include <cctype>

namespace Slic3r { namespace GUI {

namespace {

using namespace OllamaIntentRules;

static bool utf8_advance(const std::string& s, size_t& i, uint32_t& cp)
{
    if (i >= s.size())
        return false;
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
        cp = c;
        ++i;
        return true;
    }
    if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
        cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
        i += 2;
        return true;
    }
    if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
        cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6)
           | (static_cast<unsigned char>(s[i + 2]) & 0x3F);
        i += 3;
        return true;
    }
    if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
        cp = ((c & 0x07) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12)
           | ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6)
           | (static_cast<unsigned char>(s[i + 3]) & 0x3F);
        i += 4;
        return true;
    }
    cp = c;
    ++i;
    return true;
}

static bool is_hangul_codepoint(uint32_t cp)
{
    return (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x3130 && cp <= 0x318F);
}

static size_t count_words(const std::string& text)
{
    size_t words = 0;
    bool   in_word = false;
    for (unsigned char c : text) {
        if (std::isspace(c)) {
            in_word = false;
            continue;
        }
        if (!in_word) {
            ++words;
            in_word = true;
        }
    }
    return words;
}

static bool locale_prefers_korean(const std::string& locale_id)
{
    std::string lower = locale_id;
    boost::algorithm::to_lower(lower);
    return lower.rfind("ko", 0) == 0;
}

static bool has_slicer_intent_hint(const std::string& text)
{
    if (OllamaIntentRules::parse_z_rotation_degrees(text).has_value())
        return true;
    if (!OllamaSettingSearch::candidate_keys_for_request(text, 1, 3).empty())
        return true;
    if (text.find("슬라이스") != std::string::npos || boost::icontains(text, "slice"))
        return true;
    if (text.find("mm") != std::string::npos || text.find('%') != std::string::npos)
        return true;
    return false;
}

} // namespace

bool ollama_voice_contains_hangul(const std::string& text)
{
    for (size_t i = 0; i < text.size();) {
        uint32_t cp = 0;
        if (!utf8_advance(text, i, cp))
            break;
        if (is_hangul_codepoint(cp))
            return true;
    }
    return false;
}

bool ollama_voice_transcript_acceptable(const std::string& text, const std::string& locale_id,
                                        float avg_confidence)
{
    std::string trimmed = text;
    boost::trim(trimmed);
    if (trimmed.size() < 3)
        return false;

    if (avg_confidence >= 0.f && avg_confidence < 0.28f)
        return false;

    if (locale_prefers_korean(locale_id)) {
        if (!ollama_voice_contains_hangul(trimmed)) {
            const size_t words = count_words(trimmed);
            if (words >= 4 || trimmed.size() >= 18)
                return false;
        }
    }

    if (ollama_voice_looks_like_garbled_chat(trimmed))
        return false;

    return true;
}

std::string ollama_voice_transcript_reject_message(const std::string& locale_id, bool ko_ui)
{
    if (ko_ui || locale_prefers_korean(locale_id))
        return "음성을 제대로 인식하지 못했습니다. 마이크를 다시 누르고 또박또박 말씀해 주세요.";
    return "Could not understand the voice input. Tap the mic again and speak clearly.";
}

bool ollama_voice_looks_like_garbled_chat(const std::string& text)
{
    if (text.empty())
        return true;
    if (ollama_voice_contains_hangul(text))
        return false;
    if (has_slicer_intent_hint(text))
        return false;

    const size_t words = count_words(text);
    if (words < 7)
        return false;

    static const char* k_noise[] = {
        "caribbean", "caravan", "angola", "google", "garage", "blanket", "shut up",
        "gotta", "gonna", "nothing", "think i think",
    };
    std::string lower = text;
    boost::algorithm::to_lower(lower);
    int noise_hits = 0;
    for (const char* n : k_noise) {
        if (lower.find(n) != std::string::npos)
            ++noise_hits;
    }
    if (noise_hits >= 2)
        return true;
    if (words >= 10 && noise_hits >= 1)
        return true;

    return false;
}

}} // namespace

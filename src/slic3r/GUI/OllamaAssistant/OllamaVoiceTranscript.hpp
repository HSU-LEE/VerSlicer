#ifndef slic3r_OllamaVoiceTranscript_hpp_
#define slic3r_OllamaVoiceTranscript_hpp_

#include <string>

namespace Slic3r { namespace GUI {

/** True if UTF-8 text contains Hangul syllables or jamo. */
bool ollama_voice_contains_hangul(const std::string& text);

/**
 * Reject obvious speech-to-text failures (e.g. Korean speech recognized as random English).
 * locale_id: BCP-47 style, e.g. "ko-KR", "en-US".
 */
bool ollama_voice_transcript_acceptable(const std::string& text, const std::string& locale_id,
                                        float avg_confidence = -1.f);

/** User-facing hint when transcript is rejected. */
std::string ollama_voice_transcript_reject_message(const std::string& locale_id, bool ko_ui);

/** Last-resort guard before sending chat (voice or typed gibberish). */
bool ollama_voice_looks_like_garbled_chat(const std::string& text);

}} // namespace

#endif

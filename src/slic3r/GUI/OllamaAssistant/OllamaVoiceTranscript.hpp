#ifndef slic3r_OllamaVoiceTranscript_hpp_
#define slic3r_OllamaVoiceTranscript_hpp_

#include <string>

namespace Slic3r { namespace GUI {

/**
 * Reject only what the speech recognizer itself flags: empty text or low
 * confidence. Whether the words make sense is ChatGPT's call, not a keyword list.
 */
bool ollama_voice_transcript_acceptable(const std::string& text, float avg_confidence = -1.f);

/** User-facing hint when a transcript is rejected (translated). */
std::string ollama_voice_transcript_reject_message();

}} // namespace

#endif

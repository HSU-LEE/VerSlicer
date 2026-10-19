#include "OllamaVoiceTranscript.hpp"

#include "../I18N.hpp"

#include <boost/algorithm/string.hpp>

namespace Slic3r { namespace GUI {

bool ollama_voice_transcript_acceptable(const std::string& text, float avg_confidence)
{
    const std::string trimmed = boost::trim_copy(text);
    if (trimmed.empty())
        return false;
    // macOS reports per-segment confidence; very low values are recognizer misses.
    return avg_confidence < 0.f || avg_confidence >= 0.28f;
}

std::string ollama_voice_transcript_reject_message()
{
    return _u8L("Could not understand the voice input. Tap the mic again and speak clearly.");
}

}} // namespace

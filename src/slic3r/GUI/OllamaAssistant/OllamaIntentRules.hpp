#ifndef slic3r_OllamaIntentRules_hpp_
#define slic3r_OllamaIntentRules_hpp_

#include <optional>
#include <string>

namespace Slic3r { namespace GUI {

/** Text parsing helpers for the Ollama assistant (not symptom/intent classifiers). */
namespace OllamaIntentRules {

std::optional<double> parse_z_rotation_degrees(const std::string& s);
std::string           extract_first_url_from_text(const std::string& text);

} // namespace OllamaIntentRules

}} // namespace

#endif

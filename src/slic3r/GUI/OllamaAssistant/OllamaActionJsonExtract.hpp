#ifndef slic3r_OllamaActionJsonExtract_hpp_
#define slic3r_OllamaActionJsonExtract_hpp_

#include <nlohmann/json.hpp>

#include <string>

namespace Slic3r { namespace GUI {

/** Parse the first JSON object from an Ollama assistant reply (no GUI deps). */
nlohmann::json extract_ollama_action_json(const std::string& assistant_text);

}} // namespace

#endif

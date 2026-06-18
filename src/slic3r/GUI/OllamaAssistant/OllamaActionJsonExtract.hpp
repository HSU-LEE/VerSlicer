#ifndef slic3r_OllamaActionJsonExtract_hpp_
#define slic3r_OllamaActionJsonExtract_hpp_

#include <nlohmann/json.hpp>

#include <string>

namespace Slic3r { namespace GUI {

/** Parse the first JSON object from an Ollama assistant reply (no GUI deps). */
nlohmann::json extract_ollama_action_json(const std::string& assistant_text);

/** Best-effort repair of truncated/malformed JSON object text. */
std::string repair_ollama_json_text(std::string text);

/** Extract with sanitize + repair + plain-text salvage before throwing. */
nlohmann::json extract_ollama_action_json_with_repair(const std::string& assistant_text);

/** Build {message, actions} from plain-text setting lines when JSON is missing or broken. */
nlohmann::json try_salvage_ollama_action_json(const std::string& assistant_text);

}} // namespace

#endif

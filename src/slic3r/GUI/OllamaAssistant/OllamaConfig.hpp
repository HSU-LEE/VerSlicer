#ifndef slic3r_OllamaConfig_hpp_
#define slic3r_OllamaConfig_hpp_

#include <string>

namespace Slic3r { namespace GUI {

constexpr const char* kOllamaConfigSection = "ollama";
constexpr const char* kOllamaHostKey       = "host";
constexpr const char* kOllamaModelKey      = "model";
constexpr const char* kOllamaDefaultHost   = "http://127.0.0.1:11434";
constexpr const char* kOllamaDefaultModel  = "llama3.2:latest";

std::string ollama_host_from_config();
std::string ollama_model_from_config();
std::string normalize_ollama_model_tag(std::string model);

}} // namespace

#endif

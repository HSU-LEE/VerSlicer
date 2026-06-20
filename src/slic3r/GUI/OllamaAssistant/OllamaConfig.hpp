#ifndef slic3r_OllamaConfig_hpp_
#define slic3r_OllamaConfig_hpp_

#include <string>

namespace Slic3r { namespace GUI {

constexpr const char* kOllamaConfigSection = "ollama";
constexpr const char* kOllamaHostKey       = "host";
constexpr const char* kOllamaModelKey      = "model";
constexpr const char* kOllamaDefaultHost   = "http://127.0.0.1:11434";
constexpr const char* kOllamaDefaultModel  = "qwen2.5:3b";
/** Inference tuning — smaller ctx/predict = faster replies on Apple Silicon. */
constexpr int         kOllamaNumCtx        = 8192;
constexpr int         kOllamaNumPredict    = 768;
constexpr const char* kOllamaKeepAlive     = "30m";

constexpr const char* kOllamaAutoCatalogKey    = "auto_catalog";
constexpr const char* kOllamaTwoHopKey         = "two_hop";
constexpr const char* kOllamaKeywordInjectKey  = "keyword_inject";
constexpr const char* kOllamaRuleOnlyKey       = "rule_only_fallback";
constexpr const char* kOllamaAdaptiveRouteKey  = "adaptive_routing";
constexpr const char* kOllamaWikiSearchKey     = "wiki_search";
constexpr const char* kOllamaCriticKey         = "critic";

std::string ollama_host_from_config();
std::string ollama_model_from_config();
std::string normalize_ollama_model_tag(std::string model);

/** Feature flags for Ollama assistant pipeline (app config + env override). */
bool ollama_auto_catalog_enabled();
bool ollama_two_hop_enabled();
bool ollama_keyword_inject_enabled();
bool ollama_rule_only_fallback_enabled();
bool ollama_adaptive_routing_enabled();
bool ollama_wiki_search_enabled();
bool ollama_critic_enabled();

}} // namespace

#endif

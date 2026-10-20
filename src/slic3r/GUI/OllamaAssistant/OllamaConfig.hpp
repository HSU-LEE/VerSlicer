#ifndef slic3r_OllamaConfig_hpp_
#define slic3r_OllamaConfig_hpp_

#include <string>

namespace Slic3r { namespace GUI {

// The "ollama" section is a legacy name; keep it so existing verslicer.conf settings continue to load.
constexpr const char* kOllamaConfigSection = "ollama";
constexpr const char* kOllamaHostKey       = "host";
constexpr const char* kOllamaModelKey      = "model";
constexpr const char* kOllamaDefaultModel  = "";
/** Inference tuning — smaller ctx/predict = faster replies on Apple Silicon. */
constexpr int         kOllamaNumCtx        = 8192;
constexpr int         kOllamaNumPredict    = 768;
constexpr const char* kOllamaKeepAlive     = "30m";

constexpr const char* kOllamaAutoCatalogKey    = "auto_catalog";
constexpr const char* kOllamaWikiSearchKey     = "wiki_search";
constexpr const char* kOllamaMcpServerKey      = "mcp_server";
constexpr const char* kOllamaAssistLoopKey     = "assist_loop";
constexpr const char* kOllamaAssistMaxStepsKey = "assist_max_steps";

std::string ollama_host_from_config();
std::string ollama_model_from_config();
std::string normalize_ollama_model_tag(std::string model);

/** Feature flags for Ollama assistant pipeline (app config + env override). */
bool ollama_auto_catalog_enabled();
bool ollama_wiki_search_enabled();
/** Local MCP server for external agents (Cursor etc.). Off unless opted in. */
bool ollama_mcp_server_enabled();
bool ollama_assist_loop_enabled();
int  ollama_assist_max_steps();

}} // namespace

#endif

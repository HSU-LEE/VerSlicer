#ifndef slic3r_AssistantConfig_hpp_
#define slic3r_AssistantConfig_hpp_

#include <string>

namespace Slic3r { namespace GUI {

// The "ollama" section is a legacy name; keep it so existing verslicer.conf settings continue to load.
constexpr const char* kAssistantConfigSection = "ollama";
constexpr const char* kAssistantHostKey       = "host";
constexpr const char* kAssistantModelKey      = "model";
constexpr const char* kAssistantDefaultModel  = "";
/** Inference tuning — smaller ctx/predict = faster replies on Apple Silicon. */
constexpr int         kAssistantNumCtx        = 8192;
constexpr int         kAssistantNumPredict    = 768;
constexpr const char* kAssistantKeepAlive     = "30m";

constexpr const char* kAssistantAutoCatalogKey    = "auto_catalog";
constexpr const char* kAssistantWikiSearchKey     = "wiki_search";
constexpr const char* kAssistantMcpServerKey      = "mcp_server";
constexpr const char* kAssistLoopKey     = "assist_loop";
constexpr const char* kAssistMaxStepsKey = "assist_max_steps";

std::string assistant_host_from_config();
std::string assistant_model_from_config();
std::string normalize_assistant_model_tag(std::string model);

/** Feature flags for Assistant assistant pipeline (app config + env override). */
bool assistant_auto_catalog_enabled();
bool assistant_wiki_search_enabled();
/** Local MCP server for external agents (Cursor etc.). Off unless opted in. */
bool assistant_mcp_server_enabled();
bool assistant_assist_loop_enabled();
int  assistant_assist_max_steps();

}} // namespace

#endif

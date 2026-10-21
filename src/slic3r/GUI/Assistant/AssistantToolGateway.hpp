#ifndef slic3r_AssistantToolGateway_hpp_
#define slic3r_AssistantToolGateway_hpp_

#include <nlohmann/json.hpp>

#include <string>

namespace Slic3r { namespace GUI {

struct AssistantNormalizedTool
{
    bool        ok{false};
    nlohmann::json action;
    std::string error;
};

/** One schema and one execution path for the in-app agent and the local MCP server. */
class AssistantToolGateway
{
public:
    /** Assistant /api/chat `tools` array. */
    static nlohmann::json assistant_tools();
    /** MCP tools/list `tools` array. */
    static nlohmann::json mcp_tools();

    static AssistantNormalizedTool normalize(const std::string& name, const nlohmann::json& arguments);

    /**
     * Run one tool on the calling thread. The caller must be the wx UI thread.
     * Returns the MCP-style result object {content, isError}.
     */
    static nlohmann::json call(const std::string& name, const nlohmann::json& arguments);
};

}} // namespace

#endif

#ifndef slic3r_AssistantMcpServer_hpp_
#define slic3r_AssistantMcpServer_hpp_

namespace Slic3r { namespace GUI {

/** Localhost MCP server. Tool calls use AssistantToolGateway on the wx UI thread. */
class AssistantMcpServer
{
public:
    static void start();
    static void stop();
};

}} // namespace

#endif

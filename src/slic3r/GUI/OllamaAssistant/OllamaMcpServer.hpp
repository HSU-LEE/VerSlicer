#ifndef slic3r_OllamaMcpServer_hpp_
#define slic3r_OllamaMcpServer_hpp_

namespace Slic3r { namespace GUI {

/** Localhost MCP server. Tool calls use OllamaToolGateway on the wx UI thread. */
class OllamaMcpServer
{
public:
    static void start();
    static void stop();
};

}} // namespace

#endif

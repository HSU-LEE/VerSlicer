#ifndef slic3r_OllamaToolRegistry_hpp_
#define slic3r_OllamaToolRegistry_hpp_

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

enum class OllamaToolCategory
{
    Readonly,
    Mutating,
    Dangerous,
};

struct OllamaToolSpec
{
    const char*       id;
    OllamaToolCategory category;
    const char*       description;
};

class OllamaToolRegistry
{
public:
    static const std::vector<OllamaToolSpec>& all();
    static OllamaToolCategory                 category_for(const std::string& tool_id);
    static bool                               is_agent_tool(const std::string& tool_id);
    /** Read-only tools plus select_object: steps that observe without changing the plate. */
    static bool                               is_observation(const std::string& tool_id);
    static std::string                        agent_tools_schema_block(bool korean);
};

}} // namespace

#endif

#ifndef slic3r_AssistantActionRegistry_hpp_
#define slic3r_AssistantActionRegistry_hpp_

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

struct AssistantActionTypeSpec
{
    const char* type;
    bool        allowed_in_apply;
    bool        allowed_in_advisor;
    bool        allowed_in_coach;
    const char* description;
};

class AssistantActionRegistry
{
public:
    static const std::vector<AssistantActionTypeSpec>& all();
    static bool is_allowed_type(const std::string& type);
    static bool is_allowed_in_advisor(const std::string& type);
    static bool is_blocked_type(const std::string& type);
    static std::string action_types_prompt_block(bool ko_ui, bool apply_mode);
};

}} // namespace

#endif

#ifndef slic3r_AssistantModeJudge_hpp_
#define slic3r_AssistantModeJudge_hpp_

#include <optional>
#include <string>

namespace Slic3r { namespace GUI {

/** Picks Question or Apply mode from what the user wrote, so the mode the
 *  user left selected does not block the obvious intent: "벽 4개로 해줘" in
 *  Question mode switches to Apply; "리트랙션이 뭐야?" in Apply mode switches
 *  to Question. Checks that need tools ("잘 뽑힐까?") stay in Apply. */
class AssistantModeJudge
{
public:
    /** The mode to switch to (true = Apply), or nullopt to keep the current one. */
    static std::optional<bool> judge(const std::string& user_text, bool apply_mode);
};

}} // namespace Slic3r::GUI

#endif

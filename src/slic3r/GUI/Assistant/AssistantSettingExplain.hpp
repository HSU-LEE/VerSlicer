#ifndef slic3r_AssistantSettingExplain_hpp_
#define slic3r_AssistantSettingExplain_hpp_

#include <string>

class wxWindow;
class wxString;

namespace Slic3r { namespace GUI {

/** Right-click on a setting name → "Explain with AI": asks the assistant what
 *  the setting does, what raising or lowering it changes, and whether the
 *  current value suits this print, in plain words. */
class AssistantSettingExplain
{
public:
    /** Show the context menu at the mouse for this setting. GUI thread. */
    static void popup(wxWindow* parent, const std::string& opt_key, const wxString& label);

    /** The question sent to the assistant (in the UI language). */
    static wxString prompt(const std::string& opt_key, const wxString& label);
};

}} // namespace Slic3r::GUI

#endif

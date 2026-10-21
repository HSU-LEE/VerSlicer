#include "AssistantSettingExplain.hpp"

#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../Plater.hpp"

#include <wx/menu.h>

namespace Slic3r { namespace GUI {

wxString AssistantSettingExplain::prompt(const std::string& opt_key, const wxString& label)
{
    wxString name = label;
    name.Trim().Trim(false);
    if (name.EndsWith(":"))
        name.RemoveLast();
    return wxString::Format(_L("Explain the setting \"%s\" (%s) in plain words: what it does, what happens if I raise "
                               "or lower it, and whether my current value suits the model on the plate."),
                            name, wxString::FromUTF8(opt_key));
}

void AssistantSettingExplain::popup(wxWindow* parent, const std::string& opt_key, const wxString& label)
{
    if (!parent || opt_key.empty())
        return;
    wxMenu menu;
    const int id = wxWindow::NewControlId();
    menu.Append(id, _L("Explain with AI"));
    menu.Bind(
        wxEVT_MENU,
        [opt_key, label](wxCommandEvent&) {
            if (Plater* plater = wxGetApp().plater())
                plater->ai_assistant_submit(prompt(opt_key, label));
        },
        id);
    parent->PopupMenu(&menu);
}

}} // namespace Slic3r::GUI

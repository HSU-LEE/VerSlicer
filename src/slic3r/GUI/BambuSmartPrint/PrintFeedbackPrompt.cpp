#include "PrintFeedbackPrompt.hpp"

#include "libslic3r/BambuSmartPrint/PrintFeedback.hpp"
#include "libslic3r/BambuSmartPrint/PrinterLearningStore.hpp"

#include "../GUI_App.hpp"
#include "../I18N.hpp"
#include "../NotificationManager.hpp"
#include "../Plater.hpp"

#include <wx/choicdlg.h>
#include <wx/msgdlg.h>

#include <cmath>
#include <map>

namespace Slic3r { namespace GUI {

namespace {

// Learned adjustment per key, so the answer can say what moved.
std::map<std::string, float> adjustments(const std::string& printer_id)
{
    return BambuSmartPrint::PrinterLearningStore::instance().get_profile(printer_id).setting_adjustments;
}

wxString key_label(const std::string& key)
{
    if (key == "initial_layer_speed") return _L("slower first layer");
    if (key == "bed_temperature")     return _L("warmer bed");
    if (key == "brim_width")          return _L("wider brim");
    if (key == "retraction_length")   return _L("more retraction");
    if (key == "outer_wall_speed")    return _L("slower outer walls");
    return wxString::FromUTF8(key);
}

} // namespace

void PrintFeedbackPrompt::offer(const std::string& printer_id, const std::string& job_name,
                                const DynamicPrintConfig& config)
{
    Plater* plater = wxGetApp().plater();
    if (!plater || !plater->get_notification_manager())
        return;
    plater->get_notification_manager()->push_notification(
        NotificationType::CustomNotification, NotificationManager::NotificationLevel::RegularNotificationLevel,
        _u8L("Print finished. Tell VerSlicer how it came out so the next one is better."),
        _u8L("How did it come out?"),
        [printer_id, job_name, config](wxEvtHandler*) {
            wxGetApp().CallAfter([printer_id, job_name, config]() {
                ask(wxGetApp().GetTopWindow(), printer_id, job_name, config);
            });
            return true;
        });
}

void PrintFeedbackPrompt::ask(wxWindow* parent, const std::string& printer_id, const std::string& job_name,
                              const DynamicPrintConfig& config)
{
    const auto& issues = BambuSmartPrint::PrintFeedback::issues();
    wxArrayString choices;
    choices.Add(_L("It came out well"));
    for (const auto& i : issues)
        choices.Add(_(i.label));

    const wxString question = job_name.empty()
        ? _L("How did the print come out?")
        : wxString::Format(_L("How did \"%s\" come out?"), wxString::FromUTF8(job_name));
    wxSingleChoiceDialog dlg(parent, question, _L("Print result"), choices);
    dlg.SetSelection(0);
    if (dlg.ShowModal() != wxID_OK)
        return;

    const int sel = dlg.GetSelection();
    const std::string id = sel <= 0 ? std::string("good") : std::string(issues[sel - 1].id);
    const auto before = adjustments(printer_id);
    if (!BambuSmartPrint::PrinterLearningStore::instance().record_print_feedback(printer_id, id, config))
        return;
    const auto after = adjustments(printer_id);

    wxString changed;
    for (const auto& [key, value] : after) {
        const auto it = before.find(key);
        if (it == before.end() || std::abs(it->second - value) > 0.01f)
            changed << (changed.empty() ? "" : ", ") << key_label(key);
    }

    wxString text;
    if (id == "good")
        text = _L("Thanks. Recorded as a good print for this printer.");
    else if (!changed.empty())
        text = wxString::Format(_L("Recorded. Next time Smart Print will suggest a %s for this printer, and the "
                                   "review will say it was learned from this print."),
                                changed);
    else
        text = _L("Recorded. This problem does not change settings by itself; it counts toward this printer's "
                  "history, which the assistant reads.");
    if (Plater* plater = wxGetApp().plater())
        if (NotificationManager* nm = plater->get_notification_manager())
            nm->push_notification(text.utf8_string());
}

}} // namespace Slic3r::GUI

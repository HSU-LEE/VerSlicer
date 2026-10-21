#include "AssistantLearningSummary.hpp"
#include "AiCatalog.hpp"

#include "libslic3r/BambuSmartPrint/PrintFeedback.hpp"

#ifndef ASSISTANT_HEADLESS_TEST
#include "libslic3r/BambuSmartPrint/PrinterLearningStore.hpp"

#include "../DeviceManager.hpp"
#include "../DeviceCore/DevManager.h"
#include "../GUI_App.hpp"
#include "../I18N.hpp"

#include <wx/msgdlg.h>
#endif

#include <boost/format.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Slic3r { namespace GUI {

namespace {

std::string category_words(const std::string& key)
{
    if (key == "adhesion")
        return ai_catalog_text("the first layer coming loose from the bed");
    if (key == "filament")
        return ai_catalog_text("filament problems (tangles, jams, run-outs)");
    if (key == "temperature")
        return ai_catalog_text("nozzle or bed temperature faults");
    if (key == "mechanical")
        return ai_catalog_text("mechanical problems (collisions, motion errors)");
    if (key == "gcode")
        return ai_catalog_text("G-code errors");
    if (key == "network")
        return ai_catalog_text("lost connection");
    if (key == "user_cancelled")
        return ai_catalog_text("prints you stopped yourself");
    return ai_catalog_text("other causes");
}

struct KeyWords { const char* key; const char* what; const char* unit; };
const KeyWords kAdjusted[] = {
    {"bed_temperature", "Bed temperature", "°C"},
    {"initial_layer_speed", "First layer speed", "mm/s"},
    {"brim_width", "Brim width", "mm"},
    {"retraction_length", "Retraction length", "mm"},
    {"outer_wall_speed", "Outer wall speed", "mm/s"},
};

std::string signed_value(float v, const char* unit)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), (std::fabs(v - std::round(v)) < 0.05f) ? "%+.0f %s" : "%+.1f %s", v, unit);
    return buf;
}

} // namespace

nlohmann::json AssistantLearningSummary::describe(const BambuSmartPrint::PrinterLearningProfile& p)
{
    nlohmann::json out;
    out["prints"]    = p.total_prints;
    out["succeeded"] = p.successful_prints;
    out["success_rate"] = p.total_prints > 0 ? std::lround(100.0 * p.successful_prints / p.total_prints) : 0;
    out["empty"]     = p.total_prints == 0 && p.setting_adjustments.empty();

    nlohmann::json facts = nlohmann::json::array();
    if (p.total_prints > 0)
        facts.push_back((boost::format(ai_catalog_text("%1% prints recorded, %2% succeeded (%3%%%).")) % p.total_prints %
                         p.successful_prints % out["success_rate"].get<long>()).str());

    // Most common failure cause first.
    std::vector<std::pair<std::string, int>> causes(p.failures_by_category.begin(), p.failures_by_category.end());
    std::sort(causes.begin(), causes.end(), [](const auto& l, const auto& r) { return l.second > r.second; });
    if (!causes.empty() && causes.front().second > 0)
        facts.push_back((boost::format(ai_catalog_text("Most failures were caused by %1% (%2% times).")) %
                         category_words(causes.front().first) % causes.front().second).str());
    if (!p.pending_learning.empty())
        facts.push_back((boost::format(ai_catalog_text("%1% lesson(s) are waiting for your approval in Smart Print.")) %
                         p.pending_learning.size()).str());
    int reported = 0;
    for (const auto& [id, n] : p.user_feedback)
        if (id != "good")
            reported += n;
    if (reported > 0)
        facts.push_back((boost::format(ai_catalog_text("You reported %1% problem(s) after prints finished.")) % reported).str());
    if (p.helpful_learning_count > 0)
        facts.push_back((boost::format(ai_catalog_text("You marked %1% suggestion(s) as helpful.")) % p.helpful_learning_count).str());
    out["facts"] = facts;

    nlohmann::json adjustments = nlohmann::json::array();
    for (const auto& k : kAdjusted) {
        const auto it = p.setting_adjustments.find(k.key);
        if (it == p.setting_adjustments.end() || std::fabs(it->second) < 0.05f)
            continue;
        adjustments.push_back({{"key", k.key}, {"what", ai_catalog_text(k.what)},
                               {"change", k.key == std::string("brim_width") ? (boost::format("%.0f mm") % it->second).str()
                                                                             : signed_value(it->second, k.unit)},
                               {"because", BambuSmartPrint::PrintFeedback::reason_for(p, k.key)}});
    }
    out["adjustments"] = adjustments;
    return out;
}

#ifndef ASSISTANT_HEADLESS_TEST

std::string AssistantLearningSummary::current_printer_id()
{
    if (DeviceManager* dm = wxGetApp().getDeviceManager())
        if (MachineObject* sel = dm->get_selected_machine())
            return sel->get_dev_id();
    return "local";
}

void AssistantLearningSummary::show_dialog(wxWindow* parent)
{
    const auto profile = BambuSmartPrint::PrinterLearningStore::instance().get_profile(current_printer_id());
    const nlohmann::json s = describe(profile);
    wxString text;
    if (s["empty"].get<bool>()) {
        text = _L("Nothing learned yet for this printer.\n\nAfter each print, Smart Print records whether it worked. "
                  "When a print fails, it learns small setting changes (for example a slower first layer) and uses "
                  "them in the next suggestions for this printer.");
    } else {
        for (const auto& f : s["facts"])
            text << "• " << wxString::FromUTF8(f.get<std::string>()) << "\n";
        if (!s["adjustments"].empty()) {
            text << "\n" << _L("Adjusted for this printer:") << "\n";
            for (const auto& a : s["adjustments"])
                text << "• " << wxString::FromUTF8(a["what"].get<std::string>()) << ": "
                     << wxString::FromUTF8(a["change"].get<std::string>())
                     << (a["because"].get<std::string>().empty()
                             ? wxString()
                             : " (" + wxString::Format(_L("because %s"), wxString::FromUTF8(a["because"].get<std::string>())) + ")")
                     << "\n";
        }
        text << "\n" << _L("These are applied to Smart Print suggestions for this printer. Clear them in Smart Print → Privacy and data.");
    }
    wxMessageDialog dlg(parent, text, _L("What VerSlicer learned"), wxOK | wxICON_INFORMATION);
    dlg.ShowModal();
}

AssistantActionResult AssistantLearningSummary::apply(const nlohmann::json& /*action*/)
{
    const auto profile = BambuSmartPrint::PrinterLearningStore::instance().get_profile(current_printer_id());
    AssistantActionResult r;
    r.success = true;
    r.message = describe(profile).dump();
    return r;
}

AssistantActionResult AssistantLearningSummary::report_result(const nlohmann::json& action)
{
    AssistantActionResult r;
    const std::string result = action.value("result", "");
    const std::string printer = current_printer_id();
    auto& store = BambuSmartPrint::PrinterLearningStore::instance();
    if (!store.record_print_feedback(printer, result, DynamicPrintConfig())) {
        r.message = "Unknown result. Use good, first_layer_lifted, warping, stringing, layer_shift, rough_walls or other.";
        return r;
    }
    r.success = true;
    r.message = nlohmann::json{{"recorded", result}, {"learning_now", describe(store.get_profile(printer))}}.dump();
    return r;
}

#endif

}} // namespace Slic3r::GUI

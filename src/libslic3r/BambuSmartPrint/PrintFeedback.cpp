#include "PrintFeedback.hpp"

#include "libslic3r/I18N.hpp"

#include <boost/format.hpp>

namespace Slic3r {
namespace BambuSmartPrint {

const std::vector<PrintFeedbackIssue>& PrintFeedback::issues()
{
    static const std::vector<PrintFeedbackIssue> list = {
        {"first_layer_lifted", L("The first layer came loose"), FailureCategory::Adhesion},
        {"warping", L("Corners lifted or warped"), FailureCategory::Adhesion},
        {"stringing", L("Strings or blobs between parts"), FailureCategory::Filament},
        {"layer_shift", L("Layers shifted sideways"), FailureCategory::Mechanical},
        {"rough_walls", L("Rough or wavy walls"), FailureCategory::Mechanical},
        {"other", L("Something else"), FailureCategory::Unknown},
    };
    return list;
}

const PrintFeedbackIssue* PrintFeedback::find(const std::string& id)
{
    for (const PrintFeedbackIssue& i : issues())
        if (id == i.id)
            return &i;
    return nullptr;
}

std::string PrintFeedback::reason_for(const PrinterLearningProfile& p, const std::string& setting_key)
{
    std::string category;
    if (setting_key == "initial_layer_speed" || setting_key == "bed_temperature" || setting_key == "brim_width")
        category = "adhesion";
    else if (setting_key == "retraction_length")
        category = "filament";
    else if (setting_key == "outer_wall_speed")
        category = "mechanical";
    else
        return {};

    const auto it = p.failures_by_category.find(category);
    int count = it == p.failures_by_category.end() ? 0 : it->second;
    // bed_temperature is also nudged by temperature faults
    if (setting_key == "bed_temperature") {
        const auto t = p.failures_by_category.find("temperature");
        if (count == 0 && t != p.failures_by_category.end() && t->second > 0)
            return (boost::format(_u8L("the printer reported temperature faults %1% time(s)")) % t->second).str();
    }
    if (count <= 0)
        return {};
    if (category == "adhesion")
        return (boost::format(_u8L("the first layer came loose %1% time(s)")) % count).str();
    if (category == "filament")
        return (boost::format(_u8L("there was stringing or a filament problem %1% time(s)")) % count).str();
    return (boost::format(_u8L("layers shifted or walls came out rough %1% time(s)")) % count).str();
}

} // namespace BambuSmartPrint
} // namespace Slic3r

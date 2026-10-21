#include "AssistantWeakSpots.hpp"

#ifndef ASSISTANT_HEADLESS_TEST
#include "libslic3r/BambuSmartPrint/MeshGeometryAnalyzer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "../GLCanvas3D.hpp"
#include "../GUI_App.hpp"
#include "../PartPlate.hpp"
#include "../Plater.hpp"
#include "../Selection.hpp"
#endif

#include <algorithm>
#include <cmath>

namespace Slic3r { namespace GUI {

namespace {

int rank(const std::string& severity) { return severity == "high" ? 0 : severity == "medium" ? 1 : 2; }

nlohmann::json spot(const char* id, const char* severity, std::string what, std::string fix, nlohmann::json fix_action)
{
    return {{"id", id}, {"severity", severity}, {"what", std::move(what)}, {"fix", std::move(fix)},
            {"fix_tool", std::move(fix_action)}};
}

} // namespace

nlohmann::json AssistantWeakSpots::describe(const BambuSmartPrint::ModelAnalysis& a)
{
    nlohmann::json out = nlohmann::json::array();
    const double overhang = std::max(a.overhang_face_ratio, a.overhang_ratio);
    const int    pct      = static_cast<int>(std::lround(overhang * 100.0));

    if (!a.fits_bed)
        out.push_back(spot("too_big", "high", "The model is larger than the build plate and cannot print as it is.",
                           "Scale it down to fit (about " + std::to_string(static_cast<int>(std::lround(a.bed_scale_factor * 100.0))) + "% of its size).",
                           {{"tool", "transform"}, {"args", {{"op", "scale"}, {"factor", a.bed_scale_factor}}}}));
    if (overhang > 0.2)
        out.push_back(spot("steep_overhangs", overhang > 0.35 ? "high" : "medium",
                           "About " + std::to_string(pct) + "% of the surface hangs in the air (shown red on the model); "
                           "those parts will sag or fail without support.",
                           "Turn on tree supports, or rotate the model so it rests on a flatter side.",
                           {{"tool", "set_config"}, {"args", {{"preset", "print"},
                            {"options", {{"enable_support", true}, {"support_type", "tree(auto)"}}}}}}));
    if (a.first_layer_contact_ratio > 0.0 && a.first_layer_contact_ratio < 0.4)
        out.push_back(spot("small_base", "high",
                           "Only a small part of the model touches the bed, so it can come loose during the print.",
                           "Add a brim around the base.",
                           {{"tool", "set_config"}, {"args", {{"preset", "print"}, {"options", {{"enable_brim", true}}}}}}));
    if (a.tall_narrow || (a.stability_known && a.tip_over_risk > 0.4))
        out.push_back(spot("may_tip_over", "medium",
                           "The model is tall and narrow and may wobble or tip over as it gets higher.",
                           "Add a brim, and lay it down if its shape allows.",
                           {{"tool", "set_config"}, {"args", {{"preset", "print"}, {"options", {{"enable_brim", true}}}}}}));
    if (a.thin_feature_risk)
        out.push_back(spot("thin_parts", "medium",
                           "Some parts are thin, so they can snap off or print weak.",
                           "Use more walls (the functional profile) and print it a little slower.",
                           {{"tool", "set_purpose"}, {"args", {{"purpose", "functional"}}}}));
    if (a.is_small_part)
        out.push_back(spot("tiny_details", "low",
                           "The model is small; fine details can blur at the usual layer height.",
                           "Use finer layers (the decorative profile).",
                           {{"tool", "set_purpose"}, {"args", {{"purpose", "decorative"}}}}));

    std::stable_sort(out.begin(), out.end(), [](const nlohmann::json& l, const nlohmann::json& r) {
        return rank(l["severity"].get<std::string>()) < rank(r["severity"].get<std::string>());
    });
    return out;
}

#ifndef ASSISTANT_HEADLESS_TEST

AssistantActionResult AssistantWeakSpots::apply(const nlohmann::json& /*action*/)
{
    AssistantActionResult result;
    Plater* plater = wxGetApp().plater();
    PresetBundle* bundle = wxGetApp().preset_bundle;
    if (!plater || !bundle) {
        result.message = "Plater not available";
        return result;
    }
    PartPlate* plate = plater->get_partplate_list().get_curr_plate();
    Model&     model = plater->model();
    if (!plate || model.objects.empty()) {
        result.message = "No model on the plate. Ask the user to add one first.";
        return result;
    }
    const DynamicPrintConfig config = bundle->full_config();

    nlohmann::json objects = nlohmann::json::array();
    int worst_idx = -1, worst_rank = 3;
    for (size_t i = 0; i < model.objects.size(); ++i) {
        ModelObject* obj = model.objects[i];
        if (!obj || !plate->contain_instance(static_cast<int>(i), 0))
            continue;
        const auto analysis = BambuSmartPrint::MeshGeometryAnalyzer::analyze_objects({obj}, config);
        nlohmann::json spots = describe(analysis);
        if (!spots.empty()) {
            const int r = rank(spots.front()["severity"].get<std::string>());
            if (r < worst_rank) {
                worst_rank = r;
                worst_idx  = static_cast<int>(i);
            }
        }
        objects.push_back({{"object_id", static_cast<int>(i)}, {"name", obj->name},
                           {"height_mm", analysis.height_mm}, {"weak_spots", std::move(spots)}});
    }

    // Show the overhangs on the model itself, and point at the worst one.
    plater->select_view_3D("3D");
    plater->show_view3D_overhang(true);
    if (worst_idx >= 0) {
        Selection& sel = plater->get_view3D_canvas3D()->get_selection();
        sel.clear();
        sel.add_object(static_cast<unsigned int>(worst_idx), true);
    }
    if (GLCanvas3D* canvas = plater->get_view3D_canvas3D())
        canvas->set_as_dirty();

    result.success          = true;
    result.effective_change = false;
    result.message = nlohmann::json{
        {"weak_spots_by_object", objects},
        {"shown_on_3d_view", "Overhangs are highlighted in red; the model with the worst issue is selected."},
        {"how_to_answer", "Explain each weak spot in plain words in the user's language, worst first, and offer the "
                          "fix. Apply a fix only if the user agrees, by calling fix_tool."}}.dump();
    return result;
}

#endif

}} // namespace Slic3r::GUI

#include "BeginnerJourney.hpp"

#include "../GLCanvas3D.hpp"
#include "../GUI_App.hpp"
#include "../ImGuiWrapper.hpp"
#include "../I18N.hpp"
#include "../Plater.hpp"

#include "AICoachController.hpp"
#include "AIGuiOrchestrator.hpp"
#include "../BambuSmartPrint/BambuSmartPrintService.hpp"

#include <imgui/imgui.h>

namespace Slic3r { namespace GUI {

namespace {

constexpr const char* kJourneySection = "beginner_journey";
constexpr const char* kPrinterDone    = "printer_done";
constexpr const char* kModelDone    = "model_done";
constexpr const char* kSliceDone    = "slice_done";
constexpr const char* kSendDone     = "send_done";

void set_flag(const char* key, bool v)
{
    if (wxGetApp().app_config)
        wxGetApp().app_config->set(kJourneySection, key, v ? "1" : "0");
}

bool get_flag(const char* key)
{
    if (!wxGetApp().app_config)
        return false;
    return wxGetApp().app_config->get(kJourneySection, key) == "1";
}

bool show_journey_ui()
{
    // The beginner onboarding checklist is hidden in Simple layout mode per user
    // request. Since this overlay only ever surfaced in Simple mode, it no longer
    // renders anywhere (enabling it in other modes would introduce an overlay that
    // was never shown there before).
    return false;
}

} // namespace

void BeginnerJourney::save_flag(const char* key, bool done) { set_flag(key, done); }
bool BeginnerJourney::read_flag(const char* key) { return get_flag(key); }

int BeginnerJourney::completed_step_count()
{
    int n = 0;
    if (get_flag(kPrinterDone)) ++n;
    if (get_flag(kModelDone)) ++n;
    if (get_flag(kSliceDone)) ++n;
    if (get_flag(kSendDone)) ++n;
    return n;
}

void BeginnerJourney::on_printer_configured() { set_flag(kPrinterDone, true); }
void BeginnerJourney::on_model_on_bed()     { set_flag(kModelDone, true); }
void BeginnerJourney::on_sliced()           { set_flag(kSliceDone, true); }
void BeginnerJourney::on_sent_or_exported() { set_flag(kSendDone, true); }

void BeginnerJourney::render(GLCanvas3D& canvas)
{
    if (!show_journey_ui())
        return;

    ImGuiWrapper* imgui_ptr = wxGetApp().imgui();
    if (!imgui_ptr)
        return;
    ImGuiWrapper& imgui = *imgui_ptr;
    const Size    sz    = canvas.get_canvas_size();
    const float   scale = canvas.get_scale();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 8.f));
    imgui.set_next_window_pos(12.f * scale, static_cast<float>(sz.get_height()) - 180.f * scale, ImGuiCond_Always, 0.f, 1.f);

    const int journey_flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_AlwaysAutoResize;
    if (imgui.begin(std::string("BeginnerJourney"), journey_flags)) {
        imgui.text(_u8L("Getting started").c_str());
        ImGui::Separator();
        auto step = [&](const char* label, bool done) {
            imgui.text(done ? (std::string("[x] ") + label).c_str() : (std::string("[ ] ") + label).c_str());
        };
        step(_u8L("Choose printer").c_str(), get_flag(kPrinterDone));
        step(_u8L("Load a model").c_str(), get_flag(kModelDone));
        step(_u8L("Slice").c_str(), get_flag(kSliceDone));
        step(_u8L("Print or export").c_str(), get_flag(kSendDone));
        ImGui::Text("%d/4", completed_step_count());

        const auto& readiness = BambuSmartPrintService::instance().last_readiness_report();
        if (readiness.score > 0.f) {
            ImGui::Separator();
            ImGui::Text("%s: %.0f%%", _u8L("Print readiness").c_str(), readiness.score);
        }
    }
    imgui.end();
    ImGui::PopStyleVar(2);
}

bool BeginnerJourney::update(int64_t delta_ms)
{
    (void) delta_ms;
    if (!wxGetApp().plater())
        return false;

    if (!get_flag(kPrinterDone) && wxGetApp().preset_bundle) {
        const auto& printers = wxGetApp().preset_bundle->printers;
        if (!printers.get_selected_preset_name().empty())
            set_flag(kPrinterDone, true);
    }
    if (!get_flag(kModelDone) && !wxGetApp().plater()->model().objects.empty())
        set_flag(kModelDone, true);

    return show_journey_ui();
}

}} // namespace

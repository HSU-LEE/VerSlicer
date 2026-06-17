#include "SlicePilotRestrictions.hpp"

#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"

namespace Slic3r {
namespace SlicePilot {

namespace {

bool printer_model_is_bbl(const std::string& model, const PresetBundle& bundle)
{
    if (model.empty())
        return false;
    auto it = bundle.vendors.find(VENDOR_BBL);
    if (it == bundle.vendors.end())
        return false;
    for (const auto& vendor_model : it->second.models) {
        if (vendor_model.name == model)
            return true;
    }
    return false;
}

} // namespace

bool is_vendor_allowed_for_slicepilot(const std::string& vendor_name)
{
    return vendor_name == VENDOR_BBL || vendor_name == VENDOR_ORCA_FILAMENT_LIB;
}

bool is_bbl_printer_preset(const Preset& preset, const PresetBundle* bundle)
{
    if (preset.type != Preset::TYPE_PRINTER)
        return false;

    if (preset.vendor != nullptr && preset.vendor->id == VENDOR_BBL)
        return true;

    if (bundle == nullptr)
        return false;

    const std::string model = preset.config.opt_string("printer_model");
    if (printer_model_is_bbl(model, *bundle))
        return true;

    const Preset* base = bundle->printers.get_preset_base(preset);
    if (base != nullptr && base != &preset)
        return is_bbl_printer_preset(*base, bundle);

    return false;
}

void enforce_bbl_only_bundle(PresetBundle& bundle)
{
    for (Preset& preset : bundle.printers) {
        if (!is_bbl_printer_preset(preset, &bundle)) {
            preset.is_compatible = false;
            preset.is_visible    = false;
        }
    }

    if (!is_active_printer_bbl(bundle)) {
        const std::deque<Preset>& presets = bundle.printers.get_presets();
        for (size_t i = 0; i < presets.size(); ++i) {
            if (is_bbl_printer_preset(presets[i], &bundle) && presets[i].is_visible) {
                bundle.printers.select_preset(i);
                break;
            }
        }
    }

    bundle.update_compatible(PresetSelectCompatibleType::Always);
}

bool is_active_printer_bbl(const PresetBundle& bundle)
{
    return is_bbl_printer_preset(bundle.printers.get_edited_preset(), &bundle);
}

} // namespace SlicePilot
} // namespace Slic3r

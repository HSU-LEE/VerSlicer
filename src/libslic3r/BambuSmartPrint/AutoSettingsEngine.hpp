#ifndef slic3r_AutoSettingsEngine_hpp_
#define slic3r_AutoSettingsEngine_hpp_

#include "BambuSmartPrintTypes.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"

namespace Slic3r {
namespace BambuSmartPrint {

class AutoSettingsEngine
{
public:
    static ModelAnalysis analyze_model(const Model& model);
    static ModelAnalysis analyze_model(const Model& model, const DynamicPrintConfig& config);
    static AutoSettingsResult suggest_settings(const Model& model, const DynamicPrintConfig& base_config,
                                               const PrinterLearningProfile* learning = nullptr,
                                               const SliceAnalysis* slice = nullptr);
    static AutoSettingsResult suggest_settings_for_objects(const std::vector<ModelObject*>& objects,
                                                           const DynamicPrintConfig& base_config,
                                                           const PrinterLearningProfile* learning = nullptr,
                                                           const SliceAnalysis* slice = nullptr,
                                                           int plate_index = -1);
    static std::string suggest_filament_preset_name(const PresetBundle& bundle, const ModelAnalysis& analysis);
    static std::string suggested_orientation_hint(const ModelAnalysis& analysis);
    static AutoSettingsResult apply_safe_mode_to_suggestions(const DynamicPrintConfig& base_config,
                                                             AutoSettingsResult result);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

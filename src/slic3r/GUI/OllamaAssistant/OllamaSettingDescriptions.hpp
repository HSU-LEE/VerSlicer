#ifndef slic3r_OllamaSettingDescriptions_hpp_
#define slic3r_OllamaSettingDescriptions_hpp_

#include "libslic3r/BambuSmartPrint/BambuSmartPrintTypes.hpp"

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

/** Human-readable labels and situational explanations for AI set_config diffs. */
class OllamaSettingDescriptions
{
public:
    static std::string setting_label(const std::string& key, bool korean);
    static std::string format_value(const std::string& key, const std::string& raw, bool korean);

    /** One-line reason for a single config change (old → new). */
    static std::string change_reason(const BambuSmartPrint::SettingChange& change, bool korean);

    /** Dialog line: "Brim width: 0 mm → 5 mm — …" */
    static std::string preview_line(const BambuSmartPrint::SettingChange& change, bool korean);

    /** Summary paragraph from all changes; prefers accurate text over mismatched model message. */
    static std::string build_summary(const std::vector<BambuSmartPrint::SettingChange>& changes,
                                     const std::string& assistant_message, bool korean);

    /** Short "what to expect" bullets for workflow insights / risk factors. */
    static std::vector<std::string> expected_effects(const std::vector<BambuSmartPrint::SettingChange>& changes,
                                                     bool korean);
};

}} // namespace

#endif

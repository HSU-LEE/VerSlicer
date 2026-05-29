#ifndef slic3r_ConfigSnapshot_hpp_
#define slic3r_ConfigSnapshot_hpp_

#include "BambuSmartPrintTypes.hpp"
#include <string>
#include <vector>

namespace Slic3r {
namespace BambuSmartPrint {

class ConfigSnapshot
{
public:
    static std::string to_json(const DynamicPrintConfig& config);
    static DynamicPrintConfig from_json(const std::string& json_str);
    static DynamicPrintConfig from_json_safe(const std::string& json_str);
    static std::vector<SettingChange> diff(const DynamicPrintConfig& before, const DynamicPrintConfig& after);
    static std::string fingerprint(const DynamicPrintConfig& config);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

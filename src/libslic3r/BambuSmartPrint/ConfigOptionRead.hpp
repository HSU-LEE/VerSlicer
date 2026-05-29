#ifndef slic3r_BambuSmartPrintConfigOptionRead_hpp_
#define slic3r_BambuSmartPrintConfigOptionRead_hpp_

#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {
namespace BambuSmartPrint {

float config_get_float(const DynamicPrintConfig& cfg, const std::string& key, float fallback = 0.f);
int   config_get_int(const DynamicPrintConfig& cfg, const std::string& key, int fallback = 0);
bool  config_get_bool(const DynamicPrintConfig& cfg, const std::string& key, bool fallback = false);

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

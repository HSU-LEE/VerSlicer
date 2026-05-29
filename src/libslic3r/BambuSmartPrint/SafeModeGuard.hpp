#ifndef slic3r_SafeModeGuard_hpp_
#define slic3r_SafeModeGuard_hpp_

#include "BambuSmartPrintTypes.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {
namespace BambuSmartPrint {

struct SafeModeResult {
    DynamicPrintConfig config;
    std::vector<SettingChange> blocked_changes;
    std::vector<std::string> warnings;
    bool had_blocks{ false };
};

class SafeModeGuard
{
public:
    static bool is_enabled();
    static void set_enabled(bool on);

    // Clamp / reject dangerous deltas; returns adjusted config and blocked list.
    static SafeModeResult apply(const DynamicPrintConfig& before, const DynamicPrintConfig& proposed);

    static bool requires_user_approval(const SettingChange& change, const DynamicPrintConfig& before);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

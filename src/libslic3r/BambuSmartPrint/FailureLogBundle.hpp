#ifndef slic3r_FailureLogBundle_hpp_
#define slic3r_FailureLogBundle_hpp_

#include "BambuSmartPrintTypes.hpp"
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace Slic3r {
namespace BambuSmartPrint {

struct FailureLogBundleResult {
    std::string bundle_dir;
    std::string manifest_path;
    std::string config_json_path;
    std::string printer_status_path;
    std::string gcode_path;
    std::string camera_note_path;
    bool success{ false };
    std::vector<std::string> errors;
};

class FailureLogBundle
{
public:
    // Optional extra printer telemetry (from GUI DeviceManager).
    static FailureLogBundleResult capture(const PrintFailureRecord& record,
                                          const nlohmann::json* extra_printer_fields = nullptr);
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

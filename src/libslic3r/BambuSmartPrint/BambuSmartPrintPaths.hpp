#ifndef slic3r_BambuSmartPrintPaths_hpp_
#define slic3r_BambuSmartPrintPaths_hpp_

#include <string>

namespace Slic3r {
namespace BambuSmartPrint {

// Resolves {data_dir()}/bambu_smart_print and migrates legacy data once if needed.
std::string smart_print_data_dir();

// Override for unit tests (call before load). Empty restores default.
void set_smart_print_data_dir_override(const std::string& path);
void clear_smart_print_data_dir_override();

void ensure_smart_print_storage_ready();

// User-writable error catalog override path.
std::string user_error_catalog_path();

// Atomically replace path contents (Windows-safe: removes existing file before rename).
bool atomic_write_text_file(const std::string& path, const std::string& contents);

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

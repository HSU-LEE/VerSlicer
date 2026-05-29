#ifndef slic3r_SmartPrintDataManager_hpp_
#define slic3r_SmartPrintDataManager_hpp_

#include <string>

namespace Slic3r {
namespace BambuSmartPrint {

struct SmartPrintDataExportResult {
    bool        ok{ false };
    std::string path;
    std::string error;
};

struct SmartPrintDataImportResult {
    bool        ok{ false };
    std::string error;
    int         files_copied{ 0 };
};

SmartPrintDataExportResult export_backup_folder(const std::string& parent_dir);
SmartPrintDataImportResult import_backup_folder(const std::string& backup_dir, bool merge = true);

bool append_user_mc_error(int mc_code, const std::string& category, const std::string& title,
                            const std::string& description, std::string* error_out = nullptr);

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

#include "SmartPrintDataManager.hpp"
#include "BambuSmartPrintPaths.hpp"
#include "BambuErrorCatalog.hpp"
#include "FailureDatabase.hpp"
#include "PrinterLearningStore.hpp"

#include "libslic3r/Utils.hpp"
#include "nlohmann/json.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <chrono>

namespace Slic3r {
namespace BambuSmartPrint {

namespace fs = boost::filesystem;
using json = nlohmann::json;

static std::string timestamp_folder()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", std::localtime(&t));
    return std::string("bambu_smart_print_") + buf;
}

SmartPrintDataExportResult export_backup_folder(const std::string& parent_dir)
{
    SmartPrintDataExportResult r;
    try {
        const fs::path dest = fs::path(parent_dir) / timestamp_folder();
        fs::create_directories(dest);
        const fs::path src = smart_print_data_dir();
        if (fs::exists(src))
            fs::copy(src, dest / "data", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        const std::string catalog = user_error_catalog_path();
        if (fs::exists(catalog))
            fs::copy_file(catalog, dest / "error_catalog.json", fs::copy_options::overwrite_existing);
        r.path = dest.string();
        r.ok   = true;
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}

SmartPrintDataImportResult import_backup_folder(const std::string& backup_dir, bool merge)
{
    SmartPrintDataImportResult r;
    try {
        const fs::path root(backup_dir);
        fs::path src = root / "data";
        if (!fs::exists(src))
            src = root;
        if (!fs::is_directory(src)) {
            r.error = "Backup folder does not contain Smart Print data.";
            return r;
        }
        const fs::path dest = smart_print_data_dir();
        fs::create_directories(dest);
        if (!merge) {
            FailureDatabase::instance().clear_all_data();
            PrinterLearningStore::instance().clear_all_data();
        }
        for (fs::directory_iterator it(src); it != fs::directory_iterator(); ++it) {
            const fs::path target = dest / it->path().filename();
            if (fs::is_directory(it->path()))
                fs::copy(it->path(), target,
                         fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            else {
                fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing);
                ++r.files_copied;
            }
        }
        const fs::path cat = root / "error_catalog.json";
        if (fs::exists(cat))
            fs::copy_file(cat, user_error_catalog_path(), fs::copy_options::overwrite_existing);
        FailureDatabase::instance().load();
        PrinterLearningStore::instance().load();
        r.ok = true;
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}

bool append_user_mc_error(int mc_code, const std::string& category, const std::string& title,
                          const std::string& description, std::string* error_out)
{
    if (mc_code <= 0) {
        if (error_out) *error_out = "Invalid MC error code.";
        return false;
    }
    const std::string path = user_error_catalog_path();
    try {
        json root;
        if (fs::exists(path)) {
            boost::nowide::ifstream ifs(path);
            ifs >> root;
        }
        if (!root.contains("mc_errors"))
            root["mc_errors"] = json::object();
        root["mc_errors"][std::to_string(mc_code)] = {
            { "category", category },
            { "title", title },
            { "description", description }
        };
        if (!atomic_write_text_file(path, root.dump(2)))
            throw std::runtime_error("Could not write catalog file.");
        BambuErrorCatalog::reload_user_catalog();
        return true;
    } catch (const std::exception& e) {
        if (error_out) *error_out = e.what();
        return false;
    }
}

} // namespace BambuSmartPrint
} // namespace Slic3r

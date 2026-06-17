#include "BambuSmartPrintPaths.hpp"

#include "libslic3r/Utils.hpp"

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/system/error_code.hpp>

namespace Slic3r {
namespace BambuSmartPrint {

namespace fs = boost::filesystem;

static void copy_tree_if_missing(const fs::path& from, const fs::path& to)
{
    if (!fs::exists(from) || fs::exists(to))
        return;
    try {
        fs::create_directories(to.parent_path());
        for (fs::directory_iterator it(from); it != fs::directory_iterator(); ++it) {
            const fs::path dest = to / it->path().filename();
            if (fs::is_directory(it->path()))
                fs::copy_directory(it->path(), dest);
            else
                fs::copy_file(it->path(), dest, fs::copy_options::overwrite_existing);
        }
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(warning) << "BambuSmartPrint: data migration skipped: " << e.what();
    }
}

static std::string& data_dir_override()
{
    static std::string s;
    return s;
}

std::string smart_print_data_dir()
{
    if (!data_dir_override().empty())
        return data_dir_override();
    return (fs::path(data_dir()) / "bambu_smart_print").string();
}

void set_smart_print_data_dir_override(const std::string& path)
{
    data_dir_override() = path;
}

void clear_smart_print_data_dir_override()
{
    data_dir_override().clear();
}

std::string user_error_catalog_path()
{
    return (fs::path(smart_print_data_dir()) / "error_catalog.json").string();
}

bool atomic_write_text_file(const std::string& path, const std::string& contents)
{
    const std::string tmp = path + ".tmp";
    {
        boost::nowide::ofstream ofs(tmp, std::ios::binary);
        if (!ofs)
            return false;
        ofs << contents;
        if (!ofs.good())
            return false;
    }
    boost::system::error_code ec;
    fs::remove(path, ec);
    ec.clear();
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

void ensure_smart_print_storage_ready()
{
    static bool done = false;
    if (done)
        return;
    done = true;

    const fs::path target = smart_print_data_dir();
    fs::create_directories(target);

    const fs::path parent = fs::path(data_dir()).parent_path();
    for (const char* legacy_app : { "Verslicer", "orca-slicer", "BambuStudio" }) {
        const fs::path legacy = parent / legacy_app / "bambu_smart_print";
        if (legacy == target)
            continue;
        if (fs::exists(legacy))
            copy_tree_if_missing(legacy, target);
    }
}

} // namespace BambuSmartPrint
} // namespace Slic3r

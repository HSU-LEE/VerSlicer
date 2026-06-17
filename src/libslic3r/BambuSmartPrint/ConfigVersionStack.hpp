#ifndef slic3r_ConfigVersionStack_hpp_
#define slic3r_ConfigVersionStack_hpp_

#include "BambuSmartPrintTypes.hpp"
#include "libslic3r/PrintConfig.hpp"
#include <deque>
#include <string>

namespace Slic3r {
namespace BambuSmartPrint {

struct ConfigVersionEntry {
    std::string id;
    int64_t     timestamp_utc_ms{ 0 };
    std::string label;
    std::string printer_id;
    int         plate_index{ -1 };
    DynamicPrintConfig config;
};

class ConfigVersionStack
{
public:
    static ConfigVersionStack& instance();

    void load();
    bool save();

    void push(const std::string& label, const std::string& printer_id, int plate_index,
              const DynamicPrintConfig& config);
    bool pop(ConfigVersionEntry* out);
    bool peek(ConfigVersionEntry* out) const;
    bool restore_previous(DynamicPrintConfig* out_config, ConfigVersionEntry* meta = nullptr);
    size_t size() const;
    void clear();

private:
    ConfigVersionStack() = default;
    static constexpr size_t kMaxEntries = 32;
    std::deque<ConfigVersionEntry> m_stack;
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

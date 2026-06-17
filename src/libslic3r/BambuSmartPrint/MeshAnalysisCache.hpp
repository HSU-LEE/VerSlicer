#ifndef slic3r_MeshAnalysisCache_hpp_
#define slic3r_MeshAnalysisCache_hpp_

#include "BambuSmartPrintTypes.hpp"
#include <list>
#include <map>
#include <string>
#include <mutex>

namespace Slic3r {

class Model;
class DynamicPrintConfig;
class ModelObject;

namespace BambuSmartPrint {

class MeshAnalysisCache
{
public:
    static MeshAnalysisCache& instance();

    std::string cache_key_for_objects(const std::vector<ModelObject*>& objects,
                                      const DynamicPrintConfig& config,
                                      int plate_index = -1) const;

    bool lookup(const std::string& key, ModelAnalysis* out) const;
    void store(const std::string& key, const ModelAnalysis& analysis);
    void clear();

private:
    static constexpr size_t kMaxEntries = 16;

    mutable std::mutex m_mutex;
    std::map<std::string, ModelAnalysis> m_entries;
    mutable std::list<std::string>       m_lru;
};

} // namespace BambuSmartPrint
} // namespace Slic3r

#endif

#include "MeshAnalysisCache.hpp"
#include "ConfigSnapshot.hpp"

#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"

#include <functional>

namespace Slic3r {
namespace BambuSmartPrint {

MeshAnalysisCache& MeshAnalysisCache::instance()
{
    static MeshAnalysisCache c;
    return c;
}

std::string MeshAnalysisCache::cache_key_for_objects(const std::vector<ModelObject*>& objects,
                                                     const DynamicPrintConfig& config,
                                                     int plate_index) const
{
    size_t h = 0;
    for (const ModelObject* obj : objects) {
        if (!obj) continue;
        h ^= std::hash<std::string>{}(obj->name);
        h ^= std::hash<size_t>{}(obj->volumes.size());
        for (const ModelVolume* vol : obj->volumes) {
            if (!vol) continue;
            try {
                const TriangleMesh mesh = vol->mesh();
                if (mesh.its.vertices.empty())
                    continue;
                h ^= std::hash<double>{}(mesh.stats().volume);
                h ^= std::hash<size_t>{}(mesh.its.indices.size());
            } catch (...) {
                continue;
            }
        }
        if (!obj->instances.empty() && obj->instances.front()) {
            const Geometry::Transformation trafo = obj->instances.front()->get_transformation();
            const Vec3d t = trafo.get_offset();
            h ^= std::hash<double>{}(t.x());
            h ^= std::hash<double>{}(t.y());
            h ^= std::hash<double>{}(t.z());
        }
    }
    h ^= std::hash<std::string>{}(ConfigSnapshot::fingerprint(config));
    if (plate_index >= 0)
        h ^= std::hash<int>{}(plate_index);
    return std::to_string(h);
}

bool MeshAnalysisCache::lookup(const std::string& key, ModelAnalysis* out) const
{
    if (!out || key.empty())
        return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(key);
    if (it == m_entries.end())
        return false;
    *out = it->second;
    m_lru.remove(key);
    m_lru.push_back(key);
    return true;
}

void MeshAnalysisCache::store(const std::string& key, const ModelAnalysis& analysis)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries[key] = analysis;
    m_lru.remove(key);
    m_lru.push_back(key);
    while (m_entries.size() > kMaxEntries) {
        const std::string& evict = m_lru.front();
        m_entries.erase(evict);
        m_lru.pop_front();
    }
}

void MeshAnalysisCache::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
    m_lru.clear();
}

} // namespace BambuSmartPrint
} // namespace Slic3r

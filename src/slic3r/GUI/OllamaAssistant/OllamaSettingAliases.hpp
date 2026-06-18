#ifndef slic3r_OllamaSettingAliases_hpp_
#define slic3r_OllamaSettingAliases_hpp_

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

/** Korean aliases + symptom→key boosts for setting search (no .po runtime required). */
class OllamaSettingAliases
{
public:
    static std::vector<std::string> ko_terms_for_key(const std::string& key);
    static int                        symptom_boost(const std::string& query_lower, const std::string& key);
    /** Keys strongly suggested by symptom phrases in the query. */
    static std::vector<std::string> keys_from_symptoms(const std::string& query);
};

}} // namespace

#endif

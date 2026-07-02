#ifndef slic3r_OllamaSettingAliases_hpp_
#define slic3r_OllamaSettingAliases_hpp_

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

/** Korean setting-name aliases for catalog search (not symptom→action rules). */
class OllamaSettingAliases
{
public:
    static std::vector<std::string> ko_terms_for_key(const std::string& key);
};

}} // namespace

#endif

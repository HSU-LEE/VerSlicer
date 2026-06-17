#ifndef slic3r_AICoachApplyDedup_hpp_
#define slic3r_AICoachApplyDedup_hpp_

#include "AICoachTypes.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>

namespace Slic3r { namespace GUI {

/** Tracks set_config actions applied via Coach or Ollama to suppress duplicate Coach cards. */
class AICoachApplyDedup
{
public:
    static AICoachApplyDedup& instance();

    void record_applied_root(const nlohmann::json& root, const char* source);
    bool card_is_redundant(const AICoachCard& card) const;
    void clear();

private:
    AICoachApplyDedup() = default;

    static std::string fingerprint_set_config(const nlohmann::json& action);
    static void        collect_fingerprints(const nlohmann::json& root, std::unordered_set<std::string>& out);

    std::unordered_set<std::string> m_applied_fingerprints;
};

}} // namespace

#endif

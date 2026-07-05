#ifndef slic3r_OllamaConfigProposalCache_hpp_
#define slic3r_OllamaConfigProposalCache_hpp_

#include "libslic3r/BambuSmartPrint/AutoConfigEngine.hpp"

#include <mutex>
#include <optional>
#include <string>

namespace Slic3r { namespace GUI {

/**
 * Turn-scoped, thread-safe holder for the latest deterministic ConfigProposal
 * derived by the AutoConfigEngine. The assist-loop context builders read this so
 * the LLM refines a concrete proposal instead of inventing settings from scratch.
 */
class OllamaConfigProposalCache
{
public:
    static OllamaConfigProposalCache& instance();

    /** Replace the cached proposal (thread-safe). */
    void set(const BambuSmartPrint::ConfigProposal& proposal);

    /** Snapshot copy of the latest proposal, or nullopt when none cached. */
    std::optional<BambuSmartPrint::ConfigProposal> latest() const;

    /** Identity of the latest proposal (empty when none). Used for cache invalidation. */
    std::string latest_proposal_id() const;

    void clear();

private:
    OllamaConfigProposalCache() = default;

    mutable std::mutex                             m_mutex;
    std::optional<BambuSmartPrint::ConfigProposal> m_proposal;
};

}} // namespace

#endif

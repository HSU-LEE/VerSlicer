#include "OllamaConfigProposalCache.hpp"

namespace Slic3r { namespace GUI {

OllamaConfigProposalCache& OllamaConfigProposalCache::instance()
{
    static OllamaConfigProposalCache s;
    return s;
}

void OllamaConfigProposalCache::set(const BambuSmartPrint::ConfigProposal& proposal)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_proposal = proposal;
}

std::optional<BambuSmartPrint::ConfigProposal> OllamaConfigProposalCache::latest() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_proposal;
}

std::string OllamaConfigProposalCache::latest_proposal_id() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_proposal.has_value() ? m_proposal->proposal_id : std::string{};
}

void OllamaConfigProposalCache::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_proposal.reset();
}

}} // namespace

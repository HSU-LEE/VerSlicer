#include "AssistantConfigProposalCache.hpp"

namespace Slic3r { namespace GUI {

AssistantConfigProposalCache& AssistantConfigProposalCache::instance()
{
    static AssistantConfigProposalCache s;
    return s;
}

void AssistantConfigProposalCache::set(const BambuSmartPrint::ConfigProposal& proposal)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_proposal = proposal;
}

std::optional<BambuSmartPrint::ConfigProposal> AssistantConfigProposalCache::latest() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_proposal;
}

std::string AssistantConfigProposalCache::latest_proposal_id() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_proposal.has_value() ? m_proposal->proposal_id : std::string{};
}

void AssistantConfigProposalCache::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_proposal.reset();
}

}} // namespace

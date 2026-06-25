#include "OllamaAgentEventBus.hpp"

namespace Slic3r { namespace GUI {

OllamaAgentEventBus& OllamaAgentEventBus::instance()
{
    static OllamaAgentEventBus bus;
    return bus;
}

void OllamaAgentEventBus::subscribe(OllamaAgentEventHandler handler)
{
    if (!handler)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_handlers.push_back(std::move(handler));
}

void OllamaAgentEventBus::publish(OllamaAgentEventKind kind, nlohmann::json payload)
{
    OllamaAgentEvent evt{kind, std::move(payload)};
    std::vector<OllamaAgentEventHandler> handlers;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_recent.push_back(evt);
        if (m_recent.size() > 32)
            m_recent.erase(m_recent.begin());
        handlers = m_handlers;
    }
    for (const auto& h : handlers)
        h(evt);
}

std::vector<OllamaAgentEvent> OllamaAgentEventBus::recent_events(size_t max_count) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_recent.size() <= max_count)
        return m_recent;
    return std::vector<OllamaAgentEvent>(m_recent.end() - static_cast<ptrdiff_t>(max_count), m_recent.end());
}

}} // namespace

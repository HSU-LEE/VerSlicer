#include "AssistantAgentEventBus.hpp"

namespace Slic3r { namespace GUI {

AssistantAgentEventBus& AssistantAgentEventBus::instance()
{
    static AssistantAgentEventBus bus;
    return bus;
}

AssistantSubscriptionId AssistantAgentEventBus::subscribe(AssistantAgentEventHandler handler)
{
    if (!handler)
        return 0;
    std::lock_guard<std::mutex> lock(m_mutex);
    const AssistantSubscriptionId id = m_next_id++;
    m_handlers.push_back({id, std::move(handler)});
    return id;
}

void AssistantAgentEventBus::unsubscribe(AssistantSubscriptionId id)
{
    if (id == 0)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_handlers.begin(); it != m_handlers.end(); ++it) {
        if (it->id == id) {
            m_handlers.erase(it);
            return;
        }
    }
}

void AssistantAgentEventBus::publish(AssistantAgentEventKind kind, nlohmann::json payload)
{
    AssistantAgentEvent evt{kind, std::move(payload)};
    // Copy handlers under the lock, then invoke without the lock held so a handler
    // may safely unsubscribe itself (one-shot subscribers) without deadlocking.
    std::vector<Subscriber> handlers;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_recent.push_back(evt);
        if (m_recent.size() > 32)
            m_recent.erase(m_recent.begin());
        handlers = m_handlers;
    }
    for (const auto& h : handlers)
        h.handler(evt);
}

std::vector<AssistantAgentEvent> AssistantAgentEventBus::recent_events(size_t max_count) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_recent.size() <= max_count)
        return m_recent;
    return std::vector<AssistantAgentEvent>(m_recent.end() - static_cast<ptrdiff_t>(max_count), m_recent.end());
}

}} // namespace

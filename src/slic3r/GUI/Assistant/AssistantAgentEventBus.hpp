#ifndef slic3r_AssistantAgentEventBus_hpp_
#define slic3r_AssistantAgentEventBus_hpp_

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI {

enum class AssistantAgentEventKind
{
    SliceDone,
    ConfigApplied,
    DialogOpened,
    ActionFailed,
    ModelLoaded,
};

struct AssistantAgentEvent
{
    AssistantAgentEventKind kind;
    nlohmann::json       payload;
};

using AssistantAgentEventHandler = std::function<void(const AssistantAgentEvent&)>;

/** Opaque id for a subscription; 0 means "no subscription". */
using AssistantSubscriptionId = std::size_t;

/** Lightweight in-process event bus for proactive agent triggers. */
class AssistantAgentEventBus
{
public:
    static AssistantAgentEventBus& instance();

    /** Register a handler. Returns an id usable with unsubscribe() (0 if handler was empty). */
    AssistantSubscriptionId subscribe(AssistantAgentEventHandler handler);

    /** Remove a previously registered handler. Safe to call from within a handler. */
    void unsubscribe(AssistantSubscriptionId id);

    void publish(AssistantAgentEventKind kind, nlohmann::json payload = nlohmann::json::object());
    std::vector<AssistantAgentEvent> recent_events(size_t max_count = 8) const;

private:
    struct Subscriber
    {
        AssistantSubscriptionId    id;
        AssistantAgentEventHandler handler;
    };

    mutable std::mutex              m_mutex;
    std::vector<Subscriber>         m_handlers;
    AssistantSubscriptionId            m_next_id{1};
    std::vector<AssistantAgentEvent>   m_recent;
};

}} // namespace

#endif

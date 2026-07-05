#ifndef slic3r_OllamaAgentEventBus_hpp_
#define slic3r_OllamaAgentEventBus_hpp_

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace Slic3r { namespace GUI {

enum class OllamaAgentEventKind
{
    SliceDone,
    ConfigApplied,
    DialogOpened,
    ActionFailed,
    ModelLoaded,
};

struct OllamaAgentEvent
{
    OllamaAgentEventKind kind;
    nlohmann::json       payload;
};

using OllamaAgentEventHandler = std::function<void(const OllamaAgentEvent&)>;

/** Opaque id for a subscription; 0 means "no subscription". */
using OllamaSubscriptionId = std::size_t;

/** Lightweight in-process event bus for proactive agent triggers. */
class OllamaAgentEventBus
{
public:
    static OllamaAgentEventBus& instance();

    /** Register a handler. Returns an id usable with unsubscribe() (0 if handler was empty). */
    OllamaSubscriptionId subscribe(OllamaAgentEventHandler handler);

    /** Remove a previously registered handler. Safe to call from within a handler. */
    void unsubscribe(OllamaSubscriptionId id);

    void publish(OllamaAgentEventKind kind, nlohmann::json payload = nlohmann::json::object());
    std::vector<OllamaAgentEvent> recent_events(size_t max_count = 8) const;

private:
    struct Subscriber
    {
        OllamaSubscriptionId    id;
        OllamaAgentEventHandler handler;
    };

    mutable std::mutex              m_mutex;
    std::vector<Subscriber>         m_handlers;
    OllamaSubscriptionId            m_next_id{1};
    std::vector<OllamaAgentEvent>   m_recent;
};

}} // namespace

#endif

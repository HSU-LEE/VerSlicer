#ifndef slic3r_OllamaAgentEventBus_hpp_
#define slic3r_OllamaAgentEventBus_hpp_

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

/** Lightweight in-process event bus for proactive agent triggers. */
class OllamaAgentEventBus
{
public:
    static OllamaAgentEventBus& instance();

    void subscribe(OllamaAgentEventHandler handler);
    void publish(OllamaAgentEventKind kind, nlohmann::json payload = nlohmann::json::object());
    std::vector<OllamaAgentEvent> recent_events(size_t max_count = 8) const;

private:
    mutable std::mutex              m_mutex;
    std::vector<OllamaAgentEventHandler> m_handlers;
    std::vector<OllamaAgentEvent>   m_recent;
};

}} // namespace

#endif

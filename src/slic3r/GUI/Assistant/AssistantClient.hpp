#ifndef slic3r_AssistantClient_hpp_
#define slic3r_AssistantClient_hpp_

#include <functional>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

struct AssistantMessage
{
    std::string role;
    std::string content;
};

enum class AssistantCancelDomain
{
    Chat,
    Advisor,
    Planner,
    Resolver,
    All,
};

enum class AssistantRequestKind
{
    Chat,
    Advisor,
    Planner,
    Resolver,
};

class AssistantClient
{
public:
    explicit AssistantClient(std::string base_url);

    void set_base_url(std::string base_url) { m_base_url = std::move(base_url); }
    const std::string& base_url() const { return m_base_url; }

    using ChatCallback   = std::function<void(const std::string& assistant_text, const std::string& error)>;
    using StreamCallback = std::function<void(const std::string& chunk)>;
    using ModelsCallback = std::function<void(const std::vector<std::string>& models, const std::string& error)>;

    void chat(const std::string& model, const std::vector<AssistantMessage>& messages, ChatCallback callback,
              AssistantRequestKind kind = AssistantRequestKind::Chat, StreamCallback stream = nullptr,
              std::string tools_json = {});
    void list_models(ModelsCallback callback);

    static void cancel_active_requests(AssistantCancelDomain domain = AssistantCancelDomain::All);

private:
    std::string m_base_url;
};

}} // namespace

#endif

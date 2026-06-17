#ifndef slic3r_OllamaClient_hpp_
#define slic3r_OllamaClient_hpp_

#include <functional>
#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

struct OllamaMessage
{
    std::string role;
    std::string content;
};

enum class OllamaCancelDomain
{
    Chat,
    Advisor,
    All,
};

enum class OllamaRequestKind
{
    Chat,
    Advisor,
};

class OllamaClient
{
public:
    explicit OllamaClient(std::string base_url);

    void set_base_url(std::string base_url) { m_base_url = std::move(base_url); }
    const std::string& base_url() const { return m_base_url; }

    using ChatCallback   = std::function<void(const std::string& assistant_text, const std::string& error)>;
    using ModelsCallback = std::function<void(const std::vector<std::string>& models, const std::string& error)>;

    void chat(const std::string& model, const std::vector<OllamaMessage>& messages, ChatCallback callback,
              OllamaRequestKind kind = OllamaRequestKind::Chat);
    void list_models(ModelsCallback callback);
    /** Blocking; for model validation before chat. */
    std::vector<std::string> list_models_sync() const;

    static void cancel_active_requests(OllamaCancelDomain domain = OllamaCancelDomain::All);

private:
    std::string m_base_url;
};

}} // namespace

#endif

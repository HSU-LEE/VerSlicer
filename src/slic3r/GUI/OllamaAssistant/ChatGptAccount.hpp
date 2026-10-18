#ifndef slic3r_ChatGptAccount_hpp_
#define slic3r_ChatGptAccount_hpp_

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Slic3r {
class Http;
}

namespace Slic3r { namespace GUI {

struct ChatGptTurn
{
    std::string role;
    std::string content;
};

struct ChatGptCompletion
{
    std::string text;
    std::string error;
    unsigned    http_status{0};
    std::string model;
};

/** ChatGPT plan sign-in (loopback OAuth) and Responses API calls. No local model. */
class ChatGptAccount
{
public:
    static bool        linked();
    static std::string email();
    static void        sign_out();

    /** Blocking. Call off the UI thread. Empty string on success. */
    static std::string sign_in();

    static std::vector<std::string> list_models(std::string* error);

    static ChatGptCompletion complete(const std::string& model, const std::vector<ChatGptTurn>& messages,
                                      const std::string& tools_json,
                                      const std::shared_ptr<std::atomic<bool>>& cancel,
                                      const std::function<void(const std::string&)>& on_delta,
                                      const std::function<void(Slic3r::Http*)>& bind_http);
};

}} // namespace

#endif

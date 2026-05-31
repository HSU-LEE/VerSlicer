#ifndef slic3r_OllamaVoiceInput_hpp_
#define slic3r_OllamaVoiceInput_hpp_

#include <functional>
#include <memory>
#include <string>

namespace Slic3r { namespace GUI {

class OllamaVoiceInput
{
public:
    using FinalTextCallback = std::function<void(const std::string&)>;
    using ErrorCallback     = std::function<void(const std::string&)>;

    virtual ~OllamaVoiceInput() = default;

    virtual bool is_listening() const = 0;
    virtual void start()              = 0;
    virtual void stop()               = 0;

    void set_on_final_text(FinalTextCallback cb) { m_on_final = std::move(cb); }
    void set_on_error(ErrorCallback cb)          { m_on_error = std::move(cb); }

protected:
    void emit_final(const std::string& s) { if (m_on_final) m_on_final(s); }
    void emit_error(const std::string& e) { if (m_on_error) m_on_error(e); }
    FinalTextCallback final_callback() const { return m_on_final; }
    ErrorCallback     error_callback() const { return m_on_error; }

private:
    FinalTextCallback m_on_final;
    ErrorCallback     m_on_error;
};

std::unique_ptr<OllamaVoiceInput> create_ollama_voice_input();

}} // namespace

#endif


#ifdef __APPLE__

#include "OllamaVoiceInput.hpp"
#include "../GUI_App.hpp"

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <Speech/Speech.h>
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 140000
#import <AVFAudio/AVFAudio.h>
#endif

#include <atomic>
#include <memory>

namespace Slic3r { namespace GUI {

static std::string ns_to_std(NSString* s)
{
    if (!s) return {};
    const char* c = [s UTF8String];
    return c ? std::string(c) : std::string();
}

static std::string nserr_to_string(NSError* e)
{
    if (!e) return "unknown error";
    std::string out;
    out += ns_to_std(e.domain);
    out += " ";
    out += std::to_string((long)e.code);
    const std::string desc = ns_to_std(e.localizedDescription);
    if (!desc.empty()) {
        out += ": ";
        out += desc;
    }
    return out;
}

static bool is_speech_cancel_error(NSError* error)
{
    if (!error)
        return false;
    if ([error.domain isEqualToString:NSCocoaErrorDomain] && error.code == NSUserCancelledError)
        return true;
    // Speech cancellation (domain varies by macOS version).
    if (error.code == 216 || error.code == 301)
        return true;
    return false;
}

static bool is_no_speech_detected_error(NSError* error)
{
    if (!error)
        return false;
    if (error.code != 1110)
        return false;
    const std::string domain = ns_to_std(error.domain);
    return domain.find("Assistant") != std::string::npos
        || domain.find("kAFAssistant") != std::string::npos;
}

struct VoiceSession
{
    std::shared_ptr<std::atomic<bool>> app_alive;
    std::atomic<bool>                  session_active{true};
    std::atomic<bool>                  stopping{false};
    std::atomic<bool>                  capture_active{true};
    std::atomic<bool>                  awaiting_final{false};
    std::atomic<bool>                  end_audio_sent{false};
    std::atomic<bool>                  result_delivered{false};
    std::atomic<bool>                  teardown_started{false};
    std::atomic<bool>                  task_cancel_sent{false};
    std::string                        last_partial_text;
    OllamaVoiceInput::FinalTextCallback on_final;
    OllamaVoiceInput::ErrorCallback     on_error;

    AVAudioEngine*                           audioEngine{nil};
    SFSpeechAudioBufferRecognitionRequest* request{nil};
    SFSpeechRecognitionTask*                 task{nil};
    SFSpeechRecognizer*                      recognizer{nil};
    AVAudioConverter*                        audioConverter{nil};
    AVAudioPCMBuffer*                        speechPCMBuffer{nil};

    bool append_captured_buffer(AVAudioPCMBuffer* tap_buffer)
    {
        if (!tap_buffer || !request)
            return false;

        AVAudioPCMBuffer* to_append = tap_buffer;
        if (audioConverter && speechPCMBuffer) {
            AVAudioConverterInputBlock input_block = ^AVAudioBuffer*(AVAudioPacketCount in_num_packets,
                                                                     AVAudioConverterInputStatus* out_status) {
                (void)in_num_packets;
                *out_status = AVAudioConverterInputStatus_HaveData;
                return tap_buffer;
            };

            speechPCMBuffer.frameLength = 0;
            NSError* conv_err         = nil;
            const AVAudioConverterOutputStatus status =
                [audioConverter convertToBuffer:speechPCMBuffer
                                          error:&conv_err
                             withInputFromBlock:input_block];
            if (status == AVAudioConverterOutputStatus_Error || conv_err)
                return false;
            if (speechPCMBuffer.frameLength == 0)
                return false;
            to_append = speechPCMBuffer;
        }

        @try {
            [request appendAudioPCMBuffer:to_append];
            return true;
        } @catch (NSException*) {
            return false;
        }
    }

    void stop_audio_capture()
    {
        capture_active.store(false);
        AVAudioEngine* engine = audioEngine;
        if (!engine)
            return;
        @try {
            [engine.inputNode removeTapOnBus:0];
        } @catch (NSException*) {
        }
        [engine stop];
        audioEngine = nil;
    }

    void mark_end_audio()
    {
        bool expected = false;
        if (!end_audio_sent.compare_exchange_strong(expected, true))
            return;
        SFSpeechAudioBufferRecognitionRequest* req = request;
        if (!req)
            return;
        @try {
            [req endAudio];
        } @catch (NSException*) {
        }
    }

    void teardown(bool cancel_task)
    {
        // Reentrant from SFSpeech resultHandler if [task cancel] runs inside the handler.
        if (teardown_started.exchange(true))
            return;

        stopping.store(true);
        session_active.store(false);
        awaiting_final.store(false);
        capture_active.store(false);

        stop_audio_capture();

        SFSpeechRecognitionTask* t = task;
        task                       = nil;
        request                    = nil;
        recognizer                 = nil;
        audioConverter             = nil;
        speechPCMBuffer            = nil;

        if (t && cancel_task && !task_cancel_sent.exchange(true)) {
            @try {
                [t cancel];
            } @catch (NSException*) {
            }
        }
    }
};

// Shared state for async blocks — never capture the C++ voice object (`this`) in ObjC blocks.
struct MacVoiceShared
{
    std::atomic<bool>     alive{true};
    std::atomic<bool>     listening{false};
    std::atomic<uint64_t> generation{0};
    std::shared_ptr<VoiceSession> session;

    OllamaVoiceInput::FinalTextCallback on_final;
    OllamaVoiceInput::ErrorCallback     on_error;

    bool is_start_valid(uint64_t gen) const
    {
        return alive.load(std::memory_order_acquire)
            && listening.load(std::memory_order_acquire)
            && generation.load(std::memory_order_acquire) == gen;
    }
};

namespace {

static bool is_current_session(const std::shared_ptr<MacVoiceShared>& sh, const std::shared_ptr<VoiceSession>& session)
{
    return sh && session && sh->session == session;
}

static void stop_session_locked(const std::shared_ptr<MacVoiceShared>& sh, bool cancel_task)
{
    if (!sh)
        return;
    if (auto session = sh->session) {
        session->result_delivered.store(true);
        session->teardown(cancel_task);
        sh->session.reset();
    }
}

static void finish_session(const std::shared_ptr<MacVoiceShared>& sh,
                           const std::shared_ptr<VoiceSession>& session,
                           bool cancel_task)
{
    if (!session)
        return;
    session->teardown(cancel_task);
    if (sh && sh->session == session)
        sh->session.reset();
    if (sh)
        sh->listening.store(false);
}

static bool try_deliver_recognized_text(const std::shared_ptr<MacVoiceShared>& sh,
                                        const std::shared_ptr<VoiceSession>& session,
                                        const std::string& text)
{
    if (!is_current_session(sh, session) || text.empty())
        return false;

    bool expected = false;
    if (!session->result_delivered.compare_exchange_strong(expected, true))
        return false;

    session->awaiting_final.store(false);
    OllamaVoiceInput::FinalTextCallback on_final = session->on_final;
    // Do not cancel the task here — this path is reached from resultHandler; cancel reenters the handler.
    finish_session(sh, session, /*cancel_task*/ false);
    if (on_final)
        on_final(text);
    return true;
}

static void schedule_finalize_timeout(const std::shared_ptr<MacVoiceShared>& sh,
                                      const std::shared_ptr<VoiceSession>& session)
{
    std::weak_ptr<VoiceSession> weak_session = session;
    std::weak_ptr<MacVoiceShared> weak_sh    = sh;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        auto s = weak_session.lock();
        auto sh2 = weak_sh.lock();
        if (!is_current_session(sh2, s) || !s->awaiting_final.load(std::memory_order_acquire))
            return;
        if (!try_deliver_recognized_text(sh2, s, s->last_partial_text))
            finish_session(sh2, s, /*cancel_task*/ true);
    });
}

static void graceful_stop_listening(const std::shared_ptr<MacVoiceShared>& sh)
{
    if (!sh)
        return;
    auto session = sh->session;
    if (!session || session->stopping.load())
        return;

    session->awaiting_final.store(true);
    session->stop_audio_capture();
    session->mark_end_audio();

    if (!session->request) {
        finish_session(sh, session, /*cancel_task*/ true);
        return;
    }

    schedule_finalize_timeout(sh, session);
}

static NSLocale* resolve_supported_locale(NSSet<NSLocale*>* supported, NSLocale* candidate)
{
    if (!supported || !candidate)
        return nil;
    if ([supported containsObject:candidate])
        return candidate;

    NSString* lang = [candidate objectForKey:NSLocaleLanguageCode];
    if (!lang.length)
        return nil;

    for (NSLocale* loc in supported) {
        if ([[loc objectForKey:NSLocaleLanguageCode] isEqualToString:lang])
            return loc;
    }
    return nil;
}

static NSLocale* try_locale_identifiers(NSSet<NSLocale*>* supported,
                                       const std::vector<const char*>& idents)
{
    for (const char* ident : idents) {
        if (!ident || !*ident)
            continue;
        if (NSLocale* loc = resolve_supported_locale(
                supported, [NSLocale localeWithLocaleIdentifier:[NSString stringWithUTF8String:ident]]))
            return loc;
    }
    return nil;
}

static NSLocale* try_wx_language_code(NSSet<NSLocale*>* supported, const std::string& code)
{
    if (code.empty())
        return nil;

    std::vector<std::string> variants{code};
    const size_t underscore = code.find('_');
    if (underscore != std::string::npos) {
        std::string hyphen = code;
        hyphen[underscore] = '-';
        variants.push_back(std::move(hyphen));
    }
    const size_t dash = code.find('-');
    if (dash != std::string::npos && dash > 0)
        variants.push_back(code.substr(0, dash));
    else if (underscore != std::string::npos && underscore > 0)
        variants.push_back(code.substr(0, underscore));

    std::vector<const char*> idents;
    idents.reserve(variants.size());
    for (const std::string& v : variants)
        idents.push_back(v.c_str());
    return try_locale_identifiers(supported, idents);
}

static NSLocale* first_supported_preferred(NSSet<NSLocale*>* supported, NSString* prefix)
{
    for (NSString* pref in [NSLocale preferredLanguages]) {
        if (![pref hasPrefix:prefix])
            continue;
        if (NSLocale* loc = resolve_supported_locale(supported, [NSLocale localeWithLocaleIdentifier:pref]))
            return loc;
    }
    return nil;
}

static NSLocale* speech_locale()
{
    NSSet<NSLocale*>* supported = [SFSpeechRecognizer supportedLocales];

    // Dictation should follow how the user speaks (macOS language list), not Verslicer UI.
    // If Korean/Japanese appears anywhere in preferred languages, use it even when English is first.
    if (NSLocale* loc = first_supported_preferred(supported, @"ko"))
        return loc;
    if (NSLocale* loc = first_supported_preferred(supported, @"ja"))
        return loc;

    for (NSString* pref in [NSLocale preferredLanguages]) {
        if (NSLocale* loc = resolve_supported_locale(supported, [NSLocale localeWithLocaleIdentifier:pref]))
            return loc;
    }

    if (NSLocale* loc = resolve_supported_locale(supported, [NSLocale currentLocale]))
        return loc;

    const wxString app_lang = Slic3r::GUI::wxGetApp().current_language_code();
    if (NSLocale* loc = try_wx_language_code(supported, app_lang.ToUTF8().data()))
        return loc;

    if (NSLocale* loc = try_locale_identifiers(supported, {"ko-KR", "ja-JP", "en-US"}))
        return loc;

    return [NSLocale localeWithLocaleIdentifier:@"en-US"];
}

static bool is_valid_audio_format(AVAudioFormat* format)
{
    return format && format.sampleRate > 0 && format.channelCount > 0;
}

static void log_audio_format(NSString* label, AVAudioFormat* format)
{
    if (!format) {
        NSLog(@"OllamaVoice: %@ = (nil)", label);
        return;
    }
    NSLog(@"OllamaVoice: %@ = %@ sampleRate=%f channels=%u interleaved=%d",
          label,
          format,
          format.sampleRate,
          (unsigned)format.channelCount,
          format.isInterleaved);
}

// Tap format must match the input node's hardware format — not SFSpeech nativeAudioFormat.
static AVAudioFormat* input_node_recording_format(AVAudioInputNode* inputNode, bool log_debug)
{
    if (!inputNode)
        return nil;

    AVAudioFormat* output_format = [inputNode outputFormatForBus:0];
    AVAudioFormat* input_format  = [inputNode inputFormatForBus:0];

    if (log_debug) {
        log_audio_format(@"inputFormatForBus(0)", input_format);
        log_audio_format(@"outputFormatForBus(0)", output_format);
    }

    if (is_valid_audio_format(output_format))
        return output_format;
    if (is_valid_audio_format(input_format))
        return input_format;
    return nil;
}

static void remove_input_tap_if_any(AVAudioInputNode* inputNode)
{
    if (!inputNode)
        return;
    @try {
        [inputNode removeTapOnBus:0];
    } @catch (NSException*) {
    }
}

static void fail_start(const std::shared_ptr<MacVoiceShared>& sh, const std::string& message)
{
    if (!sh)
        return;
    if (sh->on_error)
        sh->on_error(message);
    sh->listening.store(false);
    stop_session_locked(sh, /*cancel_task*/ true);
}

static void begin_session(const std::shared_ptr<MacVoiceShared>& sh, uint64_t gen);

// ObjC blocks cannot reliably retain std::shared_ptr; keep a plain heap context instead.
struct PermissionCtx {
    std::shared_ptr<MacVoiceShared> sh;
    uint64_t                      gen;
};

static void request_microphone(void (^handler)(BOOL granted))
{
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 140000
    if (@available(macOS 14.0, *)) {
        [AVAudioApplication requestRecordPermissionWithCompletionHandler:handler];
        return;
    }
#endif
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:handler];
}

static void on_mic_permission_main(BOOL granted, PermissionCtx* ctx)
{
    if (!ctx)
        return;

    if (!ctx->sh || !ctx->sh->is_start_valid(ctx->gen)) {
        delete ctx;
        return;
    }

    if (!granted) {
        fail_start(ctx->sh, "Microphone permission denied");
        delete ctx;
        return;
    }

    PermissionCtx* ctx_keep = ctx;
    [SFSpeechRecognizer requestAuthorization:^(SFSpeechRecognizerAuthorizationStatus status) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!ctx_keep || !ctx_keep->sh || !ctx_keep->sh->is_start_valid(ctx_keep->gen)) {
                delete ctx_keep;
                return;
            }
            if (status != SFSpeechRecognizerAuthorizationStatusAuthorized) {
                fail_start(ctx_keep->sh, "Speech recognition permission denied");
                delete ctx_keep;
                return;
            }
            begin_session(ctx_keep->sh, ctx_keep->gen);
            delete ctx_keep;
        });
    }];
}

static void request_permissions_and_start(const std::shared_ptr<MacVoiceShared>& sh, uint64_t gen)
{
    if (!sh || !sh->is_start_valid(gen))
        return;

    auto* ctx = new PermissionCtx{sh, gen};

    request_microphone(^(BOOL granted) {
        dispatch_async(dispatch_get_main_queue(), ^{
            on_mic_permission_main(granted, ctx);
        });
    });
}

static void begin_session(const std::shared_ptr<MacVoiceShared>& sh, uint64_t gen)
{
    if (!sh || !sh->is_start_valid(gen))
        return;

    stop_session_locked(sh, /*cancel_task*/ true);

    auto session       = std::make_shared<VoiceSession>();
    session->app_alive = std::make_shared<std::atomic<bool>>(sh->alive.load());
    session->on_final  = sh->on_final;
    session->on_error  = sh->on_error;
    sh->session        = session;

    NSError* err = nil;

    session->audioEngine = [[AVAudioEngine alloc] init];
    session->request     = [[SFSpeechAudioBufferRecognitionRequest alloc] init];
    session->request.shouldReportPartialResults = YES;
    if (@available(macOS 10.15, *))
        session->request.taskHint = SFSpeechRecognitionTaskHintDictation;

    NSLocale* locale = speech_locale();
    NSLog(@"OllamaVoice: app_lang=%s preferred=%@ recognizer locale=%@",
          Slic3r::GUI::wxGetApp().current_language_code().utf8_str().data(),
          [NSLocale preferredLanguages],
          locale.localeIdentifier);
    session->recognizer = [[SFSpeechRecognizer alloc] initWithLocale:locale];
    if (!session->recognizer) {
        fail_start(sh, "Speech recognizer unavailable for locale: " + ns_to_std(locale.localeIdentifier));
        return;
    }
    if (!session->recognizer.isAvailable) {
        std::string msg = "Speech recognizer not available for " + ns_to_std(locale.localeIdentifier);
        if ([locale.languageCode isEqualToString:@"ko"])
            msg += " — add Korean in System Settings → Keyboard → Dictation (or Siri language)";
        else
            msg += " — check Siri/dictation language or network";
        fail_start(sh, msg);
        return;
    }

    AVAudioInputNode* inputNode = session->audioEngine.inputNode;

    SFSpeechRecognizer* recognizer = session->recognizer;
    SFSpeechAudioBufferRecognitionRequest* request = session->request;
    std::weak_ptr<VoiceSession> weak_session       = session;
    std::weak_ptr<MacVoiceShared> weak_sh          = sh;

    session->task = [recognizer recognitionTaskWithRequest:request
        resultHandler:^(SFSpeechRecognitionResult* result, NSError* error) {
            dispatch_async(dispatch_get_main_queue(), ^{
                auto s2  = weak_session.lock();
                auto sh2 = weak_sh.lock();
                if (!is_current_session(sh2, s2))
                    return;

                const bool finishing = s2->awaiting_final.load(std::memory_order_acquire);
                if (s2->result_delivered.load(std::memory_order_acquire))
                    return;
                if (!s2->session_active.load(std::memory_order_acquire) && !finishing)
                    return;

                if (error) {
                    if (is_speech_cancel_error(error) || is_no_speech_detected_error(error)) {
                        if (finishing) {
                            if (!try_deliver_recognized_text(sh2, s2, s2->last_partial_text))
                                finish_session(sh2, s2, /*cancel_task*/ false);
                        } else if (is_no_speech_detected_error(error) && s2->on_error) {
                            s2->on_error("No speech detected. Speak while listening, then tap the mic again to finish.");
                        }
                        return;
                    }
                    const std::string msg = "Speech recognition error: " + nserr_to_string(error);
                    if (s2->on_error)
                        s2->on_error(msg);
                    finish_session(sh2, s2, /*cancel_task*/ false);
                    return;
                }

                if (!result)
                    return;

                NSString* s_text = result.bestTranscription.formattedString ?: @"";
                const std::string text = s_text ? std::string([s_text UTF8String]) : std::string();
                if (!text.empty())
                    s2->last_partial_text = text;

                if (result.isFinal || finishing) {
                    const std::string deliver = text.empty() ? s2->last_partial_text : text;
                    if (!try_deliver_recognized_text(sh2, s2, deliver) && finishing && result.isFinal)
                        finish_session(sh2, s2, /*cancel_task*/ false);
                }
            });
        }];

    if (!inputNode) {
        fail_start(sh, "No microphone input node");
        return;
    }

    // prepare() first so hardware formats are often valid before installTap (macOS AVAudioEngine).
    [session->audioEngine prepare];

    AVAudioFormat* tap_format    = input_node_recording_format(inputNode, /*log_debug*/ false);
    AVAudioFormat* speech_format = session->request.nativeAudioFormat;

    if (!tap_format) {
        fail_start(sh, "Microphone input is not ready (invalid audio format)");
        return;
    }

    if (speech_format && is_valid_audio_format(speech_format) && ![tap_format isEqual:speech_format]) {
        session->audioConverter = [[AVAudioConverter alloc] initFromFormat:tap_format toFormat:speech_format];
        if (!session->audioConverter) {
            fail_start(sh, "Failed to configure speech audio converter");
            return;
        }
        const double ratio = speech_format.sampleRate / tap_format.sampleRate;
        const AVAudioFrameCount out_capacity =
            (AVAudioFrameCount)(4096 * ratio) + 64;
        session->speechPCMBuffer =
            [[AVAudioPCMBuffer alloc] initWithPCMFormat:speech_format frameCapacity:out_capacity];
        if (!session->speechPCMBuffer) {
            fail_start(sh, "Failed to allocate speech audio buffer");
            return;
        }
    }

    remove_input_tap_if_any(inputNode);

    std::weak_ptr<VoiceSession> weak_session_tap = session;
    @try {
        [inputNode installTapOnBus:0 bufferSize:4096 format:tap_format block:^(AVAudioPCMBuffer* buffer, AVAudioTime* when) {
            (void)when;
            auto s = weak_session_tap.lock();
            if (!s || !s->capture_active.load(std::memory_order_acquire) || s->stopping.load())
                return;
            if (!s->append_captured_buffer(buffer))
                s->capture_active.store(false);
        }];
    } @catch (NSException* ex) {
        const std::string reason = ns_to_std(ex.reason);
        fail_start(sh, "Failed to attach microphone: " + (reason.empty() ? ns_to_std(ex.name) : reason));
        return;
    }

    if (![session->audioEngine startAndReturnError:&err] || err) {
        fail_start(sh, "Failed to start audio engine: " + nserr_to_string(err));
        return;
    }
}

static void stop_on_main_thread(const std::shared_ptr<MacVoiceShared>& sh, bool sync)
{
    if (!sh)
        return;

    auto* holder = new std::shared_ptr<MacVoiceShared>(sh);
    auto run = ^{
        std::unique_ptr<std::shared_ptr<MacVoiceShared>> owner(holder);
        const std::shared_ptr<MacVoiceShared>& local = *owner;
        local->generation.fetch_add(1, std::memory_order_release);
        local->listening.store(false, std::memory_order_release);
        if (local->session && local->session->session_active.load() && !local->session->stopping.load())
            graceful_stop_listening(local);
        else
            stop_session_locked(local, /*cancel_task*/ true);
    };

    if ([NSThread isMainThread]) {
        run();
    } else if (sync) {
        dispatch_sync(dispatch_get_main_queue(), run);
    } else {
        dispatch_async(dispatch_get_main_queue(), run);
    }
}

} // namespace

class OllamaVoiceInputMac final : public OllamaVoiceInput
{
public:
    OllamaVoiceInputMac()
        : m_shared(std::make_shared<MacVoiceShared>())
    {}

    ~OllamaVoiceInputMac() override
    {
        m_shared->alive.store(false, std::memory_order_release);
        set_on_final_text({});
        set_on_error({});
        stop_on_main_thread(m_shared, /*sync*/ true);
    }

    bool is_listening() const override { return m_shared->listening.load(std::memory_order_acquire); }

    void start() override
    {
        if (m_shared->listening.load(std::memory_order_acquire)
            || (m_shared->session && m_shared->session->awaiting_final.load(std::memory_order_acquire)))
            return;

        m_shared->on_final = final_callback();
        m_shared->on_error = error_callback();

        const uint64_t gen = m_shared->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
        m_shared->listening.store(true, std::memory_order_release);

        // Heap context: ObjC blocks must not capture std::shared_ptr.
        struct StartCtx {
            std::shared_ptr<MacVoiceShared> sh;
            uint64_t                      gen;
        };
        auto* ctx = new StartCtx{m_shared, gen};
        dispatch_async(dispatch_get_main_queue(), ^{
            std::unique_ptr<StartCtx> owner(ctx);
            if (!owner->sh || !owner->sh->is_start_valid(owner->gen))
                return;
            if (owner->sh->session)
                stop_session_locked(owner->sh, /*cancel_task*/ true);
            request_permissions_and_start(owner->sh, owner->gen);
        });
    }

    void stop() override { stop_on_main_thread(m_shared, /*sync*/ false); }

private:
    std::shared_ptr<MacVoiceShared> m_shared;
};

std::unique_ptr<OllamaVoiceInput> create_ollama_voice_input()
{
    return std::make_unique<OllamaVoiceInputMac>();
}

}} // namespace

#endif

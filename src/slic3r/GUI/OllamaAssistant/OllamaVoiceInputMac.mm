#ifdef __APPLE__

#include "OllamaVoiceInput.hpp"

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <Speech/Speech.h>

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

class OllamaVoiceInputMac final : public OllamaVoiceInput
{
public:
    OllamaVoiceInputMac()
        : m_alive(std::make_shared<std::atomic<bool>>(true))
    {}

    ~OllamaVoiceInputMac() override
    {
        m_alive->store(false);
        stop_on_main_thread(/*sync*/ true);
    }

    bool is_listening() const override { return m_listening; }

    void start() override
    {
        if (m_listening)
            return;

        m_listening = true;
        const auto alive = m_alive;

        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
            dispatch_async(dispatch_get_main_queue(), ^{
                if (!alive->load()) {
                    return;
                }
                if (!granted) {
                    emit_error("Microphone permission denied");
                    m_listening = false;
                    return;
                }

                [SFSpeechRecognizer requestAuthorization:^(SFSpeechRecognizerAuthorizationStatus status) {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        if (!alive->load()) {
                            return;
                        }
                        if (status != SFSpeechRecognizerAuthorizationStatusAuthorized) {
                            emit_error("Speech recognition permission denied");
                            m_listening = false;
                            return;
                        }
                        start_on_main_thread(alive);
                    });
                }];
            });
        }];
    }

    void stop() override { stop_on_main_thread(/*sync*/ false); }

private:
    void start_on_main_thread(const std::shared_ptr<std::atomic<bool>>& alive)
    {
        if (!alive->load() || !m_listening)
            return;

        NSError* err = nil;

        m_audioEngine = [[AVAudioEngine alloc] init];
        m_request = [[SFSpeechAudioBufferRecognitionRequest alloc] init];
        m_request.shouldReportPartialResults = NO;
        m_stopping = false;

        NSLocale* locale = [NSLocale currentLocale];
        NSString* lang = [locale objectForKey:NSLocaleLanguageCode];
        NSString* identifier = locale.localeIdentifier ?: @"en-US";
        if (lang && [lang isEqualToString:@"ko"])
            identifier = @"ko-KR";
        else if (lang && [lang isEqualToString:@"en"])
            identifier = @"en-US";

        m_recognizer = [[SFSpeechRecognizer alloc] initWithLocale:[NSLocale localeWithLocaleIdentifier:identifier]];
        if (!m_recognizer) {
            emit_error("Speech recognizer unavailable");
            stop_on_main_thread(false);
            return;
        }
        if (!m_recognizer.isAvailable) {
            emit_error("Speech recognizer not available (offline/network?)");
            stop_on_main_thread(false);
            return;
        }

        AVAudioInputNode* inputNode = m_audioEngine.inputNode;
        AVAudioFormat* format = [inputNode outputFormatForBus:0];
        [inputNode removeTapOnBus:0];
        SFSpeechAudioBufferRecognitionRequest* request = m_request;
        [inputNode installTapOnBus:0 bufferSize:1024 format:format block:^(AVAudioPCMBuffer* buffer, AVAudioTime* when) {
            (void)when;
            if (!alive->load() || !request)
                return;
            [request appendAudioPCMBuffer:buffer];
        }];

        [m_audioEngine prepare];
        if (![m_audioEngine startAndReturnError:&err] || err) {
            emit_error("Failed to start audio engine: " + nserr_to_string(err));
            stop_on_main_thread(false);
            return;
        }

        m_task = [m_recognizer recognitionTaskWithRequest:m_request
                                           resultHandler:^(SFSpeechRecognitionResult* result, NSError* error) {
            const auto alive_capture = alive;
            dispatch_async(dispatch_get_main_queue(), ^{
                if (!alive_capture->load())
                    return;
                if (error) {
                    if (m_stopping)
                        return;
                    emit_error("Speech recognition error: " + nserr_to_string(error));
                    stop_on_main_thread(false);
                    return;
                }
                if (result && result.isFinal) {
                    NSString* s = result.bestTranscription.formattedString ?: @"";
                    std::string text = s ? std::string([s UTF8String]) : std::string();
                    if (!text.empty())
                        emit_final(text);
                    stop_on_main_thread(false);
                }
            });
        }];
    }

    void stop_on_main_thread(bool sync)
    {
        auto run = ^{
            if (!m_listening && m_audioEngine == nil && m_task == nil)
                return;

            m_stopping = true;
            m_listening = false;

            if (m_audioEngine) {
                AVAudioInputNode* inputNode = m_audioEngine.inputNode;
                [inputNode removeTapOnBus:0];
                [m_audioEngine stop];
                m_audioEngine = nil;
            }
            if (m_request) {
                [m_request endAudio];
                m_request = nil;
            }
            if (m_task) {
                [m_task finish];
                m_task = nil;
            }
            m_recognizer = nil;
            m_stopping = false;
        };

        if ([NSThread isMainThread]) {
            run();
        } else if (sync) {
            dispatch_sync(dispatch_get_main_queue(), run);
        } else {
            dispatch_async(dispatch_get_main_queue(), run);
        }
    }

    std::shared_ptr<std::atomic<bool>> m_alive;
    bool m_listening{false};
    bool m_stopping{false};
    AVAudioEngine* m_audioEngine{nil};
    SFSpeechAudioBufferRecognitionRequest* m_request{nil};
    SFSpeechRecognitionTask* m_task{nil};
    SFSpeechRecognizer* m_recognizer{nil};
};

std::unique_ptr<OllamaVoiceInput> create_ollama_voice_input()
{
    return std::make_unique<OllamaVoiceInputMac>();
}

}} // namespace

#endif

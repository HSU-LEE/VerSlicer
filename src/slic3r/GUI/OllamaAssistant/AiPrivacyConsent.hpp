#ifndef slic3r_AiPrivacyConsent_hpp_
#define slic3r_AiPrivacyConsent_hpp_

class wxWindow;

namespace Slic3r { namespace GUI {

/** One-time notice of what the assistant sends to OpenAI. Returns true when the
 *  user has agreed (now or earlier). Bumping kAiPrivacyConsentVersion asks again. */
bool ensure_ai_privacy_consent(wxWindow* parent);

/** Thread-safe: has the user agreed? Any ChatGPT request checks this first. */
bool ai_privacy_consent_given();

/** Main thread, at startup: load the stored answer into the thread-safe cache. */
void load_ai_privacy_consent();

constexpr const char* kAiPrivacyConsentVersion = "1";

}} // namespace

#endif

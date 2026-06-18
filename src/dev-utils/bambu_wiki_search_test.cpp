#include "../slic3r/GUI/OllamaAssistant/BambuLabWikiSearchCore.hpp"
#include "../slic3r/GUI/OllamaAssistant/OllamaActionCritic.hpp"
#include "../slic3r/GUI/OllamaAssistant/OllamaRequestRouter.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace Slic3r::GUI;

static int g_failures = 0;

static void expect_true(bool cond, const char* label)
{
    if (!cond) {
        std::cerr << "FAIL: " << label << '\n';
        ++g_failures;
    }
}

int main()
{
    {
        const std::string html = R"(<html><head><title>T</title><script>x</script></head><body><p>Retraction length</p><p>Reduce nozzle temperature.</p></body></html>)";
        const std::string text = BambuLabWikiSearchCore::html_to_plain_text(html, 500);
        expect_true(text.find("Retraction") != std::string::npos, "html strip retraction");
        expect_true(text.find("<p>") == std::string::npos, "html tags removed");
    }

    {
        expect_true(BambuLabWikiSearchCore::normalize_search_query("실이 많이 나와", true).find("stringing") != std::string::npos,
                    "ko stringing query");
        expect_true(BambuLabWikiSearchCore::normalize_search_query("베드에 안 붙", true).find("adhesion") != std::string::npos,
                    "ko adhesion query");
    }

    {
        expect_true(OllamaRequestRouter::benefits_from_wiki("고쳐줘"), "wiki for vague");
        expect_true(OllamaRequestRouter::benefits_from_wiki("stringing bad"), "wiki for symptom");
        expect_true(!OllamaRequestRouter::benefits_from_wiki("rotate 90"), "no wiki for rotate");
    }

    {
        nlohmann::json root = {{"message", "ok"}, {"actions", nlohmann::json::array()}};
        const nlohmann::json wiki = nlohmann::json::array({
            {{"title", "Stringing"}, {"excerpt", "Increase retraction length and lower nozzle temperature."}},
        });
        const OllamaCriticResult critic = OllamaActionCritic::review(root, "lots of stringing", wiki);
        expect_true(critic.verdict == OllamaCriticVerdict::Revise, "critic revise on empty actions");
        expect_true(!critic.suggested_keys.empty(), "critic suggests keys");
    }

    if (getenv("OLLAMA_WIKI_E2E") && std::string(getenv("OLLAMA_WIKI_E2E")) == "1") {
        std::cout << "OLLAMA_WIKI_E2E=1 skipped in offline test (run from app with network)\n";
    }

    if (g_failures == 0) {
        std::cout << "bambu_wiki_search_test: all passed\n";
        return EXIT_SUCCESS;
    }
    std::cerr << "bambu_wiki_search_test: " << g_failures << " failure(s)\n";
    return EXIT_FAILURE;
}

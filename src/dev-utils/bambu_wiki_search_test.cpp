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
        expect_true(BambuLabWikiSearchCore::normalize_search_query("lots of stringing", true).find("stringing") != std::string::npos,
                    "query kept as written");
        expect_true(BambuLabWikiSearchCore::normalize_search_query("  bed adhesion  ", true) == "bed adhesion",
                    "query trimmed");
    }

    {
        expect_true(!OllamaRequestRouter::benefits_from_wiki("fix it"), "wiki is a tool, not a prefetch");
        expect_true(!OllamaRequestRouter::benefits_from_wiki("rotate 90"), "wiki is a tool, not a prefetch");
    }

    {
        nlohmann::json root = {{"message", "ok"}, {"actions", nlohmann::json::array()}};
        const nlohmann::json wiki = nlohmann::json::array({
            {{"title", "Stringing"}, {"excerpt", "Increase retraction length and lower nozzle temperature."}},
        });
        const OllamaCriticResult critic = OllamaActionCritic::review(root, "lots of stringing", wiki);
        expect_true(critic.verdict == OllamaCriticVerdict::Approve, "empty actions are not a keyword revise");
        expect_true(critic.suggested_keys.empty(), "critic does not invent keys");
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

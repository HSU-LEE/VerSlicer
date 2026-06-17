#include "OllamaActionJsonExtract.hpp"

#include <boost/algorithm/string.hpp>

#include <stdexcept>

namespace Slic3r { namespace GUI {

namespace {

std::string sanitize_invalid_json_escapes(std::string text)
{
    std::string out;
    out.reserve(text.size());
    bool in_str = false;
    bool esc    = false;
    for (char c : text) {
        if (esc) {
            if (in_str && c != '"' && c != '\\' && c != '/' && c != 'b' && c != 'f' && c != 'n' && c != 'r'
                && c != 't' && c != 'u') {
                if (!out.empty() && out.back() == '\\')
                    out.pop_back();
                out += c;
            } else {
                out += c;
            }
            esc = false;
            continue;
        }
        if (c == '\\' && in_str) {
            esc = true;
            out += c;
            continue;
        }
        if (c == '"')
            in_str = !in_str;
        out += c;
    }
    return out;
}

} // namespace

nlohmann::json extract_ollama_action_json(const std::string& assistant_text)
{
    std::string text = assistant_text;
    boost::trim(text);
    if (text.empty())
        throw std::runtime_error("Empty assistant response");

    const std::string fence = "```json";
    const auto pos          = text.find(fence);
    if (pos != std::string::npos) {
        const auto start = pos + fence.size();
        const auto end   = text.find("```", start);
        if (end != std::string::npos)
            text = text.substr(start, end - start);
    } else {
        const auto p2 = text.find("```");
        if (p2 != std::string::npos) {
            const auto start = text.find('\n', p2);
            const auto end   = text.find("```", start == std::string::npos ? p2 + 3 : start);
            if (start != std::string::npos && end != std::string::npos)
                text = text.substr(start + 1, end - start - 1);
        }
    }
    boost::trim(text);
    if (text.empty())
        throw std::runtime_error("Empty assistant response");

    text = sanitize_invalid_json_escapes(std::move(text));

    const auto brace = text.find('{');
    if (brace == std::string::npos) {
        if (text.empty())
            throw std::runtime_error("Empty assistant response");
        return nlohmann::json::parse(text);
    }

    bool in_str = false;
    bool esc    = false;
    int  depth  = 0;
    size_t start = brace;
    size_t end   = std::string::npos;
    for (size_t i = start; i < text.size(); ++i) {
        const char c = text[i];
        if (esc) { esc = false; continue; }
        if (c == '\\' && in_str) { esc = true; continue; }
        if (c == '"') { in_str = !in_str; continue; }
        if (in_str) continue;
        if (c == '{') depth++;
        else if (c == '}') {
            depth--;
            if (depth < 0)
                break;
            if (depth == 0) { end = i + 1; break; }
        }
    }
    if (end != std::string::npos && depth == 0)
        return nlohmann::json::parse(text.substr(start, end - start));

    throw std::runtime_error("No balanced JSON object found in assistant response");
}

}} // namespace

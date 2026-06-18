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

std::string repair_ollama_json_text(std::string text)
{
    boost::trim(text);
    if (text.empty())
        return text;

    for (int pass = 0; pass < 4; ++pass) {
        std::string next;
        next.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (c == ',' && i + 1 < text.size()) {
                size_t j = i + 1;
                while (j < text.size() && (text[j] == ' ' || text[j] == '\n' || text[j] == '\r' || text[j] == '\t'))
                    ++j;
                if (j < text.size() && (text[j] == '}' || text[j] == ']'))
                    continue;
            }
            next += c;
        }
        text.swap(next);
    }

    const auto brace = text.find('{');
    if (brace == std::string::npos)
        return text;

    bool in_str = false;
    bool esc    = false;
    int  depth  = 0;
    for (size_t i = brace; i < text.size(); ++i) {
        const char c = text[i];
        if (esc) {
            esc = false;
            continue;
        }
        if (c == '\\' && in_str) {
            esc = true;
            continue;
        }
        if (c == '"')
            in_str = !in_str;
        if (in_str)
            continue;
        if (c == '{')
            ++depth;
        else if (c == '}' && depth > 0)
            --depth;
    }
    while (depth > 0) {
        text += '}';
        --depth;
    }
    return text;
}

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
    if (end != std::string::npos && depth == 0) {
        const std::string slice = text.substr(start, end - start);
        try {
            return nlohmann::json::parse(slice);
        } catch (...) {
            return nlohmann::json::parse(repair_ollama_json_text(slice));
        }
    }

    throw std::runtime_error("No balanced JSON object found in assistant response");
}

nlohmann::json extract_ollama_action_json_with_repair(const std::string& assistant_text)
{
    try {
        return extract_ollama_action_json(assistant_text);
    } catch (const std::exception&) {
        std::string text = assistant_text;
        boost::trim(text);
        const std::string fence = "```json";
        const auto        pos   = text.find(fence);
        if (pos != std::string::npos) {
            const auto start = pos + fence.size();
            const auto end   = text.find("```", start);
            if (end != std::string::npos)
                text = text.substr(start, end - start);
        }
        boost::trim(text);
        const auto brace = text.find('{');
        if (brace != std::string::npos)
            text = repair_ollama_json_text(text.substr(brace));
        return nlohmann::json::parse(text);
    }
}

}} // namespace

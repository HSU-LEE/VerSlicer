#include "OllamaActionJsonExtract.hpp"

#include <boost/algorithm/string.hpp>

#include <cctype>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace Slic3r { namespace GUI {

namespace {

static bool is_hex_digit(char c)
{
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

static std::string utf8_from_codepoint(uint32_t cp)
{
    std::string s;
    if (cp <= 0x7F) {
        s.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        s.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        s.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        s.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return s;
}

static std::string json_string_char_for_codepoint(uint32_t cp)
{
    if (cp == '"')
        return "\\\"";
    if (cp == '\\')
        return "\\\\";
    if (cp < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\u%04X", cp);
        return buf;
    }
    return utf8_from_codepoint(cp);
}

/** Llama and other models emit ES6-style \\u{XXXX}; JSON requires \\uXXXX or UTF-8. */
std::string repair_json_es6_unicode_escapes(std::string text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (i + 3 < text.size() && text[i] == '\\' && text[i + 1] == 'u' && text[i + 2] == '{') {
            size_t  j  = i + 3;
            uint32_t cp = 0;
            while (j < text.size() && is_hex_digit(text[j])) {
                cp = cp * 16 + (std::isdigit(static_cast<unsigned char>(text[j]))
                                    ? static_cast<unsigned>(text[j] - '0')
                                    : static_cast<unsigned>(std::tolower(static_cast<unsigned char>(text[j])) - 'a' + 10));
                ++j;
            }
            if (j < text.size() && text[j] == '}' && j > i + 3 && cp <= 0x10FFFF) {
                out += json_string_char_for_codepoint(cp);
                i = j;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

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

static const char* kSalvageConfigKeys[] = {
    "wall_loops", "sparse_infill_density", "sparse_infill_pattern", "brim_width", "brim_type",
    "enable_support", "enable_brim", "layer_height", "initial_layer_print_height", "outer_wall_speed",
    "sparse_infill_speed", "retraction_length", "retraction_speed", "nozzle_temperature",
    "bed_temperature", "first_layer_bed_temperature", "top_shell_layers", "bottom_shell_layers",
    "support_type", "elefant_foot_compensation", "ironing_type",
};

static bool is_config_key_char(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

static nlohmann::json parse_salvaged_scalar(std::string val)
{
    boost::trim(val);
    while (!val.empty() && (val.back() == ',' || val.back() == ';'))
        val.pop_back();
    boost::trim(val);
    if (val.empty())
        return nlohmann::json();
    if (val == "true" || val == "on" || val == "yes")
        return true;
    if (val == "false" || val == "off" || val == "no")
        return false;
    bool is_num = !val.empty();
    for (size_t i = 0; i < val.size() && is_num; ++i) {
        const char c = val[i];
        if (i == 0 && (c == '+' || c == '-'))
            continue;
        if (c == '.' && val.find('.') == static_cast<int>(i))
            continue;
        if (!std::isdigit(static_cast<unsigned char>(c)))
            is_num = false;
    }
    if (is_num) {
        if (val.find('.') != std::string::npos)
            return std::stod(val);
        return std::stoi(val);
    }
    return val;
}

static nlohmann::json salvage_options_from_text(const std::string& text)
{
    nlohmann::json options = nlohmann::json::object();

    for (const char* key : kSalvageConfigKeys) {
        if (options.contains(key))
            continue;
        const std::string needle = key;
        size_t            pos    = 0;
        while ((pos = text.find(needle, pos)) != std::string::npos) {
            if (pos > 0 && is_config_key_char(text[pos - 1])) {
                ++pos;
                continue;
            }
            size_t i = pos + needle.size();
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
                ++i;
            if (i >= text.size() || (text[i] != ':' && text[i] != '=')) {
                ++pos;
                continue;
            }
            ++i;
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
                ++i;
            if (i < text.size() && text[i] == '"') {
                ++i;
                const size_t q = text.find('"', i);
                if (q == std::string::npos)
                    break;
                options[key] = text.substr(i, q - i);
                break;
            }
            size_t j = i;
            while (j < text.size() && text[j] != ',' && text[j] != '\n' && text[j] != '\r' && text[j] != '}'
                   && text[j] != ']')
                ++j;
            const nlohmann::json val = parse_salvaged_scalar(text.substr(i, j - i));
            if (!val.is_null())
                options[key] = val;
            break;
        }
    }

    // "wall_loops": 3 style fragments
    for (const char* key : kSalvageConfigKeys) {
        if (options.contains(key))
            continue;
        const std::string quoted = std::string("\"") + key + "\"";
        const size_t      pos      = text.find(quoted);
        if (pos == std::string::npos)
            continue;
        size_t i = pos + quoted.size();
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        if (i >= text.size() || text[i] != ':')
            continue;
        ++i;
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        size_t j = i;
        while (j < text.size() && text[j] != ',' && text[j] != '\n' && text[j] != '\r' && text[j] != '}'
               && text[j] != ']')
            ++j;
        const nlohmann::json val = parse_salvaged_scalar(text.substr(i, j - i));
        if (!val.is_null())
            options[key] = val;
    }

    return options;
}

static std::string salvage_message_from_text(const std::string& text)
{
    std::ostringstream prose;
    bool               in_json = false;
    int                depth   = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '{') {
            in_json = true;
            ++depth;
            continue;
        }
        if (c == '}') {
            if (depth > 0)
                --depth;
            if (depth == 0)
                in_json = false;
            continue;
        }
        if (in_json)
            continue;
        if (c == '\n' && prose.str().empty())
            continue;
        if (!prose.str().empty() || !std::isspace(static_cast<unsigned char>(c))) {
            if (c == '\n' && !prose.str().empty())
                break;
            prose << c;
        }
    }
    std::string msg = prose.str();
    boost::trim(msg);
    if (msg.size() > 240)
        msg = msg.substr(0, 240) + "…";
    return msg;
}

static bool is_useful_action_root(const nlohmann::json& root)
{
    if (!root.is_object())
        return false;
    if (root.contains("actions") && root["actions"].is_array() && !root["actions"].empty())
        return true;
    return false;
}

static nlohmann::json parse_json_slice(const std::string& slice)
{
    try {
        return nlohmann::json::parse(slice);
    } catch (...) {
        return nlohmann::json::parse(repair_ollama_json_text(slice));
    }
}

static nlohmann::json try_parse_any_json_object(std::string text)
{
    text = repair_json_es6_unicode_escapes(std::move(text));
    text = sanitize_invalid_json_escapes(std::move(text));

    size_t search_from = 0;
    while (search_from < text.size()) {
        const size_t brace = text.find('{', search_from);
        if (brace == std::string::npos)
            break;

        bool in_str = false;
        bool esc    = false;
        int  depth  = 0;
        size_t end  = std::string::npos;
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
            else if (c == '}') {
                --depth;
                if (depth == 0) {
                    end = i + 1;
                    break;
                }
            }
        }

        if (end == std::string::npos)
            break;

        try {
            nlohmann::json root = parse_json_slice(text.substr(brace, end - brace));
            if (is_useful_action_root(root))
                return root;
            if (root.is_object() && root.contains("message") && root["message"].is_string()
                && !root["message"].get<std::string>().empty() && is_useful_action_root(root))
                return root;
        } catch (...) {
        }

        search_from = end;
    }

    return nlohmann::json();
}

} // namespace

nlohmann::json try_salvage_ollama_action_json(const std::string& assistant_text)
{
    const nlohmann::json options = salvage_options_from_text(assistant_text);
    if (options.empty())
        return nlohmann::json();

    nlohmann::json root = nlohmann::json::object();
    std::string    msg  = salvage_message_from_text(assistant_text);
    if (msg.empty())
        msg = "Applying recovered settings from the assistant reply.";
    root["message"] = msg;
    root["actions"] = nlohmann::json::array({
        nlohmann::json{{"type", "set_config"}, {"preset", "print"}, {"options", options}},
    });
    return root;
}

std::string repair_ollama_json_text(std::string text)
{
    text = repair_json_es6_unicode_escapes(std::move(text));
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

    if (auto root = try_parse_any_json_object(text); !root.is_null())
        return root;

    if (auto salvaged = try_salvage_ollama_action_json(assistant_text); !salvaged.empty())
        return salvaged;

    throw std::runtime_error("No balanced JSON object found in assistant response");
}

nlohmann::json extract_ollama_action_json_with_repair(const std::string& assistant_text)
{
    try {
        return extract_ollama_action_json(assistant_text);
    } catch (const std::exception&) {
        if (auto salvaged = try_salvage_ollama_action_json(assistant_text); !salvaged.empty())
            return salvaged;

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
        text = repair_json_es6_unicode_escapes(std::move(text));
        const auto brace = text.find('{');
        if (brace != std::string::npos)
            text = repair_ollama_json_text(text.substr(brace));
        text = sanitize_invalid_json_escapes(std::move(text));
        try {
            nlohmann::json root = nlohmann::json::parse(text);
            if (root.is_object())
                return root;
        } catch (const std::exception&) {
        }

        if (auto salvaged = try_salvage_ollama_action_json(assistant_text); !salvaged.empty())
            return salvaged;
        throw;
    }
}

}} // namespace

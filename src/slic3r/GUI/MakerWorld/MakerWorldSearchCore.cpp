#include "MakerWorldSearchCore.hpp"

#include <boost/log/trivial.hpp>

#include <boost/algorithm/string.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>

namespace Slic3r { namespace GUI {

namespace {

std::string safe_string(const nlohmann::json& j, const char* key)
{
    if (!j.contains(key))
        return {};
    if (j[key].is_string())
        return j[key].get<std::string>();
    if (j[key].is_number_integer())
        return std::to_string(j[key].get<long long>());
    if (j[key].is_number_unsigned())
        return std::to_string(j[key].get<unsigned long long>());
    return {};
}

std::string pick_filename(const std::string& title, const std::string& fallback)
{
    std::string base = title.empty() ? fallback : title;
    for (char& c : base) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
    }
    boost::algorithm::trim(base);
    if (base.empty())
        base = "model";
    if (base.size() > 80)
        base = base.substr(0, 80);
    if (base.find(".3mf") == std::string::npos)
        base += ".3mf";
    return base;
}

bool is_cjk_codepoint(uint32_t cp)
{
    return (cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF)
        || (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0x1100 && cp <= 0x11FF);
}

bool utf8_decode_next(const std::string& s, size_t& i, uint32_t& cp)
{
    if (i >= s.size())
        return false;
    const unsigned char c0 = static_cast<unsigned char>(s[i]);
    if (c0 < 0x80) {
        cp = c0;
        ++i;
        return true;
    }
    if ((c0 & 0xE0) == 0xC0 && i + 1 < s.size()) {
        cp = ((c0 & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
        i += 2;
        return true;
    }
    if ((c0 & 0xF0) == 0xE0 && i + 2 < s.size()) {
        cp = ((c0 & 0x0F) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6)
            | (static_cast<unsigned char>(s[i + 2]) & 0x3F);
        i += 3;
        return true;
    }
    if ((c0 & 0xF8) == 0xF0 && i + 3 < s.size()) {
        cp = ((c0 & 0x07) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12)
            | ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3F);
        i += 4;
        return true;
    }
    cp = c0;
    ++i;
    return true;
}

bool is_zero_width(uint32_t cp)
{
    return cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0xFEFF;
}

void utf8_append(uint32_t cp, std::string& out)
{
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string append_tags_description(const nlohmann::json& d, const std::string& base)
{
    std::string out = base;
    if (d.contains("tags") && d["tags"].is_array()) {
        for (const auto& t : d["tags"]) {
            if (t.is_string())
                out += " " + t.get<std::string>();
        }
    }
    const std::string desc = safe_string(d, "description");
    if (!desc.empty())
        out += " " + desc;
    const std::string summary = safe_string(d, "summary");
    if (!summary.empty())
        out += " " + summary;
    return out;
}

void parse_one_hit(const nlohmann::json& hit, std::vector<MakerWorldCandidate>& out)
{
    nlohmann::json d = hit;
    if (hit.contains("design") && hit["design"].is_object())
        d = hit["design"];

    MakerWorldCandidate c;
    c.design_id = safe_string(d, "id");
    if (c.design_id.empty())
        c.design_id = safe_string(d, "design_id");
    if (c.design_id.empty())
        c.design_id = safe_string(d, "designId");
    c.model_id = safe_string(d, "modelId");
    if (c.model_id.empty())
        c.model_id = safe_string(d, "model_id");
    c.profile_id = safe_string(d, "profileId");
    if (c.profile_id.empty())
        c.profile_id = safe_string(hit, "profileId");
    c.title = safe_string(d, "title");
    if (c.title.empty())
        c.title = safe_string(d, "designTitle");
    c.author = safe_string(d, "author");
    if (c.author.empty() && d.contains("designCreator") && d["designCreator"].is_object())
        c.author = safe_string(d["designCreator"], "name");
    c.cover_url = safe_string(d, "cover");
    if (c.cover_url.empty())
        c.cover_url = safe_string(d, "cover_url");
    if (c.cover_url.empty())
        c.cover_url = safe_string(d, "coverUrl");
    c.license      = safe_string(d, "license");
    c.download_url = safe_string(d, "download_url");
    if (c.download_url.empty())
        c.download_url = safe_string(hit, "download_url");
    c.filename = pick_filename(c.title, "model_" + c.design_id);
    if (d.contains("downloadCount") && d["downloadCount"].is_number())
        c.download_count = d["downloadCount"].get<int>();
    else if (d.contains("download_count") && d["download_count"].is_number())
        c.download_count = d["download_count"].get<int>();
    if (hit.contains("login_required") && hit["login_required"].is_boolean())
        c.login_required = hit["login_required"].get<bool>();
    if (!c.login_required && d.contains("login_required") && d["login_required"].is_boolean())
        c.login_required = d["login_required"].get<bool>();

    if (!c.design_id.empty()) {
        if (c.title.empty())
            c.title = "model_" + c.design_id;
        // tags/description stored in author field suffix for staffpick scoring haystack
        const std::string extra = append_tags_description(d, "");
        if (!extra.empty())
            c.author += extra;
        out.push_back(std::move(c));
    }
}

bool token_min_length_ok(const std::string& tok)
{
    if (tok.empty())
        return false;
    size_t i = 0;
    uint32_t cp = 0;
    if (!utf8_decode_next(tok, i, cp))
        return false;
    if (i == tok.size() && is_cjk_codepoint(cp))
        return true;
    return tok.size() >= 2;
}

bool is_ascii_word_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool haystack_contains_token(const std::string& hay_lower, const std::string& tok)
{
    if (tok.empty())
        return false;
    size_t pos = 0;
    while ((pos = hay_lower.find(tok, pos)) != std::string::npos) {
        const bool left_ok  = pos == 0 || !is_ascii_word_char(hay_lower[pos - 1]);
        const bool right_ok = pos + tok.size() >= hay_lower.size()
            || !is_ascii_word_char(hay_lower[pos + tok.size()]);
        if (left_ok && right_ok)
            return true;
        ++pos;
    }
    return false;
}

bool is_search_stopword(const std::string& tok)
{
    static const char* kStop[] = {
        "a", "an", "the", "me", "my", "for", "of", "on", "in", "to", "and", "or", "with",
        "please", "some", "any", "good", "nice", "cool", "best",
    };
    for (const char* s : kStop) {
        if (tok == s)
            return true;
    }
    return false;
}

void strip_ascii_word_insensitive(std::string& text, const char* word)
{
    if (!word || !*word)
        return;
    std::string lower = text;
    boost::algorithm::to_lower(lower);
    const std::string needle = word;
    std::string       needle_lower = needle;
    boost::algorithm::to_lower(needle_lower);
    for (size_t pos = 0; (pos = lower.find(needle_lower, pos)) != std::string::npos;) {
        const bool left_ok  = pos == 0 || !is_ascii_word_char(lower[pos - 1]);
        const bool right_ok = pos + needle.size() >= lower.size()
            || !is_ascii_word_char(lower[pos + needle.size()]);
        if (left_ok && right_ok) {
            text.replace(pos, needle.size(), " ");
            lower.replace(pos, needle.size(), std::string(needle.size(), ' '));
        } else {
            pos += needle.size();
        }
    }
}

std::string extract_quoted_phrase(const std::string& text)
{
    for (size_t i = 0; i + 1 < text.size(); ++i) {
        const char q = text[i];
        if (q != '"' && q != '\'' && static_cast<unsigned char>(q) != 0xE2)
            continue;
        size_t start = i + 1;
        size_t end   = std::string::npos;
        if (static_cast<unsigned char>(q) == 0xE2 && i + 2 < text.size()) {
            // UTF-8 “ or ”
            const unsigned char c1 = static_cast<unsigned char>(text[i + 1]);
            const unsigned char c2 = static_cast<unsigned char>(text[i + 2]);
            if (c1 == 0x80 && (c2 == 0x9C || c2 == 0x9D)) {
                start = i + 3;
                end   = text.find(text.substr(i, 3), start);
            }
        } else {
            end = text.find(q, start);
        }
        if (end != std::string::npos && end > start) {
            const std::string quoted = text.substr(start, end - start);
            if (!quoted.empty())
                return quoted;
        }
    }
    return {};
}

} // namespace

std::string normalize_makerworld_search_query(const std::string& user_text)
{
    const std::string quoted = extract_quoted_phrase(user_text);
    std::string       q      = quoted.empty() ? user_text : quoted;

    static const char* kStripKo[] = {
        "메이커월드", "메이커 월드", "뱀부랩", "뱀부 랩", "뱀부",
        "찾아주세요", "찾아 줘", "찾아줘", "찾아", "검색해줘", "검색 해줘", "검색해", "검색",
        "뽑아줘", "뽑아 줘", "뽑고", "뽑아", "뽑", "골라줘", "골라 줘", "골라", "추천해줘", "추천",
        "가져와줘", "가져와", "불러와줘", "불러와", "다운로드해줘", "다운로드",
        "해주세요", "해 주세요", "해줘", "해 줘", "주세요", "좀", "제발",
        "하고 싶어", "하고싶어", "싶어", "싶은", "보고 싶", "보고싶",
        "모델", "디자인", "작품", "프린트", "출력물", "출력", "인쇄", "만들어",
    };
    static const char* kStripEn[] = {
        "makerworld", "bambu lab", "bambu", "search", "find", "look for", "get me", "show me",
        "pick", "choose", "recommend", "suggest", "download", "import", "open", "please",
        "model", "design", "print", "stl", "3mf",
    };

    boost::algorithm::to_lower(q);
    for (const char* s : kStripKo)
        boost::algorithm::replace_all(q, s, " ");
    for (const char* s : kStripEn)
        strip_ascii_word_insensitive(q, s);

    q = sanitize_search_text(q);
    if (!q.empty())
        return q;

    // Fallback: strip only platform names from original text.
    std::string fallback = user_text;
    boost::algorithm::to_lower(fallback);
    boost::algorithm::replace_all(fallback, "makerworld", " ");
    boost::algorithm::replace_all(fallback, "메이커월드", " ");
    boost::algorithm::replace_all(fallback, "메이커 월드", " ");
    return sanitize_search_text(fallback);
}

std::string sanitize_search_text(const std::string& query)
{
    std::string out;
    out.reserve(query.size());
    for (size_t i = 0; i < query.size();) {
        uint32_t cp = 0;
        if (!utf8_decode_next(query, i, cp))
            break;
        if (!is_zero_width(cp))
            utf8_append(cp, out);
    }
    boost::algorithm::trim(out);
    while (out.find("  ") != std::string::npos)
        boost::algorithm::replace_all(out, "  ", " ");
    return out;
}

std::vector<MakerWorldCandidate> parse_hits_json(const std::string& body)
{
    std::vector<MakerWorldCandidate> out;
    if (body.empty() || body.front() != '{')
        return out;
    try {
        nlohmann::json root = nlohmann::json::parse(body);
        const nlohmann::json* hits = nullptr;
        if (root.contains("hits") && root["hits"].is_array())
            hits = &root["hits"];
        else if (root.contains("data") && root["data"].is_object() && root["data"].contains("hits")
                 && root["data"]["hits"].is_array())
            hits = &root["data"]["hits"];
        else if (root.contains("results") && root["results"].is_array())
            hits = &root["results"];
        else if (root.contains("data") && root["data"].is_object() && root["data"].contains("list")
                 && root["data"]["list"].is_array())
            hits = &root["data"]["list"];
        if (!hits)
            return out;
        for (const auto& h : *hits)
            if (h.is_object())
                parse_one_hit(h, out);
    } catch (...) {
        BOOST_LOG_TRIVIAL(warning) << "[MakerWorld] search hits JSON parse failed; returning no candidates";
    }
    return out;
}

std::vector<std::string> tokenize_query(const std::string& query)
{
    std::string q = query;
    boost::algorithm::to_lower(q);
    for (char& c : q) {
        if (c == '-' || c == '/' || c == '_')
            c = ' ';
    }
    std::vector<std::string> tokens;
    boost::split(tokens, q, boost::is_any_of(" \t,"), boost::token_compress_on);
    tokens.erase(std::remove_if(tokens.begin(), tokens.end(),
                                [](const std::string& t) {
                                    return !token_min_length_ok(t) || is_search_stopword(t);
                                }),
                 tokens.end());
    return tokens;
}

int score_candidate(const MakerWorldCandidate& c, const std::vector<std::string>& tokens,
                    const std::string& phrase_lower, bool deprioritize_login_required)
{
    std::string title_lower = c.title;
    boost::algorithm::to_lower(title_lower);
    std::string author_lower = c.author;
    boost::algorithm::to_lower(author_lower);
    const std::string hay = title_lower + " " + author_lower;

    int score = 0;
    if (!phrase_lower.empty() && title_lower.find(phrase_lower) != std::string::npos)
        score += 1500;
    else if (!phrase_lower.empty() && hay.find(phrase_lower) != std::string::npos)
        score += 900;

    int title_matched = 0;
    int hay_matched   = 0;
    for (const auto& tok : tokens) {
        if (haystack_contains_token(title_lower, tok))
            ++title_matched;
        if (haystack_contains_token(hay, tok))
            ++hay_matched;
    }
    if (!tokens.empty()) {
        score += title_matched * 180;
        score += hay_matched * 80;
        if (title_matched == static_cast<int>(tokens.size()))
            score += 350;
        else if (hay_matched == static_cast<int>(tokens.size()))
            score += 150;
    }

    if (!tokens.empty() && title_lower.find(tokens.front()) == 0)
        score += 80;

    if (tokens.size() >= 2) {
        const std::string bigram = tokens[0] + " " + tokens[1];
        if (title_lower.find(bigram) != std::string::npos)
            score += 120;
    }

    score += std::min(c.download_count / 1000, 120);

    if (deprioritize_login_required && c.login_required)
        score -= 500;

    return score;
}

std::vector<MakerWorldCandidate> merge_candidates(std::vector<MakerWorldCandidate> base,
                                                  const std::vector<MakerWorldCandidate>& extra)
{
    auto seen = [&](const std::string& id) {
        return std::any_of(base.begin(), base.end(),
                           [&](const MakerWorldCandidate& c) { return c.design_id == id; });
    };
    for (const auto& c : extra) {
        if (c.design_id.empty() || seen(c.design_id))
            continue;
        base.push_back(c);
    }
    return base;
}

std::vector<MakerWorldCandidate> rank_and_dedupe(std::vector<MakerWorldCandidate> candidates, const std::string& query,
                                                 int limit, bool deprioritize_login_required)
{
    if (candidates.empty() || limit <= 0)
        return {};

    std::string phrase = query;
    boost::algorithm::to_lower(phrase);
    const auto tokens = tokenize_query(query);

    std::vector<std::pair<int, MakerWorldCandidate>> scored;
    scored.reserve(candidates.size());
    std::vector<std::string> seen_ids;
    for (auto& c : candidates) {
        if (c.design_id.empty())
            continue;
        if (std::find(seen_ids.begin(), seen_ids.end(), c.design_id) != seen_ids.end())
            continue;
        seen_ids.push_back(c.design_id);
        scored.emplace_back(score_candidate(c, tokens, phrase, deprioritize_login_required), std::move(c));
    }

    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first)
            return a.first > b.first;
        return a.second.download_count > b.second.download_count;
    });

    std::vector<MakerWorldCandidate> out;
    for (size_t i = 0; i < scored.size() && static_cast<int>(out.size()) < limit; ++i)
        out.push_back(std::move(scored[i].second));
    return out;
}

std::vector<MakerWorldCandidate> filter_by_query(const std::vector<MakerWorldCandidate>& in, const std::string& query)
{
    return filter_by_query_scored(in, query, static_cast<int>(in.size()));
}

std::vector<MakerWorldCandidate> filter_by_query_scored(const std::vector<MakerWorldCandidate>& in,
                                                        const std::string& query, int limit)
{
    if (query.empty())
        return in;
    const auto tokens = tokenize_query(query);
    if (tokens.empty())
        return in;

    // AND match first (title preferred, then title+tags)
    std::vector<MakerWorldCandidate> and_matches;
    for (const auto& c : in) {
        std::string title_lower = c.title;
        boost::algorithm::to_lower(title_lower);
        std::string author_lower = c.author;
        boost::algorithm::to_lower(author_lower);
        const std::string hay = title_lower + " " + author_lower;
        bool              match = true;
        for (const auto& tok : tokens) {
            if (!haystack_contains_token(hay, tok)) {
                match = false;
                break;
            }
        }
        if (match)
            and_matches.push_back(c);
    }

    if (!and_matches.empty())
        return rank_and_dedupe(std::move(and_matches), query, limit, true);

    // OR fallback: require a minimum token hit ratio for multi-word queries
    const int min_hits = tokens.size() <= 1 ? 1 : static_cast<int>((tokens.size() + 1) / 2);
    std::vector<MakerWorldCandidate> or_matches;
    for (const auto& c : in) {
        std::string title_lower = c.title;
        boost::algorithm::to_lower(title_lower);
        std::string author_lower = c.author;
        boost::algorithm::to_lower(author_lower);
        const std::string hay = title_lower + " " + author_lower;
        int               hits = 0;
        for (const auto& tok : tokens) {
            if (haystack_contains_token(hay, tok))
                ++hits;
        }
        if (hits >= min_hits)
            or_matches.push_back(c);
    }
    return rank_and_dedupe(std::move(or_matches), query, limit, true);
}

std::vector<std::string> search_query_variants(const std::string& query, const std::string& translated_english)
{
    std::vector<std::string> variants;
    auto push_unique = [&](const std::string& s) {
        const std::string cleaned = sanitize_search_text(s);
        if (cleaned.empty())
            return;
        if (std::find(variants.begin(), variants.end(), cleaned) == variants.end())
            variants.push_back(cleaned);
    };
    push_unique(query);

    if (!translated_english.empty()) {
        push_unique(translated_english);
        std::string lower = query;
        boost::algorithm::to_lower(lower);
        std::string en_lower = translated_english;
        boost::algorithm::to_lower(en_lower);
        if (lower != en_lower)
            push_unique(lower + " " + en_lower);
    }

    std::string lower = query;
    boost::algorithm::to_lower(lower);

    // Heuristic: insert spaces before common English subwords in glued Korean+Latin queries
    if (lower.find(' ') == std::string::npos && lower.size() > 4) {
        static const char* kSplitHints[] = {"dragon", "keycap", "vase", "robot", "miniature", "articulated",
                                              "gundam", "lithophane", "christmas", "dinosaur"};
        for (const char* hint : kSplitHints) {
            const auto pos = lower.find(hint);
            if (pos != std::string::npos && pos > 0) {
                push_unique(lower.substr(0, pos) + " " + lower.substr(pos));
                break;
            }
        }
    }

    // Narrower variants: drop common size/style qualifiers for broader recall
    static const char* kOptionalQualifiers[] = {
        "mini", "small", "large", "big", "tiny", "cute", "articulated", "flexi", "v2", "v3",
        "미니", "작은", "큰", "귀여운", "관절",
    };
    for (const char* qual : kOptionalQualifiers) {
        if (lower.find(qual) == std::string::npos)
            continue;
        std::string stripped = lower;
        boost::algorithm::replace_all(stripped, qual, " ");
        push_unique(stripped);
    }

    return variants;
}

}} // namespace

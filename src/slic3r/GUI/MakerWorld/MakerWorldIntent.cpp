#include "MakerWorldIntent.hpp"

#include "MakerWorldSearchCore.hpp"
#include "MakerWorldUrl.hpp"

#include <boost/algorithm/string.hpp>

namespace Slic3r { namespace GUI {

namespace {

bool has_compound_slicer_intent(const std::string& lower)
{
    static const char* kSlice[] = {
        "brim", "support", "infill", "layer", "temperature", "speed", "raft", "skirt",
        "브림", "서포트", "채움", "레이어", "온도", "속도", "지지", "채우",
        "set_config", "add_model", "delete", "slice", "슬라이스",
    };
    for (const char* s : kSlice) {
        if (lower.find(s) != std::string::npos)
            return true;
    }
    return false;
}

} // namespace

bool MakerWorldIntent::is_informational_makerworld_question(const std::string& user_text)
{
    std::string lower = user_text;
    boost::algorithm::to_lower(lower);
    if (lower.find('?') == std::string::npos && lower.find("？") == std::string::npos)
        return false;
    static const char* kInfo[] = {
        "what is", "what's", "how does", "how do", "explain", "tell me about",
        "뭐야", "무엇", "설명", "알려", "어떻게", "뭐예요", "뭔가요",
    };
    for (const char* s : kInfo) {
        if (lower.find(s) != std::string::npos)
            return true;
    }
    return false;
}

bool MakerWorldIntent::user_wants_makerworld_search(const std::string& user_text)
{
    if (is_informational_makerworld_question(user_text))
        return false;
    std::string lower = user_text;
    boost::algorithm::to_lower(lower);
    if (text_contains_makerworld_link(user_text))
        return false;
    if (has_compound_slicer_intent(lower))
        return false;

    const bool find_kw = lower.find("찾") != std::string::npos || lower.find("검색") != std::string::npos
        || lower.find("search") != std::string::npos || lower.find("find") != std::string::npos
        || lower.find("look for") != std::string::npos || lower.find("get me") != std::string::npos
        || lower.find("show me") != std::string::npos;
    const bool pick_kw = lower.find("뽑") != std::string::npos || lower.find("골라") != std::string::npos
        || lower.find("추천") != std::string::npos || lower.find("보고 싶") != std::string::npos
        || lower.find("pick") != std::string::npos || lower.find("choose") != std::string::npos
        || lower.find("recommend") != std::string::npos || lower.find("suggest") != std::string::npos;
    const bool model_kw = lower.find("모델") != std::string::npos || lower.find("model") != std::string::npos
        || lower.find("디자인") != std::string::npos || lower.find("design") != std::string::npos
        || lower.find("피규어") != std::string::npos || lower.find("figure") != std::string::npos
        || lower.find("figurine") != std::string::npos;
    const bool mw_kw = lower.find("makerworld") != std::string::npos || lower.find("메이커") != std::string::npos;
    // Acquisition verbs ("print me …", "~ 출력해줘") count as search intent only
    // combined with an explicit model-ish noun, so symptom sentences containing
    // 출력(물) never match.
    const bool acquire_kw = lower.find("출력해") != std::string::npos || lower.find("출력하고 싶") != std::string::npos
        || lower.find("인쇄해") != std::string::npos || lower.find("print me") != std::string::npos
        || lower.find("want to print") != std::string::npos;

    if ((find_kw && model_kw) || (find_kw && mw_kw) || (mw_kw && model_kw) || (pick_kw && model_kw)
        || (acquire_kw && model_kw))
        return true;

    // Natural language: "articulated dragon 찾아줘" without explicit "model"
    const std::string keywords = normalize_makerworld_search_query(user_text);
    if (keywords.size() < 2)
        return false;
    if (find_kw || pick_kw || mw_kw)
        return true;
    return false;
}

bool MakerWorldIntent::user_wants_makerworld_import(const std::string& user_text)
{
    if (text_contains_makerworld_link(user_text)) {
        std::string trimmed = user_text;
        boost::algorithm::trim(trimmed);
        if (trimmed.find(' ') == std::string::npos && trimmed.find("http") == 0) {
            if (!parse_design_id_from_url(trimmed).empty())
                return true;
        }
        std::string lower = user_text;
        boost::algorithm::to_lower(lower);
        const bool get_kw = lower.find("가져") != std::string::npos || lower.find("불러") != std::string::npos
            || lower.find("download") != std::string::npos || lower.find("import") != std::string::npos
            || lower.find("열어") != std::string::npos;
        return get_kw || lower.find(".3mf") != std::string::npos;
    }
    std::string lower = user_text;
    boost::algorithm::to_lower(lower);
    const bool get_kw = lower.find("가져") != std::string::npos || lower.find("불러") != std::string::npos
        || lower.find("download") != std::string::npos || lower.find("import") != std::string::npos
        || lower.find("열어") != std::string::npos;
    return get_kw && (lower.find("makerworld") != std::string::npos || lower.find("메이커") != std::string::npos);
}

bool MakerWorldIntent::is_pure_makerworld_request(const std::string& user_text)
{
    if (is_informational_makerworld_question(user_text))
        return false;
    std::string lower = user_text;
    boost::algorithm::to_lower(lower);
    if (has_compound_slicer_intent(lower))
        return false;
    if (user_wants_makerworld_import(user_text))
        return true;
    return user_wants_makerworld_search(user_text);
}

}} // namespace

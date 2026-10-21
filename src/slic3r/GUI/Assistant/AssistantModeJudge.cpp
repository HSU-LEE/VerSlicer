#include "AssistantModeJudge.hpp"

#include <boost/algorithm/string.hpp>

namespace Slic3r { namespace GUI {

namespace {

bool has_any(const std::string& text, std::initializer_list<const char*> words)
{
    for (const char* w : words)
        if (text.find(w) != std::string::npos)
            return true;
    return false;
}

// Asks the slicer to do something.
bool asks_for_change(const std::string& t)
{
    return has_any(t, {"해줘", "해 줘", "해주세요", "바꿔", "변경해", "켜줘", "켜 줘", "꺼줘", "꺼 줘", "올려", "낮춰",
                       "줄여", "늘려", "키워", "돌려", "회전", "적용해", "설정해", "추가해", "넣어", "빼줘", "지워",
                       "맞춰줘", "고쳐", "만들어줘", "만들어 줘", "슬라이스해", "정렬해", "배치해", "복제해", "되돌려",
                       "make it", "make this", "set ", "change ", "turn on", "turn off", "increase", "decrease",
                       "reduce", "raise", "lower", "rotate", "scale", "resize", "apply", "enable", "disable",
                       "add a", "add ", "remove", "slice it", "arrange", "undo", "fix it", "fix this"});
}

// Asks for an explanation only.
bool asks_for_explanation(const std::string& t)
{
    return has_any(t, {"뭐야", "무엇", "뭔가요", "뭐예요", "왜 ", "왜?", "차이", "설명해", "알려줘", "의미",
                       "what is", "what's", "what does", "why ", "explain", "difference", "meaning", "how does"});
}

// Wants the plate checked, which needs tools even though it is a question.
bool asks_for_check(const std::string& t)
{
    return has_any(t, {"잘될까", "잘 될까", "잘뽑힐까", "잘 뽑힐까", "실패할", "문제 있", "문제있", "괜찮을까",
                       "will this print", "will it print", "print well", "fail", "any problem", "weak spot",
                       "배웠", "learned", "카메라", "camera"});
}

} // namespace

std::optional<bool> AssistantModeJudge::judge(const std::string& user_text, bool apply_mode)
{
    const std::string t = boost::algorithm::to_lower_copy(user_text) + " ";
    const bool change = asks_for_change(t);
    const bool check  = asks_for_check(t);
    if (!apply_mode && (change || check) && !(asks_for_explanation(t) && !change))
        return true;
    if (apply_mode && asks_for_explanation(t) && !change && !check)
        return false;
    return std::nullopt;
}

}} // namespace Slic3r::GUI

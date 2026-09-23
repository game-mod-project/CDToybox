#include "game/effect_format.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace cdtb::game {
namespace {

constexpr std::string_view kStaticinfoPrefix = "Staticinfo:";

// body 는 토큰의 여는 `{` 다음부터 닫는 `}` 앞까지다("Param1" · "|Param1|" ·
// "Staticinfo:SubLevel:Hp" · "Key:Key_Skill_1" ...).
bool is_param_token(std::string_view body) {
    return body == "Param0" || body == "Param1" || body == "Param2" || body == "Param3";
}

bool is_abs_param_token(std::string_view body) {
    return body == "|Param0|" || body == "|Param1|" || body == "|Param2|" ||
           body == "|Param3|";
}

}  // namespace

std::string duration_suffix(std::uint32_t ms) {
    if (ms == 0) return {};
    if (ms >= 60000) return "(" + std::to_string(ms / 60000) + "분)";
    return "(" + std::to_string(ms / 1000) + "초)";
}

std::string effect_number(double v) {
    // 정수면 정수로 - to_string 은 "45.000000" 을 안 낸다.
    if (std::isfinite(v) && v == std::floor(v)) {
        return std::to_string(static_cast<long long>(v));
    }
    // 아니면 고정 소수로 찍고 뒤의 불필요한 0(과 남는 '.')을 뗀다. 내림은 안 한다 -
    // 값의 반올림·내림은 이 함수를 부르기 전(배율 적용 쪽)의 책임이다.
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << v;
    std::string s = oss.str();
    std::size_t last = s.find_last_not_of('0');
    if (last != std::string::npos) {
        if (s[last] == '.') --last;
        s.erase(last + 1);
    }
    return s;
}

std::string effect_line(
    std::string_view format, const EffectValues& v,
    const std::function<std::string(std::string_view, std::string_view)>& name_of) {
    if (format.empty()) return {};

    std::string out;
    out.reserve(format.size());
    std::size_t i = 0;
    while (i < format.size()) {
        if (format[i] != '{') {
            out += format[i];
            ++i;
            continue;
        }

        const std::size_t close = format.find('}', i);
        if (close == std::string_view::npos) {
            // 짝이 안 맞는 여는 중괄호 - 남은 문자열을 그대로 붙이고 끝낸다.
            out.append(format.substr(i));
            break;
        }

        const std::string_view token = format.substr(i, close - i + 1);  // "{...}" 포함
        const std::string_view body = format.substr(i + 1, close - i - 1);  // 안쪽 내용

        if (is_param_token(body)) {
            out += effect_number(v.param);
        } else if (is_abs_param_token(body)) {
            out += effect_number(std::abs(v.param));
        } else if (body == "RepeatTick") {
            out += effect_number(v.repeat_tick);
        } else if (body.starts_with(kStaticinfoPrefix)) {
            const std::string_view rest = body.substr(kStaticinfoPrefix.size());
            const std::size_t colon = rest.find(':');
            std::string name;
            if (colon != std::string_view::npos && name_of) {
                name = name_of(rest.substr(0, colon), rest.substr(colon + 1));
            }
            out.append(!name.empty() ? name : std::string(token));
        } else {
            // {Key:...} · {Money:...} 등 - 손대지 않는다.
            out.append(token);
        }
        i = close + 1;
    }
    return out + duration_suffix(v.duration_ms);
}

}  // namespace cdtb::game

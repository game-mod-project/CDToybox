#include "game/effect_format.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace cdtb::game {
namespace {

constexpr std::string_view kStaticinfoPrefix = "Staticinfo:";

// 종류 4~7 은 0~3 의 **절대값 표시** 짝이다(§4.7-E). `{|Param1|}` = 종류 5.
constexpr std::size_t kAbsTypeBase = 4;
constexpr std::size_t kRepeatTickType = 8;

// body 는 토큰의 여는 `{` 다음부터 닫는 `}` 앞까지다("Param1" · "|Param1|" ·
// "Staticinfo:SubLevel:Hp" · "Key:Key_Skill_1" ...).
//
// `{ParamN}` 이면 N(0~3)을, 아니면 -1 을 낸다.
int param_index(std::string_view body) {
    if (body.size() != 6 || body.substr(0, 5) != "Param") return -1;
    const char c = body[5];
    if (c < '0' || c > '3') return -1;
    return c - '0';
}

// `{|ParamN|}` 이면 N(0~3)을, 아니면 -1 을 낸다.
int abs_param_index(std::string_view body) {
    if (body.size() != 8 || body.front() != '|' || body.back() != '|') {
        return -1;
    }
    return param_index(body.substr(1, 6));
}

}  // namespace

bool format_needs_params(std::string_view format) {
    // 토큰 자르기는 effect_line 과 **같은 규칙**이라야 한다 - 여기서 "값 자리가
    // 없다" 고 본 줄을 effect_line 이 값 자리로 읽으면 토큰이 새어 나간다.
    std::size_t i = 0;
    while (i < format.size()) {
        if (format[i] != '{') {
            ++i;
            continue;
        }
        const std::size_t close = format.find('}', i);
        if (close == std::string_view::npos) break;   // 짝이 안 맞는다 - 그대로 둔다
        const std::string_view body = format.substr(i + 1, close - i - 1);
        if (param_index(body) >= 0 || abs_param_index(body) >= 0 ||
            body == "RepeatTick") {
            return true;
        }
        i = close + 1;
    }
    return false;
}

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
    std::string_view format, const EffectParams& v,
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

        const int plain = param_index(body);
        const int absolute = abs_param_index(body);
        if (plain >= 0) {
            const auto type = static_cast<std::size_t>(plain);
            out.append(v.has(type) ? effect_number(v.value[type])
                                   : std::string(token));
        } else if (absolute >= 0) {
            const auto type = static_cast<std::size_t>(absolute) + kAbsTypeBase;
            out.append(v.has(type) ? effect_number(std::abs(v.value[type]))
                                   : std::string(token));
        } else if (body == "RepeatTick") {
            out.append(v.has(kRepeatTickType)
                           ? effect_number(v.value[kRepeatTickType])
                           : std::string(token));
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

std::string effect_line(
    std::string_view format, const EffectValues& v,
    const std::function<std::string(std::string_view, std::string_view)>& name_of) {
    // 단순형은 종류를 안 가린다 - 모든 값 자리를 같은 값으로 채운다.
    EffectParams p;
    p.duration_ms = v.duration_ms;
    for (std::size_t t = 0; t < kRepeatTickType; ++t) p.set(t, v.param);
    p.set(kRepeatTickType, v.repeat_tick);
    return effect_line(format, p, name_of);
}

}  // namespace cdtb::game

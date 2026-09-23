#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace cdtb::game {

// PatternDescriptionInfo 형식 문자열에 값을 끼워 효과 한 줄을 만든다(순수 함수 -
// 게임 메모리·ImGui 를 모른다).
//
// 입구가 둘이다.
//   * `EffectValues` - 값이 하나뿐인 단순형. 한 줄에 `{Param1}` 하나가 오는
//     흔한 꼴이다.
//   * `EffectParams` - **파라미터 종류별로** 값을 받는다. 한 줄에 종류가 여럿인
//     형식이 실재하기 때문이다(실측: `'... {RepeatTick} 초마다 {|Param1|} 감소'`
//     = 종류 8 + 종류 5, `'{Param1} {Param2}칸 확장'` = 종류 1 + 종류 2).
//     종류마다 값이 **BuffData 의 다른 칸**에서 오므로 하나로 못 합친다.
//   앞의 것은 뒤의 것으로 구현돼 있다(모든 종류를 같은 값으로 채운다).
//
// 자리표시자(specs/2026-09-22-item-description-effects-design.md §4.7-E, 실측):
//   {Param0}~{Param3}      = 종류 0~3.
//   {|Param0|}~{|Param3|}  = 종류 4~7(절대값 표시). `{|Param1|}` 은 **종류 5** 다.
//   {RepeatTick}           = 종류 8.
//   {Staticinfo:<표>:<키>} = name_of(표, 키) 로 바꾼다. 이름표가 비면(빈 문자열이거나
//                            콜백이 없으면) 토큰을 그대로 둔다 - 빈 자리가 남으면
//                            문장이 깨지기 때문이다.
// 그 밖의 `{...}`(`{Key:<액션>}` · `{Money:<통화>:<n>}` 등)는 손대지 않는다.
// 서로 안 맞는 중괄호(닫는 `}` 없는 `{...` · 여는 `{` 없는 `...}`)도 죽지 않고 그대로 둔다.

// 파라미터 종류는 0~8 이다(위 자리표시자 표).
inline constexpr std::size_t kEffectParamTypeCount = 9;

// 종류별 값. 색인이 곧 파라미터 종류다.
//
// 걷기 쪽(item_effects)은 패턴의 `_paramList` 를 돌며 종류마다 다른 규칙으로
// 값을 구해 여기에 넣는다. **안 채운 종류의 토큰은 그대로 남는다** - 빈 자리를
// 남기느니 토큰을 보이는 쪽이 낫다(위 `{Staticinfo:...}` 와 같은 이유).
struct EffectParams {
    double value[kEffectParamTypeCount] = {};
    bool filled[kEffectParamTypeCount] = {};
    std::uint32_t duration_ms = 0;  // 접미 "(1분)" 계산용, 0 이면 접미 없음

    void set(std::size_t type, double v) {
        if (type < kEffectParamTypeCount) {
            value[type] = v;
            filled[type] = true;
        }
    }
    bool has(std::size_t type) const {
        return type < kEffectParamTypeCount && filled[type];
    }
};

struct EffectValues {
    double param = 0.0;             // {Param0..3}/{|Param0..3|} 자리 값(배율 적용 뒤)
    double repeat_tick = 0.0;       // {RepeatTick} 자리, 초 단위
    std::uint32_t duration_ms = 0;  // 접미 "(1분)" 계산용, 0 이면 접미 없음
};

// 형식 문자열에 값을 끼운다. 끝에 duration_suffix(duration_ms) 를 붙인다.
// 형식 문자열이 비면 결과도 비고(접미도 안 붙는다).
std::string effect_line(std::string_view format, const EffectValues& v,
                        const std::function<std::string(std::string_view table,
                                                        std::string_view key)>& name_of);

// 같은 일을 종류별 값으로 한다. `{|ParamN|}` 은 종류 N+4 의 값을 절대값으로 낸다.
std::string effect_line(std::string_view format, const EffectParams& v,
                        const std::function<std::string(std::string_view table,
                                                        std::string_view key)>& name_of);

// 지속시간 접미. ms >= 60000 이면 "(N분)"(N = ms/60000 내림), 0 < ms < 60000 이면
// "(N초)"(N = ms/1000 내림), ms == 0 이면 빈 문자열. 실측: 90000 -> "(1분)".
std::string duration_suffix(std::uint32_t ms);

// 숫자 표기: 정수면 정수로, 아니면 소수점 이하 불필요한 0 을 떼고 내림 없이 그대로.
std::string effect_number(double v);

}  // namespace cdtb::game

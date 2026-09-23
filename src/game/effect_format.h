#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace cdtb::game {

// PatternDescriptionInfo 형식 문자열에 값을 끼워 효과 한 줄을 만든다(순수 함수 -
// 게임 메모리·ImGui 를 모른다). 값이 하나뿐인 단순형이다 - 한 줄에 파라미터 종류가
// 여럿인 경우(값마다 다른 칸에서 옴)는 걷기 쪽(Task 4)이 다룬다.
//
// 자리표시자(specs/2026-09-22-item-description-effects-design.md §4.7-E, 실측):
//   {Param0}~{Param3}      = 종류 0~3, v.param 그대로.
//   {|Param0|}~{|Param3|}  = 종류 4~7, std::abs(v.param).
//   {RepeatTick}           = 종류 8, v.repeat_tick.
//   {Staticinfo:<표>:<키>} = name_of(표, 키) 로 바꾼다. 이름표가 비면(빈 문자열이거나
//                            콜백이 없으면) 토큰을 그대로 둔다 - 빈 자리가 남으면
//                            문장이 깨지기 때문이다.
// 그 밖의 `{...}`(`{Key:<액션>}` · `{Money:<통화>:<n>}` 등)는 손대지 않는다.
// 서로 안 맞는 중괄호(닫는 `}` 없는 `{...` · 여는 `{` 없는 `...}`)도 죽지 않고 그대로 둔다.
struct EffectValues {
    double param = 0.0;             // {Param0..3}/{|Param0..3|} 자리 값(배율 적용 뒤)
    double repeat_tick = 0.0;       // {RepeatTick} 자리, 초 단위
    std::uint32_t duration_ms = 0;  // 접미 "(1분)" 계산용, 0 이면 접미 없음
};

// 형식 문자열에 값을 끼운다. 끝에 duration_suffix(v.duration_ms) 를 붙인다.
// 형식 문자열이 비면 결과도 비고(접미도 안 붙는다).
std::string effect_line(std::string_view format, const EffectValues& v,
                        const std::function<std::string(std::string_view table,
                                                        std::string_view key)>& name_of);

// 지속시간 접미. ms >= 60000 이면 "(N분)"(N = ms/60000 내림), 0 < ms < 60000 이면
// "(N초)"(N = ms/1000 내림), ms == 0 이면 빈 문자열. 실측: 90000 -> "(1분)".
std::string duration_suffix(std::uint32_t ms);

// 숫자 표기: 정수면 정수로, 아니면 소수점 이하 불필요한 0 을 떼고 내림 없이 그대로.
std::string effect_number(double v);

}  // namespace cdtb::game

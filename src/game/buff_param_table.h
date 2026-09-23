#pragma once

#include <cstddef>
#include <cstdint>

namespace cdtb::game {

// 버프 **파라미터 배율 표** — 효과 문구의 `{Param1}` 자리에 넣을 값이
// BuffData 의 **어느 칸**에 있고 **무엇으로 나누는지**.
//
// 왜 표인가
// ---------
// 값은 정수로 저장돼 있고(예: 45000), 화면에는 나눈 뒤가 나온다(45). 그 배율이
// BuffData 의 **파생 클래스마다 다르다** — 같은 `+0x98` 이라도 ÷1000 · ÷10⁴ ·
// ÷10⁷ 이 섞여 있다. 게임 코드(가상 함수 슬롯 11)에 규칙이 그대로 있어서
// 손으로 옮기지 않고 **뽑아서 커밋한다**:
//
//     python tools/rtti/buff_params.py <덤프이미지> <vtable목록> \
//            --out src/game/buff_param_table.inc
//
// 생성기는 아는 셋(÷1000 · ÷10⁴ · ÷10⁷)으로 자기 검증을 하고, 안 맞으면 표를
// 내지 않고 죽는다. 근거는 명세
// `docs/superpowers/specs/2026-09-22-item-description-effects-design.md` §4.7-H''.
//
// 모르는 클래스
// -------------
// 규칙을 못 뽑은 클래스는 표에 **넣지 않는다**. `buff_param_divisor` 가 0 을,
// `buff_param_offset` 이 0 을 돌려주면 그 뜻이다. 부르는 쪽은 그 효과 줄을
// 버리고 "해석 못 한 효과" 로 세어야 한다 — 틀린 숫자를 보이는 것보다 낫다.
//
// 게임이 갱신되면
// ---------------
// `vtable_va` 는 모듈 고정 VA 다. 갱신 뒤에는 이미지를 다시 떠서 생성기를 다시
// 돌린다([[game-update-rva-drift]] — 영역마다 이동 폭이 달라 단일 델타는 금물).

struct BuffParamRule {
    std::uint64_t vtable_va;   // BuffData 파생 클래스의 vtable (모듈 고정 VA)
    std::uint16_t offset;      // 값이 사는 칸 (BuffData 기준)
    double divisor;            // 화면 값 = 칸 값 / divisor
    const char* note;          // 어떤 파라미터 종류인지 · 곁가지 규칙
};

#include "game/buff_param_table.inc"

constexpr std::size_t buff_param_rule_count() noexcept {
    return sizeof(kBuffParamRules) / sizeof(kBuffParamRules[0]);
}

// 모르는 vtable 이면 nullptr.
constexpr const BuffParamRule* buff_param_rule(std::uint64_t vtable) noexcept {
    for (const BuffParamRule& r : kBuffParamRules) {
        if (r.vtable_va == vtable) {
            return &r;
        }
    }
    return nullptr;
}

// 값이 사는 칸. **0 이면 모르는 클래스다**(진짜 오프셋은 0 일 수 없다).
constexpr std::uint16_t buff_param_offset(std::uint64_t vtable) noexcept {
    const BuffParamRule* r = buff_param_rule(vtable);
    return r ? r->offset : std::uint16_t{0};
}

// 나누는 수. **0 이면 모르는 클래스다**(원값 그대로면 1 이다).
constexpr double buff_param_divisor(std::uint64_t vtable) noexcept {
    const BuffParamRule* r = buff_param_rule(vtable);
    return r ? r->divisor : 0.0;
}

}  // namespace cdtb::game

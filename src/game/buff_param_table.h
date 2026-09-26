#pragma once

#include <cstddef>
#include <cstdint>

namespace cdtb::game {

// 버프 **파라미터 배율 표** — 효과 문구의 `{ParamN}` 자리에 넣을 값이
// BuffData 의 **어느 칸**에 있고 **무엇으로 나누는지**.
//
// 왜 표인가
// ---------
// 값은 정수로 저장돼 있고(예: 45000), 화면에는 나눈 뒤가 나온다(45). 그 배율이
// 한 자리에 고정돼 있지 않다 — 같은 `+0x98` 이라도 ÷1000 · ÷10⁴ · ÷10⁷ 이
// 섞인다. 게임 코드(가상 함수 슬롯 11)에 규칙이 그대로 있어서 손으로 옮기지 않고
// **뽑아서 커밋한다**:
//
//     python tools/rtti/buff_params.py <덤프이미지> <vtable목록> \
//            --out src/game/buff_param_table.inc
//
// 생성기는 아는 셋(÷1000 · ÷10⁴ · ÷10⁷)으로 자기 검증을 하고, 안 맞으면 표를
// 내지 않고 죽는다. 근거는 명세
// `docs/superpowers/specs/2026-09-22-item-description-effects-design.md` §4.7-H''.
//
// 키가 셋이다 (2026-09-23 수정)
// -----------------------------
// vtable 하나로는 못 고른다. 호출부(RVA 0x1F29931~)가 슬롯 11 에 넘기는 인자가
// 둘 더 있고, **둘 다 배율을 가른다**:
//
//     mov  rax, [rdi]                       ; rdi = BuffData
//     movzx r9d, byte ptr [rdi + 0x3a]      ; 넷째 인자 = BuffData +0x3A
//     mov  r8b, <종류>                      ; 셋째 인자 = 파라미터 종류
//     lea  rdx, [rsp + ...]                 ; 둘째 = 결과 자리
//     mov  rcx, rdi                         ; 첫째 = this
//     call qword ptr [rax + 0x58]           ; 슬롯 11
//
//   * **파라미터 종류**(r8b) — `{Param0..3}` = 0..3, `{|Param0..3|}` = 4..7.
//     `DamageBuffData` 는 종류 1 이 `+0x98 ÷1000`, 종류 3 이 `+0xF8 ÷10⁴` 다.
//   * **BuffData `+0x3A`**(r9b) — 비율형 클래스가 `cmp r9b, 1` 로 갈라진다.
//     1 이면 ÷10⁷, 아니면 ÷1000. 실측 분포는 0 이 2881개, 1 이 232개다.
//     이것은 BuffData 자신의 바이트다. 패턴 파라미터 항목의 둘째 바이트
//     (`_isDisplayAbsoluteNumber`)와는 **다른 것**이다 — 그 바이트는 호출부가
//     따로 본다(`cmp byte ptr [rax + r15*2 + 1], 0`).
//
// 나눗셈이 두 갈래다 — `integer_div`
// ----------------------------------
// 같은 "÷10000" 이라도 게임이 **어떻게** 나누는지가 다르다.
//
//   * **정수**(`integer_div == true`) — 컴파일러 매직 상수(`imul` + `sar`)다.
//     나머지가 버려지므로 우리도 잘라야 한다. 실측: 소켓 치명타 25000/10⁴ -> **2**,
//     75000/10⁴ -> **7**(반올림이면 3 · 8 이 됐을 것이다, §4.7-H 3번).
//   * **실수**(`false`) — `vdivsd`/`vmulsd` 다. **소수가 살아남는다** — 여기서
//     자르면 화면값이 2.5 여야 할 자리에 2 가 나간다.
//
// 생성기가 모의 실행 중에 어느 경로를 지났는지 보고 이 칸을 채운다. 둘이 섞인
// 조합은 false 다(없는 자릿수를 만들지 않는다).
//
// 종류 8(`{RepeatTick}`)은 이 표에 없다. 호출부가 슬롯 11 을 부르지 않고
// BuffData `+0x28`(주기 ms)을 1000.0 으로 나눠 직접 만든다(RVA 0x1F29A51) —
// 여기도 **실수 나눗셈이라 안 자른다**.
//
// 모르는 조합
// -----------
// 규칙을 못 뽑은 조합은 표에 **넣지 않는다**. `buff_param_divisor` 가 0 을,
// `buff_param_offset` 이 0 을 돌려주면 그 뜻이다. 부르는 쪽은 그 효과 줄을
// 버리고 "해석 못 한 효과" 로 세어야 한다 — 틀린 숫자를 보이는 것보다 낫다.
// 클래스 자체를 아는지(`buff_param_class_known`)와 이 조합을 아는지는 다르다.
//
// 게임이 갱신되면
// ---------------
// `vtable_va` 는 모듈 고정 VA 다. 갱신 뒤에는 이미지를 다시 떠서 생성기를 다시
// 돌린다([[game-update-rva-drift]] — 영역마다 이동 폭이 달라 단일 델타는 금물).

struct BuffParamRule {
    std::uint64_t vtable_va;   // BuffData 파생 클래스의 vtable (모듈 고정 VA)
    std::uint8_t param_type;   // 슬롯 11 의 r8b. {Param0..3}=0..3, {|Param0..3|}=4..7
    std::uint8_t flag3a;       // 슬롯 11 의 r9b = BuffData +0x3A (0 또는 1)
    std::uint16_t offset;      // 값이 사는 칸 (BuffData 기준)
    double divisor;            // 화면 값 = 칸 값 / divisor
    bool integer_div;          // 정수 나눗셈인가 (아래)
    const char* note;          // 곁가지 규칙 (절대값 표시 · 겹침 증분 · thunk)
};

// 클래스 이름 -> vtable. 생성기가 같이 낸다.
//
// vtable VA 는 게임 갱신마다 옮겨 다니고 **서로의 순서도 안 지켜진다**(2949->2976
// 에서 실측). 그래서 자리를 박아 두는 쪽은 갱신마다 깨지고, 한 곳만 잘못 고치면
// 조용히 다른 클래스를 보게 된다. 이름은 갱신을 안 탄다.
struct BuffParamClass {
    const char* name;          // RTTI 이름에서 `.?AV` 와 `@pa@@` 를 뗀 것
    std::uint64_t vtable_va;   // 모듈 고정 VA
};

#include "game/buff_param_table.inc"

constexpr std::size_t buff_param_class_count() noexcept {
    return sizeof(kBuffParamClasses) / sizeof(kBuffParamClasses[0]);
}

// 이름으로 vtable 을 찾는다. 모르는 이름이면 0.
//
// **시험·도구 전용이다.** 걷기 쪽(`item_effects.cpp`)은 런타임에 읽은 vtable 로
// 곧장 `buff_param_rule` 을 부른다 - 이름 비교를 효과 줄마다 할 이유가 없다.
constexpr std::uint64_t buff_param_vtable(const char* name) noexcept {
    if (name == nullptr) return 0;
    for (const BuffParamClass& c : kBuffParamClasses) {
        const char* a = c.name;
        const char* b = name;
        while (*a != '\0' && *a == *b) {
            ++a;
            ++b;
        }
        if (*a == '\0' && *b == '\0') return c.vtable_va;
    }
    return 0;
}

constexpr std::size_t buff_param_rule_count() noexcept {
    return sizeof(kBuffParamRules) / sizeof(kBuffParamRules[0]);
}

// 게임은 `cmp r9b, 1` 만 한다 — 1 이 아닌 값은 전부 0 과 같다.
constexpr std::uint8_t buff_param_norm_flag(std::uint8_t flag3a) noexcept {
    return flag3a == 1 ? std::uint8_t{1} : std::uint8_t{0};
}

// 모르는 조합이면 nullptr.
constexpr const BuffParamRule* buff_param_rule(std::uint64_t vtable,
                                               std::uint8_t param_type,
                                               std::uint8_t flag3a) noexcept {
    const std::uint8_t f = buff_param_norm_flag(flag3a);
    for (const BuffParamRule& r : kBuffParamRules) {
        if (r.vtable_va == vtable && r.param_type == param_type && r.flag3a == f) {
            return &r;
        }
    }
    return nullptr;
}

// 값이 사는 칸. **0 이면 모르는 조합이다**(진짜 오프셋은 0 일 수 없다).
constexpr std::uint16_t buff_param_offset(std::uint64_t vtable,
                                          std::uint8_t param_type,
                                          std::uint8_t flag3a) noexcept {
    const BuffParamRule* r = buff_param_rule(vtable, param_type, flag3a);
    return r ? r->offset : std::uint16_t{0};
}

// 나누는 수. **0 이면 모르는 조합이다**(원값 그대로면 1 이다).
constexpr double buff_param_divisor(std::uint64_t vtable,
                                    std::uint8_t param_type,
                                    std::uint8_t flag3a) noexcept {
    const BuffParamRule* r = buff_param_rule(vtable, param_type, flag3a);
    return r ? r->divisor : 0.0;
}

// 클래스를 아예 모르는 것과 "아는 클래스인데 이 종류는 안 쓴다" 를 가른다 —
// 전자는 표를 다시 뽑아야 하고, 후자는 정상이다.
//
// 걷기 쪽(`item_effects.cpp`)은 `buff_param_rule` 하나만 쓴다. 이 함수와 위의
// `buff_param_offset`/`buff_param_divisor`/`buff_param_rule_count` 는 **시험과
// 도구용**이다(`tests/buff_param_table_tests.cpp` 가 표의 모양을 여기로 본다).
constexpr bool buff_param_class_known(std::uint64_t vtable) noexcept {
    for (const BuffParamRule& r : kBuffParamRules) {
        if (r.vtable_va == vtable) {
            return true;
        }
    }
    return false;
}

}  // namespace cdtb::game

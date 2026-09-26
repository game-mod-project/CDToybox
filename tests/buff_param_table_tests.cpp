// 버프 파라미터 **배율 표**. 이 표가 틀리면 효과 문구의 숫자가 틀리고,
// 숫자가 틀린 툴팁은 없는 툴팁보다 나쁘다. 표는 생성물이므로 여기서 지키는 것은
// "생성기를 다시 돌렸을 때 실측값이 그대로인가" 다 — 값이 바뀌면 여기서 깨진다.
//
// 키는 셋(vtable · 파라미터 종류 · BuffData +0x3A)이다. 셋 다 안 보고 고르면
// 같은 클래스에서 1000배 틀린 숫자가 나온다 — 그것을 못박는 시험이 아래 있다.
#include <cstdint>

#include "game/buff_param_table.h"
#include "harness.h"

using cdtb::game::buff_param_class_known;
using cdtb::game::buff_param_divisor;
using cdtb::game::buff_param_norm_flag;
using cdtb::game::buff_param_offset;
using cdtb::game::buff_param_rule;
using cdtb::game::buff_param_rule_count;
using cdtb::game::BuffParamRule;
using cdtb::game::kBuffParamRules;
using cdtb::game::buff_param_vtable;

// **vtable 자리를 박지 않는다.** VA 는 게임 갱신마다 옮겨 다니고 서로의 순서도
// 안 지켜진다 - 1.0.0.2949 -> 2976 에서 이름<->주소 16쌍을 두 빌드에서 정렬해
// 견줘 보니 BlockCrime 이 14번째에서 최하위로 갔다. 자리를 박으면 갱신마다
// 이 파일에서만 서른 곳을 손으로 고쳐야 하고, 한 곳만 틀려도 시험이 조용히
// 다른 클래스를 본다. 이름은 갱신을 안 탄다.
constexpr std::uint64_t kVtStatMax =
    buff_param_vtable("VaryStatMaxValueBuffData");
// 어비스 소켓 치명타("갑옷 타격 시 치명타 확률 N% 증가")를 내는 클래스.
// 근거는 게임 대조다 - 관통 I·II·III 가 2% · 5% · 7% 로 찍히는 것
// (`cdtb_probe effects 1003765 1003766 1003767`).
constexpr std::uint64_t kVtCrit =
    buff_param_vtable("AddCritiacalRateByMaterialKeyBuffData");
constexpr std::uint64_t kVtDefStatRate =
    buff_param_vtable("VaryDataDefinedStatRateBuffData");
constexpr std::uint64_t kVtStat = buff_param_vtable("VaryStatBuffData");
constexpr std::uint64_t kVtDefStat =
    buff_param_vtable("VaryDataDefinedStatBuffData");
constexpr std::uint64_t kVtStatRate = buff_param_vtable("VaryStatRateBuffData");
constexpr std::uint64_t kVtDamage = buff_param_vtable("DamageBuffData");
constexpr std::uint64_t kVtMinRate = buff_param_vtable("SetStatMinRateBuffData");
constexpr std::uint64_t kVtBuffLevel =
    buff_param_vtable("ChangeBuffLevelBuffData");
constexpr std::uint64_t kVtStaticLevel =
    buff_param_vtable("VaryStaticStatLevelBuffData");
constexpr std::uint64_t kVtVoidPassive = buff_param_vtable("VoidPassiveBuffData");
constexpr std::uint64_t kVtVoidActive = buff_param_vtable("VoidActiveBuffData");
constexpr std::uint64_t kVtImmune = buff_param_vtable("ImmuneBuffData");
constexpr std::uint64_t kVtIgnoreResource =
    buff_param_vtable("IgnoreUseResourceStatBuffData");

TEST(buff_param_table_resolves_every_class_this_file_names) {
    // 이름이 하나라도 표에서 사라지면 아래 시험들이 전부 "0 번지" 를 물어 조용히
    // 통과한다. 그 구멍을 여기서 막는다.
    const std::uint64_t named[] = {
        kVtStatMax,     kVtCrit,        kVtDefStatRate,    kVtStat,
        kVtDefStat,     kVtStatRate,    kVtDamage,         kVtMinRate,
        kVtBuffLevel,   kVtStaticLevel, kVtVoidPassive,    kVtVoidActive,
        kVtImmune,      kVtIgnoreResource};
    for (const std::uint64_t vt : named) {
        CHECK(vt > 0x140000000ull && vt < 0x150000000ull);
    }
    CHECK_EQ(buff_param_vtable("그런 클래스 없다"), 0ull);
    CHECK_EQ(buff_param_vtable(nullptr), 0ull);
}

TEST(buff_param_table_has_the_three_measured_rules) {
    // 게임 툴팁과 글자까지 맞춘 줄에서 나온 실측 셋.
    // 생성기를 다시 돌려 표가 바뀌면 여기서 깨진다.
    CHECK_EQ(buff_param_divisor(kVtStatMax, 1, 0), 1000.0);      // 생명 최대치 45
    CHECK_EQ(buff_param_divisor(kVtCrit, 1, 0), 10000.0);     // 치명타 2/5/7%
    CHECK_EQ(buff_param_divisor(kVtDefStatRate, 2, 1), 10000000.0);  // 용기 75% 회복
    CHECK_EQ(buff_param_offset(kVtStatMax, 1, 0), 0x98);
    CHECK_EQ(buff_param_offset(kVtCrit, 1, 0), 0x98);
    CHECK_EQ(buff_param_offset(kVtDefStatRate, 2, 1), 0x98);
}

TEST(buff_param_table_splits_one_class_by_the_buffdata_flag) {
    // 같은 클래스·같은 종류인데 BuffData +0x3A 로 배율이 갈린다.
    // 이 갈래를 안 보고 하나만 쓰면 용기 회복이 75 대신 750000 으로 나온다.
    CHECK_EQ(buff_param_divisor(kVtDefStatRate, 2, 1), 10000000.0);
    CHECK_EQ(buff_param_divisor(kVtDefStatRate, 2, 0), 1000.0);
    CHECK(buff_param_divisor(kVtDefStatRate, 2, 0)
          != buff_param_divisor(kVtDefStatRate, 2, 1));
    // 같은 갈래가 걸린 다른 클래스들(비율 가족)도 같은 모양이다.
    CHECK_EQ(buff_param_divisor(kVtStat, 1, 0), 1000.0);       // VaryStat
    CHECK_EQ(buff_param_divisor(kVtStat, 1, 1), 10000000.0);
    CHECK_EQ(buff_param_divisor(kVtDefStat, 1, 0), 1000.0);       // VaryDataDefinedStat
    CHECK_EQ(buff_param_divisor(kVtDefStat, 1, 1), 10000000.0);
    CHECK_EQ(buff_param_divisor(kVtStatRate, 1, 0), 1.0);          // VaryStatRate
    CHECK_EQ(buff_param_divisor(kVtStatRate, 1, 1), 10000.0);
    // 안 갈리는 클래스는 두 값이 같아야 한다 — 갈래를 잘못 붙이지 않았는지.
    CHECK_EQ(buff_param_divisor(kVtStatMax, 1, 1), 1000.0);
    CHECK_EQ(buff_param_divisor(kVtCrit, 1, 1), 10000.0);
}

TEST(buff_param_table_splits_one_class_by_the_parameter_type) {
    // Damage 는 종류마다 다른 칸을 쓴다. 종류를 안 보면 남의 칸을 읽는다.
    CHECK_EQ(buff_param_offset(kVtDamage, 1, 0), 0x98);
    CHECK_EQ(buff_param_divisor(kVtDamage, 1, 0), 1000.0);
    CHECK_EQ(buff_param_offset(kVtDamage, 3, 0), 0xF8);
    CHECK_EQ(buff_param_divisor(kVtDamage, 3, 0), 10000.0);
    // SetStatMinRate 도 마찬가지다(+0x98 / +0xA0).
    CHECK_EQ(buff_param_offset(kVtMinRate, 1, 0), 0x98);
    CHECK_EQ(buff_param_offset(kVtMinRate, 2, 0), 0xA0);
}

TEST(buff_param_table_covers_the_absolute_value_twins) {
    // {|ParamN|} 은 종류 N+4 로 온다. 같은 칸·같은 배율이어야 한다.
    for (std::uint8_t t = 0; t < 4; ++t) {
        const BuffParamRule* plain = buff_param_rule(kVtStatMax, t, 0);
        const BuffParamRule* twin =
            buff_param_rule(kVtStatMax, static_cast<std::uint8_t>(t + 4), 0);
        CHECK((plain == nullptr) == (twin == nullptr));
        if (plain && twin) {
            CHECK_EQ(plain->offset, twin->offset);
            CHECK_EQ(plain->divisor, twin->divisor);
        }
    }
}

TEST(buff_param_table_reports_unknown_combinations) {
    CHECK_EQ(buff_param_divisor(0xDEADBEEFull, 1, 0), 0.0);   // 모르는 클래스
    CHECK_EQ(buff_param_offset(0xDEADBEEFull, 1, 0), 0);
    CHECK(buff_param_rule(0xDEADBEEFull, 1, 0) == nullptr);
    CHECK(!buff_param_class_known(0xDEADBEEFull));
    // 아는 클래스인데 안 쓰는 종류 - 이것도 "모름"이다.
    CHECK_EQ(buff_param_divisor(kVtStatMax, 2, 0), 0.0);
    CHECK(buff_param_class_known(kVtStatMax));
    // 종류 8({RepeatTick})은 슬롯 11 에 오지 않는다 - 표에 없어야 한다.
    for (const BuffParamRule& r : kBuffParamRules) {
        CHECK(r.param_type < 8);
    }
}

TEST(buff_param_table_treats_any_flag_other_than_one_as_zero) {
    // 게임은 `cmp r9b, 1` 만 한다. 2 가 들어와도 0 갈래여야 한다.
    CHECK_EQ(buff_param_norm_flag(0), 0);
    CHECK_EQ(buff_param_norm_flag(1), 1);
    CHECK_EQ(buff_param_norm_flag(2), 0);
    CHECK_EQ(buff_param_norm_flag(0xFF), 0);
    CHECK_EQ(buff_param_divisor(kVtDefStatRate, 2, 7), 1000.0);
}

TEST(buff_param_table_leaves_classes_without_a_parameter_out) {
    // 슬롯 11 이 빈 문자열만 돌려주는 클래스들. 추측해 넣으면 안 된다.
    CHECK(!buff_param_class_known(kVtVoidPassive));   // VoidPassiveBuffData
    CHECK(!buff_param_class_known(kVtVoidActive));   // VoidActiveBuffData
    CHECK(!buff_param_class_known(kVtImmune));   // ImmuneBuffData
    // 파라미터가 숫자가 아니라 표 조회(이름)뿐인 것도 마찬가지다.
    CHECK(!buff_param_class_known(kVtIgnoreResource));   // IgnoreUseResourceStat
}

TEST(buff_param_table_keeps_the_level_classes_at_their_raw_value) {
    // 명세 §4.7-H' 2번 표: 레벨형은 나눗셈이 없다. 1 은 "원값", 0 은 "모름"이라
    // 둘을 섞으면 부르는 쪽이 모르는 조합을 원값으로 그린다.
    CHECK_EQ(buff_param_offset(kVtBuffLevel, 1, 0), 0x94);   // ChangeBuffLevel
    CHECK_EQ(buff_param_divisor(kVtBuffLevel, 1, 0), 1.0);
    CHECK_EQ(buff_param_offset(kVtStaticLevel, 1, 0), 0xA0);   // VaryStaticStatLevel
    CHECK_EQ(buff_param_divisor(kVtStaticLevel, 1, 0), 1.0);
}

TEST(buff_param_table_marks_integer_and_float_divisions_apart) {
    // 같은 "÷N" 이라도 게임이 매직 상수(`imul`+`sar`)로 나눈 자리는 나머지가
    // 버려지고(정수), `vdivsd` 자리는 소수가 남는다. 하나로 뭉뚱그려 전부 자르면
    // 2.5 여야 할 화면값이 2 로 나간다 - 그 갈래를 표가 들고 있어야 한다.
    const BuffParamRule* crit = buff_param_rule(kVtCrit, 1, 0);
    CHECK(crit != nullptr);
    if (crit != nullptr) {
        // 실측: 25000/10⁴ -> 2 · 75000/10⁴ -> 7(반올림이면 3 · 8).
        CHECK(crit->integer_div);
    }
    const BuffParamRule* rate = buff_param_rule(kVtDefStatRate, 2, 1);
    CHECK(rate != nullptr);
    if (rate != nullptr) {
        CHECK(!rate->integer_div);   // ÷10⁷ 은 실수 경로다
    }
    // 같은 클래스·같은 종류인데 `+0x3A` 로 나눗셈 갈래까지 달라진다.
    const BuffParamRule* rate0 = buff_param_rule(kVtDefStatRate, 2, 0);
    CHECK(rate0 != nullptr);
    if (rate0 != nullptr) {
        CHECK(rate0->integer_div);
    }
    // 표가 통째로 한쪽으로 쏠리면(생성기가 갈래를 못 읽은 것이다) 여기서 깨진다.
    std::size_t ints = 0;
    for (const BuffParamRule& r : kBuffParamRules) {
        if (r.integer_div) ++ints;
    }
    CHECK(ints > 0);
    CHECK(ints < buff_param_rule_count());
}

TEST(buff_param_table_rows_are_sorted_and_unique) {
    // 오름차순이 아니면 같은 키가 두 줄 들어가도 안 보인다.
    CHECK(buff_param_rule_count() > 0);
    for (std::size_t i = 1; i < buff_param_rule_count(); ++i) {
        const BuffParamRule& a = kBuffParamRules[i - 1];
        const BuffParamRule& b = kBuffParamRules[i];
        const bool ordered =
            a.vtable_va < b.vtable_va ||
            (a.vtable_va == b.vtable_va &&
             (a.param_type < b.param_type ||
              (a.param_type == b.param_type && a.flag3a < b.flag3a)));
        CHECK(ordered);
    }
}

TEST(buff_param_table_rows_are_well_formed) {
    for (const BuffParamRule& r : kBuffParamRules) {
        // BuffData 의 값 칸은 전부 +0x90 위다. 0 이나 작은 값은 뽑기 실패의 흔적이다.
        CHECK(r.offset >= 0x90 && r.offset <= 0x200);
        CHECK(r.divisor > 0.0);
        CHECK(r.flag3a <= 1);
        CHECK(r.note != nullptr && r.note[0] != '\0');
        // vtable 은 모듈 고정 VA 다(기준 0x140000000).
        CHECK(r.vtable_va > 0x140000000ull && r.vtable_va < 0x150000000ull);
    }
}

TEST(buff_param_table_is_usable_at_compile_time) {
    // 스냅샷 조립은 배경 스레드에서 돈다 — 조회가 실행시간 초기화에 기대면 안 된다.
    // 이름 조회도 컴파일 시간이어야 한다 - 아니면 위 상수들이 실행시간 초기화가
    // 되어 이 static_assert 자체가 안 선다.
    static_assert(kVtStatMax != 0);
    static_assert(buff_param_divisor(kVtStatMax, 1, 0) == 1000.0);
    static_assert(buff_param_offset(kVtStatMax, 1, 0) == 0x98);
    static_assert(buff_param_divisor(kVtDefStatRate, 2, 0) == 1000.0);
    static_assert(buff_param_divisor(kVtDefStatRate, 2, 1) == 10000000.0);
    static_assert(buff_param_divisor(0x1ull, 1, 0) == 0.0);
    CHECK(true);
}

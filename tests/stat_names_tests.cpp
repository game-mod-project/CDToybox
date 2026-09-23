#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>

#include "fake_memory.h"
#include "game/effect_format.h"
#include "game/localization.h"
#include "game/stat_names.h"
#include "harness.h"
#include "mem/rtti.h"

// `{Staticinfo:SubLevel:Hp}` -> `생명` 치환표를 가짜 메모리 위에서 못박는다.
//
// 사슬(명세 §4.7-H' 1번, 2026-09-23 실측):
//   SubLevelInfo(+0x46) · StatusInfo(+0x34) = 지식 행 -> KnowledgeInfo(+0x08)
//   문자열 객체의 **해시**(+0x0C) -> 현지화 필드 0x490 = 표시 이름
//
// 표본은 실측 여섯이다. 다만 기력 줄은 표에 `Sp` 로 적혀 있는데 그것은 **지식**의
// 내부 이름(`Knowledge_Sp`)이고, 형식 문자열이 실제로 쓰는 토큰은
// `{Staticinfo:SubLevel:Stamina}` 다(R1 실측 - 행 17). 그래서 SubLevel 쪽 키는
// `Stamina`, 지식 쪽 이름은 `Knowledge_Sp` 로 세운다.

namespace {

using cdtb::game::LocSystem;
using cdtb::game::StatNames;
using cdtb::tests::FakeMemory;

// 힙 배치. 지식 레코드 포인터 배열이 6713행이라 뒤쪽에 넉넉히 둔다.
struct Fixture {
    FakeMemory mem;

    static constexpr std::size_t kSubMgr = 0x00000;
    static constexpr std::size_t kStatusMgr = 0x00080;
    static constexpr std::size_t kKnowMgr = 0x00100;
    static constexpr std::size_t kSubIndex = 0x00200;
    static constexpr std::size_t kStatusIndex = 0x00280;
    static constexpr std::size_t kSubPtrs = 0x00300;
    static constexpr std::size_t kStatusPtrs = 0x00380;
    static constexpr std::size_t kSubRecords = 0x00400;
    static constexpr std::size_t kStatusRecords = 0x00800;
    static constexpr std::size_t kRecStride = 0x80;
    static constexpr std::size_t kStatStrObjs = 0x00A00;
    static constexpr std::size_t kStatStrChars = 0x00C00;
    static constexpr std::size_t kKnowStrObjs = 0x00E00;
    static constexpr std::size_t kKnowStrChars = 0x01000;
    static constexpr std::size_t kKnowRecords = 0x01200;
    static constexpr std::size_t kStrStride = 0x20;
    static constexpr std::size_t kKnowRecStride = 0x40;
    static constexpr std::size_t kLocSys = 0x02000;
    static constexpr std::size_t kLocCats = 0x02100;
    static constexpr std::size_t kLocPtrs = 0x02500;
    static constexpr std::size_t kLocEntries = 0x02600;
    static constexpr std::size_t kLocPool = 0x02800;
    static constexpr std::uint32_t kLocPoolSize = 0x400;
    static constexpr std::size_t kKnowPtrs = 0x08000;

    // 지식 매니저가 든 지식 수. 실측값 그대로다.
    static constexpr std::uint32_t kKnowCount = 6713;
    // 지식 이름이 사는 현지화 카테고리(실측 cat 9). `resolve` 는 전부 훑으므로
    // 구현이 이 값을 알 필요는 없다 - 표를 세울 때만 쓴다.
    static constexpr int kLocCategory = 9;

    // --- 표본 ---
    struct StatRow {
        const char* key;          // _stringKey
        std::uint16_t know_row;   // 0xFFFF = 지식 없음
    };
    // SubLevel(+0x46). 마지막 행은 지식이 없어 표에 안 들어간다.
    static constexpr StatRow kSub[] = {
        {"Hp", 0},                  // **행 0 은 유효한 행이다**
        {"Mp", 5085},
        {"Stamina", 5084},
        {"AttackSpeedRate", 5},
        {"CriticalRate", 10},
        {"Exp", 0xFFFF},
    };
    // Status(+0x34). 둘째 행의 지식은 현지화에 이름이 없어 건너뛴다.
    static constexpr StatRow kStatus[] = {
        {"IceResistance", 5102},
        {"Dpv", 5100},
    };

    struct KnowRow {
        std::uint16_t row;
        const char* internal;     // KnowledgeInfo +0x08 의 문자열
        std::uint32_t hash;       // 그 문자열 객체 +0x0C - 현지화 엔티티다
        const char* label;        // 현지화에 없으면 널
    };
    static constexpr KnowRow kKnow[] = {
        {0, "Knowledge_Hp", 0x11111111u, "생명"},
        {5, "Knowledge_AttackSpeedRate", 0x22222222u, "공격 속도"},
        {10, "Knowledge_CriticalRate", 0x33333333u, "치명타 확률"},
        {5084, "Knowledge_Sp", 0x44444444u, "기력"},
        {5085, "Knowledge_Mp", 0x55555555u, "용기"},
        {5102, "Knowledge_IceResistance", 0x66666666u, "냉기 저항"},
        {5100, "Knowledge_Unlisted", 0x77777777u, nullptr},
    };

    Fixture() {
        mem.heap.assign(0x18000, 0);
        build_stat_table(kSubMgr, kSubIndex, kSubPtrs, kSubRecords, kSub,
                         std::size(kSub), cdtb::game::kSubLevelKnowRow, 0);
        build_stat_table(kStatusMgr, kStatusIndex, kStatusPtrs, kStatusRecords,
                         kStatus, std::size(kStatus),
                         cdtb::game::kStatusKnowRow, std::size(kSub));
        build_knowledge();
        build_localization();
    }

    // 엔진 문자열 객체 {char* +0x00, u32 길이 +0x08, u32 해시 +0x0C}.
    void put_engine_string(std::size_t obj, std::size_t chars, const char* s,
                           std::uint32_t hash) {
        mem.put_u64(obj + 0x00, mem.heap_addr(chars));
        mem.put_u32(obj + 0x08, static_cast<std::uint32_t>(std::strlen(s)));
        mem.put_u32(obj + 0x0C, hash);
        mem.put_str(chars, s);
    }

    // StaticInfoManager2 배치: +0x28 색인 · +0x30 개수 · +0x58 레코드 배열.
    void build_stat_table(std::size_t mgr, std::size_t index, std::size_t ptrs,
                          std::size_t records, const StatRow* rows,
                          std::size_t n, std::size_t know_row_off,
                          std::size_t str_slot_base) {
        mem.put_u64(mgr + 0x28, mem.heap_addr(index));
        mem.put_u32(mgr + 0x30, static_cast<std::uint32_t>(n));
        mem.put_u64(mgr + 0x58, mem.heap_addr(ptrs));
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint32_t key = static_cast<std::uint32_t>(i + 1);
            mem.put_u32(index + i * 8 + 0, key);
            const std::size_t rec = records + i * kRecStride;
            mem.put_u64(ptrs + i * 8, mem.heap_addr(rec));
            mem.put_u32(rec + 0x00, key);
            mem.put_u16(rec + know_row_off, rows[i].know_row);
            const std::size_t slot = str_slot_base + i;
            const std::size_t obj = kStatStrObjs + slot * kStrStride;
            mem.put_u64(rec + cdtb::game::kStatRecStringKey, mem.heap_addr(obj));
            // 스탯 표의 해시는 안 쓴다 - 지식 쪽 해시만 현지화 엔티티다.
            put_engine_string(obj, kStatStrChars + slot * kStrStride,
                              rows[i].key, 0);
        }
    }

    // KnowledgeInfoManager: +0x08 개수 · +0x58 레코드 포인터 배열.
    void build_knowledge() {
        mem.put_u32(kKnowMgr + 0x08, kKnowCount);
        mem.put_u64(kKnowMgr + 0x58, mem.heap_addr(kKnowPtrs));
        for (std::size_t i = 0; i < std::size(kKnow); ++i) {
            const std::size_t rec = kKnowRecords + i * kKnowRecStride;
            mem.put_u64(kKnowPtrs + kKnow[i].row * 8, mem.heap_addr(rec));
            const std::size_t obj = kKnowStrObjs + i * kStrStride;
            mem.put_u64(rec + 0x08, mem.heap_addr(obj));
            put_engine_string(obj, kKnowStrChars + i * kStrStride,
                              kKnow[i].internal, kKnow[i].hash);
        }
    }

    // 현지화: 시스템 +0x48 카테고리 표 · +0x58 풀 · +0x60 풀 크기.
    // 항목은 +0x10 키 · +0x18 풀 오프셋이고 **키 오름차순**이어야 이분 탐색이 산다.
    void build_localization() {
        mem.put_u64(kLocSys + 0x48, mem.heap_addr(kLocCats));
        mem.put_u64(kLocSys + 0x58, mem.heap_addr(kLocPool));
        mem.put_u32(kLocSys + 0x60, kLocPoolSize);

        std::uint32_t n = 0;
        for (const KnowRow& k : kKnow) {
            if (k.label == nullptr) continue;   // 이름이 없는 지식은 표에 없다
            const std::size_t e = kLocEntries + n * 0x20;
            mem.put_u64(kLocPtrs + n * 8, mem.heap_addr(e));
            mem.put_u64(e + 0x10, cdtb::game::loc_key(
                                      k.hash, cdtb::game::kKnowledgeNameField));
            mem.put_u32(e + 0x18, static_cast<std::uint32_t>(n * 0x40));
            mem.put_u8(e + 0x1C, static_cast<std::uint8_t>(kLocCategory));
            mem.put_str(kLocPool + n * 0x40, k.label);
            ++n;
        }
        mem.put_u64(kLocCats + kLocCategory * 16 + 0, mem.heap_addr(kLocPtrs));
        mem.put_u32(kLocCats + kLocCategory * 16 + 8, n);
    }

    std::uintptr_t sub_manager() const { return mem.heap_addr(kSubMgr); }
    std::uintptr_t status_manager() const { return mem.heap_addr(kStatusMgr); }
    std::uintptr_t know_manager() const { return mem.heap_addr(kKnowMgr); }

    LocSystem loc() const {
        LocSystem s;
        s.object = mem.heap_addr(kLocSys);
        s.pool = mem.heap_addr(kLocPool);
        s.pool_size = kLocPoolSize;
        return s;
    }

    // 표를 다 갖춘 채로 만든다.
    bool build(StatNames* names) const {
        return names->build_from_managers(mem, loc(), sub_manager(),
                                          status_manager(), know_manager());
    }
};

}  // namespace

// ------------------------------------------------------------------ 치환 사슬

TEST(stat_names_resolves_sublevel_through_knowledge) {
    Fixture f;
    StatNames names;
    CHECK(f.build(&names));
    CHECK_EQ(names.name_of("SubLevel", "Hp"), std::string("생명"));
    CHECK_EQ(names.name_of("SubLevel", "Mp"), std::string("용기"));
    CHECK_EQ(names.name_of("SubLevel", "Stamina"), std::string("기력"));
}

TEST(stat_names_resolves_status_through_active_knowledge) {
    // Status 는 지식 행이 `_activeKnowledgeInfo`(+0x34)에 있다.
    Fixture f;
    StatNames names;
    CHECK(f.build(&names));
    CHECK_EQ(names.name_of("Status", "IceResistance"), std::string("냉기 저항"));
}

TEST(stat_names_covers_the_measured_samples) {
    // 실측 여섯 줄이 전부 나오고, 그 밖의 행은 안 들어간다.
    Fixture f;
    StatNames names;
    CHECK(f.build(&names));
    CHECK_EQ(names.name_of("SubLevel", "AttackSpeedRate"), std::string("공격 속도"));
    CHECK_EQ(names.name_of("SubLevel", "CriticalRate"), std::string("치명타 확률"));
    CHECK_EQ(names.size(), static_cast<std::size_t>(6));
}

TEST(stat_names_treats_knowledge_row_zero_as_valid) {
    // 지식 행 0(Knowledge_Hp)은 "없음" 이 아니다. 없음은 0xFFFF 뿐이다.
    Fixture f;
    StatNames names;
    CHECK(f.build(&names));
    CHECK_EQ(names.name_of("SubLevel", "Hp"), std::string("생명"));
    CHECK(names.name_of("SubLevel", "Exp").empty());   // 0xFFFF 행
}

TEST(stat_names_skips_rows_whose_name_does_not_resolve) {
    // 지식은 있는데 현지화에 이름이 없는 행(Status:Dpv). 그 행만 빠지고
    // 나머지는 그대로 산다 - 전체 실패로 만들지 않는다.
    Fixture f;
    StatNames names;
    CHECK(f.build(&names));
    CHECK(names.name_of("Status", "Dpv").empty());
    CHECK_EQ(names.name_of("Status", "IceResistance"), std::string("냉기 저항"));
}

TEST(stat_names_returns_empty_for_unknown_key) {
    Fixture f;
    StatNames names;
    CHECK(f.build(&names));
    CHECK(names.name_of("SubLevel", "NoSuchStat").empty());
    CHECK(names.name_of("NoSuchTable", "Hp").empty());
    CHECK(names.name_of("Status", "Hp").empty());   // 표가 다르면 안 걸린다
    CHECK(names.name_of("", "").empty());
}

TEST(stat_names_is_empty_before_build) {
    StatNames names;
    CHECK_EQ(names.size(), static_cast<std::size_t>(0));
    CHECK(names.name_of("SubLevel", "Hp").empty());
}

// ------------------------------------------------------------------ 실패 경로

TEST(stat_names_build_fails_when_a_manager_is_missing) {
    Fixture f;
    // SubLevel 이 없다
    StatNames a;
    CHECK(!a.build_from_managers(f.mem, f.loc(), 0, f.status_manager(),
                                 f.know_manager()));
    CHECK_EQ(a.size(), static_cast<std::size_t>(0));
    // Status 가 없다
    StatNames b;
    CHECK(!b.build_from_managers(f.mem, f.loc(), f.sub_manager(), 0,
                                 f.know_manager()));
    CHECK_EQ(b.size(), static_cast<std::size_t>(0));
    // 지식 매니저가 없다
    StatNames c;
    CHECK(!c.build_from_managers(f.mem, f.loc(), f.sub_manager(),
                                 f.status_manager(), 0));
    CHECK_EQ(c.size(), static_cast<std::size_t>(0));
}

TEST(stat_names_build_fails_when_the_knowledge_table_is_not_up_yet) {
    // 매니저는 있는데 개수·배열이 말이 안 된다(아직 안 찼다).
    Fixture f;
    f.mem.put_u32(Fixture::kKnowMgr + 0x08, 0);
    StatNames names;
    CHECK(!f.build(&names));
    CHECK_EQ(names.size(), static_cast<std::size_t>(0));
}

TEST(stat_names_build_fails_without_localization) {
    Fixture f;
    StatNames names;
    CHECK(!names.build_from_managers(f.mem, LocSystem{}, f.sub_manager(),
                                     f.status_manager(), f.know_manager()));
    CHECK_EQ(names.size(), static_cast<std::size_t>(0));
}

TEST(stat_names_build_fails_when_no_name_resolves_yet) {
    // 현지화 풀은 섰는데 카테고리가 아직 비었다 - 조용히 빈 표 + false 라야
    // 부르는 쪽이 재시도한다(staged-data-load-retry).
    Fixture f;
    f.mem.put_u32(Fixture::kLocCats + Fixture::kLocCategory * 16 + 8, 0);
    StatNames names;
    CHECK(!f.build(&names));
    CHECK_EQ(names.size(), static_cast<std::size_t>(0));
}

TEST(stat_names_keeps_the_old_table_when_a_rebuild_fails) {
    // 한 번 잘 만든 표는 실패한 재시도가 지우지 않는다.
    Fixture f;
    StatNames names;
    CHECK(f.build(&names));
    CHECK_EQ(names.size(), static_cast<std::size_t>(6));
    CHECK(!names.build_from_managers(f.mem, f.loc(), f.sub_manager(),
                                     f.status_manager(), 0));
    CHECK_EQ(names.size(), static_cast<std::size_t>(6));
    CHECK_EQ(names.name_of("SubLevel", "Hp"), std::string("생명"));
}

TEST(stat_names_build_finds_nothing_without_rtti_or_globals) {
    // RTTI 로 찾는 쪽. 가짜 모듈에는 지식 전역도 매니저 클래스도 없다.
    Fixture f;
    f.mem.image.assign(0x1000, 0);
    cdtb::mem::Rtti rt(f.mem);
    StatNames names;
    CHECK(!names.build(rt, f.mem, f.loc()));
    CHECK_EQ(names.size(), static_cast<std::size_t>(0));
}

// -------------------------------------------------------------- 효과 문구 연결

TEST(stat_names_feeds_effect_line) {
    // Task 2 의 포맷터가 받는 콜백이 곧 name_of 다. 실측 골든 줄을 그대로 낸다.
    Fixture f;
    StatNames names;
    CHECK(f.build(&names));
    const auto name_of = [&names](std::string_view table, std::string_view key) {
        return names.name_of(table, key);
    };
    cdtb::game::EffectValues v;
    v.param = 45.0;
    v.duration_ms = 90000;
    CHECK_EQ(cdtb::game::effect_line("{Staticinfo:SubLevel:Hp} 최대치 {Param1} 증가",
                                     v, name_of),
             std::string("생명 최대치 45 증가(1분)"));

    // 이름표에 없는 스탯은 토큰을 그대로 둔다(빈 자리를 안 남긴다).
    cdtb::game::EffectValues w;
    CHECK_EQ(cdtb::game::effect_line("{Staticinfo:SubLevel:NoSuchStat} 증가", w,
                                     name_of),
             std::string("{Staticinfo:SubLevel:NoSuchStat} 증가"));
}

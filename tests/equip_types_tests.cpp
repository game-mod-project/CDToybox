#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "fake_memory.h"
#include "game/equip_types.h"
#include "game/localization.h"
#include "harness.h"
#include "mem/rtti.h"

// `_equipAbleHash` -> `EquipTypeInfo` 행 이름 사슬을 가짜 메모리 위에서 못박는다.
//
// 사슬(명세 §4.7-F, 2026-09-23 R2 실측):
//   EquipTypeInfoManager(+0x08 개수 · +0x58 레코드 배열)
//     레코드 `_equipAbleHashList`(+0x48, 벡터 {ptr, u32 개수, u32 용량}) 에
//     해시가 들어 있으면 그 행이 대상 -> `_equipTypeName` 키(+0x60, 완성된
//     현지화 키) 로 이름을 푼다.

namespace {

using cdtb::game::LocSystem;
using cdtb::tests::FakeMemory;

struct Fixture {
    FakeMemory mem;

    static constexpr std::size_t kMgr = 0x0000;
    static constexpr std::size_t kRecordPtrs = 0x0100;
    static constexpr std::size_t kRecords = 0x0400;
    static constexpr std::size_t kRecStride = 0x80;
    static constexpr std::size_t kHashArrays = 0x2000;
    static constexpr std::size_t kHashArrayStride = 0x40;
    static constexpr std::size_t kLocSys = 0x4000;
    static constexpr std::size_t kLocCats = 0x4100;
    static constexpr std::size_t kLocPtrs = 0x4500;
    static constexpr std::size_t kLocEntries = 0x4600;
    static constexpr std::size_t kLocPool = 0x4C00;
    static constexpr std::uint32_t kLocPoolSize = 0x800;
    // 실측 cat 46 과 같은 번호를 쓴다 - `resolve` 는 전부 훑으므로 시험 결과에는
    // 영향이 없지만, 근거 문서와 맞춰 둔다.
    static constexpr int kLocCategory = 46;

    static constexpr std::uint32_t kTargetHash = 0xAAAA1111u;
    static constexpr std::uint32_t kOtherHash = 0xBBBB2222u;

    struct Row {
        // 널이면 이 행의 이름을 현지화에 넣지 않는다(안 풀림 시험용).
        const char* name;
        std::vector<std::uint32_t> hashes;
    };

    // 행 0·1·2·4 는 목표 해시가 들어 있다. 행 3 은 다른 해시만 있어 안 걸린다.
    // 행 2 와 4 는 이름이 같다("신발") - 게임 데이터가 그렇다(신발이 2행이다,
    // 지우지 않고 그대로 둔다). 행 5 는 해시는 걸리지만 이름이 현지화에 없다
    // (그 행만 건너뛴다 - 전체 실패가 아니다).
    const std::vector<Row> rows = {
        {"한손검", {kTargetHash, 0x11112222u}},
        {"장갑", {0x33334444u, kTargetHash}},
        {"신발", {kTargetHash}},
        {"투구", {kOtherHash}},
        {"신발", {kTargetHash, 0x55556666u}},
        {nullptr, {kTargetHash}},
    };

    Fixture() {
        mem.heap.assign(0x6000, 0);
        build_manager();
        build_localization();
    }

    static std::uint64_t name_key(std::size_t row) {
        return cdtb::game::loc_key(0x90000000u + static_cast<std::uint32_t>(row),
                                   cdtb::game::kEquipTypeNameField);
    }

    void build_manager() {
        mem.put_u32(kMgr + cdtb::game::kEquipTypeMgrCount,
                   static_cast<std::uint32_t>(rows.size()));
        mem.put_u64(kMgr + cdtb::game::kEquipTypeMgrRecords,
                   mem.heap_addr(kRecordPtrs));

        for (std::size_t i = 0; i < rows.size(); ++i) {
            const std::size_t rec = kRecords + i * kRecStride;
            mem.put_u64(kRecordPtrs + i * 8, mem.heap_addr(rec));

            const std::size_t harr = kHashArrays + i * kHashArrayStride;
            for (std::size_t j = 0; j < rows[i].hashes.size(); ++j) {
                mem.put_u32(harr + j * 4, rows[i].hashes[j]);
            }
            mem.put_u64(rec + cdtb::game::kEquipAbleHashList, mem.heap_addr(harr));
            mem.put_u32(rec + cdtb::game::kEquipAbleHashCount,
                       static_cast<std::uint32_t>(rows[i].hashes.size()));

            // 이름이 없는 시험행도 **키는 있다**(현지화에 못 찾는 상황을 흉내).
            mem.put_u64(rec + cdtb::game::kEquipTypeNameKey, name_key(i));
        }
    }

    void build_localization() {
        mem.put_u64(kLocSys + 0x48, mem.heap_addr(kLocCats));
        mem.put_u64(kLocSys + 0x58, mem.heap_addr(kLocPool));
        mem.put_u32(kLocSys + 0x60, kLocPoolSize);

        struct Entry {
            std::uint64_t key;
            const char* text;
        };
        std::vector<Entry> entries;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].name == nullptr) continue;   // 안 풀림 시험행은 뺀다
            entries.push_back({name_key(i), rows[i].name});
        }
        // 이분 탐색이 살려면 **키 오름차순**이어야 한다.
        std::sort(entries.begin(), entries.end(),
                 [](const Entry& a, const Entry& b) { return a.key < b.key; });

        for (std::size_t n = 0; n < entries.size(); ++n) {
            const std::size_t e = kLocEntries + n * 0x20;
            mem.put_u64(kLocPtrs + n * 8, mem.heap_addr(e));
            mem.put_u64(e + 0x10, entries[n].key);
            mem.put_u32(e + 0x18, static_cast<std::uint32_t>(n * 0x40));
            mem.put_u8(e + 0x1C, static_cast<std::uint8_t>(kLocCategory));
            mem.put_str(kLocPool + n * 0x40, entries[n].text);
        }
        mem.put_u64(kLocCats + kLocCategory * 16 + 0, mem.heap_addr(kLocPtrs));
        mem.put_u32(kLocCats + kLocCategory * 16 + 8,
                   static_cast<std::uint32_t>(entries.size()));
    }

    std::uintptr_t manager() const { return mem.heap_addr(kMgr); }

    LocSystem loc() const {
        LocSystem s;
        s.object = mem.heap_addr(kLocSys);
        s.pool = mem.heap_addr(kLocPool);
        s.pool_size = kLocPoolSize;
        return s;
    }
};

// 문서 예("한손검 · 낫 · 장갑 · 신발 외 31종")와 같은 모양의 35개짜리 목록.
// 앞 네 개만 문구에 나오므로 이름이 맞아야 하고, 나머지 31개는 내용이 중요치
// 않다(개수만 잰다).
std::vector<std::string> weapon_glove_shoe_35() {
    std::vector<std::string> v = {"한손검", "낫", "장갑", "신발"};
    for (int i = 0; i < 31; ++i) {
        v.push_back("무기" + std::to_string(i));
    }
    return v;
}

}  // namespace

// ------------------------------------------------------------ equip_type_names

TEST(equip_type_names_selects_only_rows_containing_the_hash) {
    Fixture f;
    const auto names = cdtb::game::equip_type_names_from_manager(
        f.mem, f.loc(), f.manager(), Fixture::kTargetHash);
    // 행 0·1·2·4 만 해시가 걸린다. 행 3(투구)은 다른 해시라 안 나오고,
    // 행 5 는 해시는 걸리지만 이름이 안 풀려 빠진다(아래 시험에서 따로 본다).
    const std::vector<std::string> expect = {"한손검", "장갑", "신발", "신발"};
    CHECK(names == expect);
}

TEST(equip_type_names_skips_rows_whose_name_does_not_resolve) {
    Fixture f;
    const auto names = cdtb::game::equip_type_names_from_manager(
        f.mem, f.loc(), f.manager(), Fixture::kTargetHash);
    // 행 5 는 해시가 걸리지만 현지화에 이름이 없다 - 결과 개수(4)에 없어야
    // 하고, 그렇다고 전체 호출이 실패하는 것도 아니다(나머지는 그대로 나온다).
    CHECK_EQ(names.size(), static_cast<std::size_t>(4));
    CHECK(std::find(names.begin(), names.end(), std::string()) == names.end());
}

TEST(equip_type_names_keeps_duplicate_names_across_rows) {
    // 신발이 행 2·4 둘이다 - 게임 데이터가 그렇다. 지우지 않고 그대로 둔다.
    Fixture f;
    const auto names = cdtb::game::equip_type_names_from_manager(
        f.mem, f.loc(), f.manager(), Fixture::kTargetHash);
    CHECK_EQ(std::count(names.begin(), names.end(), std::string("신발")),
            static_cast<std::ptrdiff_t>(2));
}

TEST(equip_type_names_returns_empty_for_zero_hash) {
    Fixture f;
    const auto names = cdtb::game::equip_type_names_from_manager(
        f.mem, f.loc(), f.manager(), 0);
    CHECK(names.empty());
}

TEST(equip_type_names_returns_empty_without_manager) {
    Fixture f;
    const auto names = cdtb::game::equip_type_names_from_manager(
        f.mem, f.loc(), 0, Fixture::kTargetHash);
    CHECK(names.empty());
}

TEST(equip_type_names_returns_empty_without_localization) {
    Fixture f;
    const auto names = cdtb::game::equip_type_names_from_manager(
        f.mem, LocSystem{}, f.manager(), Fixture::kTargetHash);
    CHECK(names.empty());
}

TEST(equip_type_names_returns_empty_when_no_row_matches) {
    // 어느 행의 목록에도 없는 해시. `kOtherHash`(행 3)와도 다르다 - 걸리는
    // 행이 하나도 없어야 한다.
    Fixture f;
    const auto names = cdtb::game::equip_type_names_from_manager(
        f.mem, f.loc(), f.manager(), 0xCCCCCCCCu);
    CHECK(names.empty());
}

TEST(equip_type_names_finds_nothing_without_rtti_or_manager) {
    // RTTI 로 찾는 쪽. 가짜 모듈에는 매니저 클래스가 없다 - stat_names 의
    // `build_finds_nothing_without_rtti_or_globals` 와 같은 이유.
    Fixture f;
    f.mem.image.assign(0x1000, 0);
    cdtb::mem::Rtti rtti(f.mem);
    const auto names = cdtb::game::equip_type_names(rtti, f.mem, f.loc(),
                                                     Fixture::kTargetHash);
    CHECK(names.empty());
}

// -------------------------------------------------------------- equip_types_line

TEST(equip_types_line_returns_empty_for_empty_list) {
    CHECK(cdtb::game::equip_types_line({}).empty());
    CHECK(cdtb::game::equip_types_line({}, 0).empty());
}

TEST(equip_types_line_joins_without_suffix_when_within_max_shown) {
    const std::vector<std::string> names = {"한손검", "장갑"};
    CHECK_EQ(cdtb::game::equip_types_line(names), std::string("한손검 · 장갑"));
}

TEST(equip_types_line_does_not_append_suffix_at_exactly_max_shown) {
    // 정확히 max_shown 개면 "외" 가 안 붙는다.
    const std::vector<std::string> names = {"A", "B", "C", "D"};
    CHECK_EQ(cdtb::game::equip_types_line(names, 4), std::string("A · B · C · D"));
}

TEST(equip_types_line_appends_count_when_over_max_shown) {
    const auto names = weapon_glove_shoe_35();
    CHECK_EQ(cdtb::game::equip_types_line(names, 4),
            std::string("한손검 · 낫 · 장갑 · 신발 외 31종"));
}

TEST(equip_types_line_with_max_shown_zero_reports_count_only) {
    const auto names = weapon_glove_shoe_35();
    CHECK_EQ(cdtb::game::equip_types_line(names, 0), std::string("35종"));
    const std::vector<std::string> small = {"장갑", "신발"};
    CHECK_EQ(cdtb::game::equip_types_line(small, 0), std::string("2종"));
}

TEST(equip_types_line_keeps_duplicate_names) {
    const std::vector<std::string> names = {"신발", "신발"};
    CHECK_EQ(cdtb::game::equip_types_line(names), std::string("신발 · 신발"));
}

#include "game/equip_types.h"

#include <algorithm>

#include "game/roster.h"  // find_static_manager

namespace cdtb::game {
namespace {

// 매니저 머리(개수 · 레코드 포인터 배열). item_effects.cpp 의 같은 이름 구조체와
// 같은 이유 - 이 매니저는 아이템 매니저와 달리 개수가 `+0x08` 이다.
struct Table {
    std::uint32_t count = 0;
    std::uintptr_t records = 0;
};

bool read_table(const mem::Reader& r, std::uintptr_t manager, Table* out) {
    std::uint32_t n = 0;
    std::uint64_t recs = 0;
    if (!r.read_value(manager + kEquipTypeMgrCount, &n)) return false;
    if (!r.read_value(manager + kEquipTypeMgrRecords, &recs)) return false;
    // 매니저를 잘못 집으면 개수가 쓰레기값이다 - 그대로 믿고 돌면 안 끝난다.
    if (n == 0 || n > kMaxEquipTypeRows || recs == 0) return false;
    out->count = n;
    out->records = static_cast<std::uintptr_t>(recs);
    return true;
}

// 레코드 포인터 배열에서 한 행. 범위 밖이거나 널이면 0.
std::uintptr_t record_at(const mem::Reader& r, const Table& t,
                         std::uint32_t row) {
    if (row >= t.count) return 0;
    std::uint64_t rec = 0;
    if (!r.read_value(t.records + static_cast<std::uintptr_t>(row) * 8,
                      &rec)) {
        return 0;
    }
    return static_cast<std::uintptr_t>(rec);
}

// 이 행의 `_equipAbleHashList` 에 해시가 들어 있는가. 목록의 [1]번째가 그 행
// 자신의 이름 엔티티라도(명세 §4.7-F) 우리는 "들어 있는가" 만 보므로 그대로
// 둔다 - 걸러낼 필요가 없다.
bool row_has_hash(const mem::Reader& r, std::uintptr_t record,
                  std::uint32_t hash) {
    std::uint64_t list = 0;
    std::uint32_t n = 0;
    if (!r.read_value(record + kEquipAbleHashList, &list) || list == 0) {
        return false;
    }
    if (!r.read_value(record + kEquipAbleHashCount, &n) || n == 0 ||
        n > kMaxEquipHashListLen) {
        return false;   // 목록이 없거나 길이가 말이 안 된다 - 이 행은 대상이 아니다
    }
    for (std::uint32_t i = 0; i < n; ++i) {
        std::uint32_t v = 0;
        if (!r.read_value(static_cast<std::uintptr_t>(list) +
                              i * kEquipAbleHashStride,
                          &v)) {
            continue;   // 한 칸 못 읽어도 나머지는 계속 본다
        }
        if (v == hash) return true;
    }
    return false;
}

// 행의 부위 이름. 못 풀면 빈 문자열(그 행만 건너뛴다 - 전체 실패가 아니다).
std::string row_name(const mem::Reader& r, const LocSystem& loc,
                     std::uintptr_t record) {
    std::uint64_t key = 0;
    if (!r.read_value(record + kEquipTypeNameKey, &key) || key == 0) {
        return {};
    }
    std::string text;
    if (!resolve(r, loc, key, &text, nullptr) || text.empty()) return {};
    return text;
}

}  // namespace

std::vector<std::string> equip_type_names_from_manager(
    const mem::Reader& reader, const LocSystem& loc, std::uintptr_t manager,
    std::uint64_t equip_able_hash) {
    std::vector<std::string> out;
    if (equip_able_hash == 0) return out;   // 없음 - 장착 제한이 없는 아이템
    if (manager == 0) return out;
    if (!loc.valid()) return out;

    Table t;
    if (!read_table(reader, manager, &t)) return out;

    const auto hash = static_cast<std::uint32_t>(equip_able_hash);
    for (std::uint32_t row = 0; row < t.count; ++row) {
        const std::uintptr_t rec = record_at(reader, t, row);
        if (rec == 0) continue;   // 빈 슬롯. 나머지는 계속 본다.
        if (!row_has_hash(reader, rec, hash)) continue;

        std::string name = row_name(reader, loc, rec);
        if (name.empty()) continue;   // 이름이 안 풀렸다 - 이 행만 건너뛴다
        out.push_back(std::move(name));
    }
    return out;
}

std::vector<std::string> equip_type_names(const mem::Rtti& rtti,
                                          const mem::Reader& reader,
                                          const LocSystem& loc,
                                          std::uint64_t equip_able_hash) {
    // 싼 관문부터 본다 - RTTI 힙 훑기가 제일 비싸다.
    if (equip_able_hash == 0) return {};
    if (!loc.valid()) return {};

    std::uintptr_t manager = 0;
    if (!find_static_manager(reader, rtti, kEquipTypeManagerClass, &manager)) {
        return {};
    }
    return equip_type_names_from_manager(reader, loc, manager,
                                         equip_able_hash);
}

std::string equip_types_line(const std::vector<std::string>& names,
                             std::size_t max_shown) {
    if (names.empty()) return {};
    if (max_shown == 0) {
        return std::to_string(names.size()) + "종";
    }

    std::string out;
    const std::size_t shown = std::min(names.size(), max_shown);
    for (std::size_t i = 0; i < shown; ++i) {
        if (i != 0) out += " · ";
        out += names[i];
    }
    if (names.size() > max_shown) {
        out += " 외 ";
        out += std::to_string(names.size() - max_shown);
        out += "종";
    }
    return out;
}

}  // namespace cdtb::game

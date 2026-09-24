#include "game/stat_names.h"

#include <utility>

#include "game/knowledge.h"   // kKnowMgrCount · kKnowMgrArray · know_manager · know_mgr_sane
#include "game/roster.h"      // find_static_manager · roster_header · read_engine_string

namespace cdtb::game {
namespace {

// 표 이름과 내부 키를 한 문자열로 붙인다. 표를 안 섞으려고 붙인다 -
// SubLevel 과 Status 에 같은 이름의 키가 있을 수 있다.
std::string table_key(std::string_view table, std::string_view key) {
    std::string s;
    s.reserve(table.size() + 1 + key.size());
    s.append(table);
    s.push_back(':');
    s.append(key);
    return s;
}

// 지식 행 -> 표시 이름. 못 풀면 빈 문자열(그 행만 건너뛴다).
std::string knowledge_name(const mem::Reader& reader, const LocSystem& loc,
                           const KnowMgr& know, std::uint16_t row) {
    // **0 은 유효한 행이다**(Knowledge_Hp). 위쪽만 막는다.
    if (static_cast<int>(row) >= know.count) return {};
    std::uint64_t info = 0;
    if (!reader.read_value(know.array + static_cast<std::uintptr_t>(row) * 8,
                           &info) ||
        info == 0) {
        return {};
    }
    std::uint64_t str_obj = 0;
    if (!reader.read_value(static_cast<std::uintptr_t>(info) + kInfoInternalName,
                           &str_obj) ||
        str_obj == 0) {
        return {};
    }
    // 이름이 아니라 **그 문자열 객체의 해시**가 현지화 엔티티다.
    std::uint32_t hash = 0;
    if (!reader.read_value(
            static_cast<std::uintptr_t>(str_obj) + kEngineStringHash, &hash) ||
        hash == 0) {
        return {};
    }
    std::string text;
    if (!resolve(reader, loc, loc_key(hash, kKnowledgeNameField), &text,
                 nullptr)) {
        return {};
    }
    return text;
}

// 표 하나를 걷는다. 매니저가 매니저 같지 않으면(개수·배열이 말이 안 되면) false -
// 그때는 잘못 집은 것이라 전체를 실패로 돌린다. 개별 행의 실패는 건너뛴다.
bool walk_table(const mem::Reader& reader, const LocSystem& loc,
                const KnowMgr& know, std::uintptr_t manager,
                std::string_view table, std::size_t know_row_off,
                std::unordered_map<std::string, std::string>* out) {
    std::uint32_t count = 0;
    std::uintptr_t records = 0;
    if (!roster_header(reader, manager, &count, &records)) return false;

    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint64_t rec = 0;
        if (!reader.read_value(records + static_cast<std::uintptr_t>(i) * 8,
                               &rec) ||
            rec == 0) {
            continue;   // 널 슬롯
        }
        const auto record = static_cast<std::uintptr_t>(rec);

        std::uint64_t str_obj = 0;
        if (!reader.read_value(record + kStatRecStringKey, &str_obj) ||
            str_obj == 0) {
            continue;
        }
        const std::string key =
            read_engine_string(reader, static_cast<std::uintptr_t>(str_obj));
        if (key.empty()) continue;

        std::uint16_t row = kNoKnowRow;
        if (!reader.read_value(record + know_row_off, &row)) continue;
        if (row == kNoKnowRow) continue;   // 지식이 안 달린 스탯

        std::string name = knowledge_name(reader, loc, know, row);
        if (name.empty()) continue;

        out->insert_or_assign(table_key(table, key), std::move(name));
    }
    return true;
}

}  // namespace

bool StatNames::build_from_managers(const mem::Reader& reader,
                                    const LocSystem& loc,
                                    std::uintptr_t sub_level_mgr,
                                    std::uintptr_t status_mgr,
                                    std::uintptr_t knowledge_mgr) {
    if (!loc.valid()) return false;                       // 현지화가 아직 없다
    if (sub_level_mgr == 0 || status_mgr == 0) return false;
    if (knowledge_mgr == 0) return false;

    KnowMgr know;
    know.object = knowledge_mgr;
    std::int32_t count = 0;
    std::uint64_t array = 0;
    if (!reader.read_value(knowledge_mgr + kKnowMgrCount, &count)) return false;
    if (!reader.read_value(knowledge_mgr + kKnowMgrArray, &array)) return false;
    know.count = count;
    know.array = static_cast<std::uintptr_t>(array);
    if (!know_mgr_sane(know.count, know.array)) return false;   // 지식 표가 아직 없다

    // 다 모은 뒤에 갈아 끼운다 - 재시도가 실패해도 이미 잘 만든 표를 안 지운다.
    std::unordered_map<std::string, std::string> out;
    if (!walk_table(reader, loc, know, sub_level_mgr, kStatTableSubLevel,
                    kSubLevelKnowRow, &out)) {
        return false;
    }
    if (!walk_table(reader, loc, know, status_mgr, kStatTableStatus,
                    kStatusKnowRow, &out)) {
        return false;
    }
    // 한 줄도 안 풀렸으면 아직 덜 찬 것이다(현지화 풀이 늦게 온다). 성공으로
    // 치면 부르는 쪽이 재시도를 멈춰 이름이 영영 안 붙는다.
    if (out.empty()) return false;

    map_ = std::move(out);
    return true;
}

bool StatNames::build(const mem::Rtti& rtti, const mem::Reader& reader,
                      const LocSystem& loc) {
    // 싼 관문부터 본다 - RTTI 힙 훑기가 제일 비싸다.
    if (!loc.valid()) return false;

    KnowMgr know;
    if (!know_manager(reader, &know)) return false;

    std::uintptr_t sub_level = 0;
    if (!find_static_manager(reader, rtti, kSubLevelManagerClass, &sub_level)) {
        return false;
    }
    std::uintptr_t status = 0;
    if (!find_static_manager(reader, rtti, kStatusManagerClass, &status)) {
        return false;
    }
    return build_from_managers(reader, loc, sub_level, status, know.object);
}

std::string StatNames::name_of(std::string_view table,
                               std::string_view key) const {
    if (table.empty() || key.empty()) return {};
    const auto it = map_.find(table_key(table, key));
    if (it == map_.end()) return {};
    return it->second;
}

}  // namespace cdtb::game

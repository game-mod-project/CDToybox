#include "game/clan.h"

#include <atomic>
#include <utility>

#include "game/roster.h"

namespace cdtb::game {

namespace {

// 서버 쪽이 등록 작업이 쓰는 그 컴포넌트다(고용 작업 0x2ADE280 이
// [[세션+0x68]+0x110] 로 잡는다). 클라 쪽에도 같은 배치가 있으나
// 명부의 주인은 서버 쪽이다.
constexpr char kClanClass[] = ".?AVServerMercenaryClanActorComponent@pa@@";
constexpr char kClanClientClass[] = ".?AVClientMercenaryClanActorComponent@pa@@";

bool read_records_header(const mem::Reader& reader, std::uintptr_t clan,
                         std::uintptr_t* array_out, std::uint32_t* count_out) {
    std::uint64_t arr = 0;
    std::uint32_t count = 0, cap = 0;
    if (!reader.read_value(clan + kClanRecordsOff, &arr) || arr == 0) return false;
    if (!reader.read_value(clan + kClanCountOff, &count)) return false;
    if (!reader.read_value(clan + kClanCapacityOff, &cap)) return false;
    if (count == 0 || count > kClanMaxRecords || cap < count) return false;
    *array_out = static_cast<std::uintptr_t>(arr);
    *count_out = count;
    return true;
}

}  // namespace

bool looks_like_clan_component(const mem::Reader& reader, std::uintptr_t clan) {
    if (clan == 0) return false;
    std::uintptr_t arr = 0;
    std::uint32_t count = 0;
    if (!read_records_header(reader, clan, &arr, &count)) return false;
    // 첫 레코드의 표식을 본다. 22개를 전부 확인했을 때 +0x22 는 예외
    // 없이 0xFFFF 였다.
    std::uint64_t rec = 0;
    if (!reader.read_value(arr, &rec) || rec == 0) return false;
    std::uint16_t guard = 0;
    if (!reader.read_value(static_cast<std::uintptr_t>(rec) + kClanRecordGuard,
                           &guard)) {
        return false;
    }
    return guard == 0xFFFF;
}

namespace {

bool find_clan_of_class(const mem::Reader& reader, const mem::Rtti& rtti,
                        const char* cls, std::uintptr_t* out) {
    std::uintptr_t best = 0;
    std::uint32_t best_n = 0;
    for (const auto addr : rtti.instances_of_class(cls, 16)) {
        if (!looks_like_clan_component(reader, addr)) continue;
        std::uintptr_t arr = 0;
        std::uint32_t count = 0;
        if (!read_records_header(reader, addr, &arr, &count)) continue;
        if (best == 0 || count > best_n) {
            best = addr;
            best_n = count;
        }
    }
    if (best == 0) return false;
    *out = best;
    return true;
}

// 번호로 레코드를 찾는다. 표식(+0x22)까지 확인해야 재활용된 메모리에
// 쓰는 사고를 막는다.
bool find_record_by_no(const mem::Reader& reader, std::uintptr_t clan,
                       std::uint64_t merc_no, std::uintptr_t* rec_out,
                       std::uint16_t* row_out) {
    std::uintptr_t arr = 0;
    std::uint32_t count = 0;
    if (!read_records_header(reader, clan, &arr, &count)) return false;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint64_t rec = 0;
        if (!reader.read_value(arr + static_cast<std::uintptr_t>(i) * 8, &rec) ||
            rec == 0) {
            continue;
        }
        const auto base = static_cast<std::uintptr_t>(rec);
        std::uint16_t guard = 0;
        std::uint64_t no = 0;
        if (!reader.read_value(base + kClanRecordGuard, &guard) ||
            guard != 0xFFFF) {
            continue;
        }
        if (!reader.read_value(base + kClanRecordNo, &no) || no != merc_no) {
            continue;
        }
        std::uint16_t row = 0xFFFF;
        reader.read_value(base + kClanRecordRow, &row);
        *rec_out = base;
        *row_out = row;
        return true;
    }
    return false;
}

}  // namespace

bool resolve_species_write(const mem::Rtti& rtti, const mem::Reader& reader,
                           std::uint64_t merc_no, SpeciesWriteTarget* out) {
    if (out == nullptr || merc_no == 0) return false;
    SpeciesWriteTarget t;
    std::uintptr_t srv = 0, cli = 0;
    if (find_clan_of_class(reader, rtti, kClanClass, &srv)) {
        std::uintptr_t rec = 0;
        std::uint16_t row = 0xFFFF;
        if (find_record_by_no(reader, srv, merc_no, &rec, &row)) {
            t.server = rec + kClanRecordRow;
            t.server_row = row;
        }
    }
    if (find_clan_of_class(reader, rtti, kClanClientClass, &cli)) {
        std::uintptr_t rec = 0;
        std::uint16_t row = 0xFFFF;
        if (find_record_by_no(reader, cli, merc_no, &rec, &row)) {
            t.client = rec + kClanRecordRow;
            t.client_row = row;
        }
    }
    *out = t;
    return t.ok();
}

bool find_clan_component(const mem::Reader& reader, const mem::Rtti& rtti,
                         std::uintptr_t* out) {
    if (out == nullptr) return false;
    // RTTI 후보에 살아있지 않은 것이 섞인다(실측: 2개 중 1개가 주소부터
    // 엉뚱했다). 명부를 가장 많이 든 것을 고른다 - 액터 매니저에서
    // 쓴 것과 같은 방식이다.
    return find_clan_of_class(reader, rtti, kClanClass, out);
}

bool read_clan_roster(const mem::Reader& reader, std::uintptr_t clan,
                      std::vector<ClanEntry>* out) {
    if (out == nullptr) return false;
    std::uintptr_t arr = 0;
    std::uint32_t count = 0;
    if (!read_records_header(reader, clan, &arr, &count)) return false;

    std::vector<ClanEntry> list;
    list.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint64_t rec = 0;
        if (!reader.read_value(arr + static_cast<std::uintptr_t>(i) * 8, &rec) ||
            rec == 0) {
            continue;
        }
        const auto base = static_cast<std::uintptr_t>(rec);
        ClanEntry e;
        e.record = base;
        if (!reader.read_value(base + kClanRecordRow, &e.row)) continue;
        reader.read_value(base + kClanRecordNo, &e.merc_no);
        reader.read_value(base + kClanRecordHandle, &e.handle);
        if (const RosterEntry* r = character_by_row(e.row)) {
            e.key = r->key;
            e.name = r->name;
            e.label = r->label;
            e.merc_row = r->merc_row;
        }
        list.push_back(std::move(e));
    }
    *out = std::move(list);
    return true;
}


// --------------------------------------------------- 모드용 캐시

namespace {

std::atomic<std::uintptr_t> g_clan{0};
const mem::Rtti* g_rtti = nullptr;
// 목록은 그리는 스레드만 만들고 읽는다 - actors 와 같은 규칙이다.
std::vector<ClanEntry> g_roster;

}  // namespace

bool discover_clan(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_clan.load(std::memory_order_acquire) != 0) return true;
    std::uintptr_t c = 0;
    if (!find_clan_component(reader, rtti, &c)) return false;
    g_clan.store(c, std::memory_order_release);
    g_rtti = &rtti;
    return true;
}

bool clan_ready() { return g_clan.load(std::memory_order_acquire) != 0; }

bool refresh_clan_roster(const mem::Reader& reader) {
    std::uintptr_t c = g_clan.load(std::memory_order_acquire);
    if (c == 0) return false;
    std::vector<ClanEntry> list;
    // 동반자 수가 바뀌면 게임이 컴포넌트를 통째로 새로 만든다(실측
    // 2026-09-09). 캐시한 것이 아직 컴포넌트 꼴인지 먼저 본다.
    if (!looks_like_clan_component(reader, c) ||
        !read_clan_roster(reader, c, &list)) {
        // 월드를 나갔다 들어오면 컴포넌트가 바뀐다. 한 번 다시 찾는다.
        if (g_rtti == nullptr) return false;
        std::uintptr_t again = 0;
        if (!find_clan_component(reader, *g_rtti, &again) || again == 0) return false;
        if (!read_clan_roster(reader, again, &list)) return false;
        g_clan.store(again, std::memory_order_release);
    }
    g_roster = std::move(list);
    return true;
}

const std::vector<ClanEntry>& clan_roster() { return g_roster; }

}  // namespace cdtb::game

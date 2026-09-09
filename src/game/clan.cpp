#include "game/clan.h"

#include <atomic>
#include <windows.h>
#include <utility>

#include "game/actors.h"
#include "game/companion.h"
#include "core/log.h"
#include "mem/safe_read.h"
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

// 컴포넌트를 캐시해 둔다.
//
// find_clan_of_class 는 RTTI 인스턴스 스캔이라 기가바이트를 훑는다.
// 종 바꾸기 한 번에 네 번(클라·서버 × 쓰기·재확인) 돌면 게임이
// 멈춰 보인다 - 사용자가 짚은 그 멈춤이다(2026-09-09).
//
// 캐시한 것이 아직 컴포넌트 꼴인지는 **값싼 검사**로 볼 수 있다
// (looks_like_clan_component). 그것만 통과하면 그대로 쓴다. 동반자가
// 늘거나 줄어 새 컴포넌트가 만들어지면 그때만 다시 찾는다.
std::atomic<std::uintptr_t> g_cached_server{0};
std::atomic<std::uintptr_t> g_cached_client{0};

bool clan_component_cached(const mem::Reader& reader, const mem::Rtti& rtti,
                           bool client, std::uintptr_t* out) {
    std::atomic<std::uintptr_t>& slot = client ? g_cached_client : g_cached_server;
    const std::uintptr_t have = slot.load(std::memory_order_acquire);
    if (have != 0 && looks_like_clan_component(reader, have)) {
        *out = have;
        return true;
    }
    std::uintptr_t found = 0;
    if (!find_clan_of_class(reader, rtti,
                            client ? kClanClientClass : kClanClass, &found)) {
        return false;
    }
    slot.store(found, std::memory_order_release);
    *out = found;
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
    if (clan_component_cached(reader, rtti, false, &srv)) {
        std::uintptr_t rec = 0;
        std::uint16_t row = 0xFFFF;
        if (find_record_by_no(reader, srv, merc_no, &rec, &row)) {
            t.server = rec + kClanRecordRow;
            t.server_row = row;
        }
    }
    if (clan_component_cached(reader, rtti, true, &cli)) {
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

bool resolve_spawn_flag(const mem::Rtti& rtti, const mem::Reader& reader,
                        std::uint64_t merc_no, SpawnFlagTarget* out) {
    if (out == nullptr || merc_no == 0) return false;
    SpawnFlagTarget t;
    std::uintptr_t srv = 0, cli = 0;
    if (clan_component_cached(reader, rtti, false, &srv)) {
        std::uintptr_t rec = 0;
        std::uint16_t row = 0xFFFF;
        if (find_record_by_no(reader, srv, merc_no, &rec, &row)) {
            t.server = rec + kClanRecordHandle;
            reader.read_value(t.server, &t.server_handle);
        }
    }
    if (clan_component_cached(reader, rtti, true, &cli)) {
        std::uintptr_t rec = 0;
        std::uint16_t row = 0xFFFF;
        if (find_record_by_no(reader, cli, merc_no, &rec, &row)) {
            t.client = rec + kClanRecordHandle;
            reader.read_value(t.client, &t.client_handle);
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

bool find_clan_component_client(const mem::Reader& reader, const mem::Rtti& rtti,
                                std::uintptr_t* out) {
    if (out == nullptr) return false;
    return find_clan_of_class(reader, rtti, kClanClientClass, out);
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
        reader.read_value(base + kClanRecordOwner, &e.owner_row);
        if (e.owner_row != 0xFFFF) {
            if (const RosterEntry* o = character_by_row(e.owner_row))
                e.owner_name = o->display();
            e.owner_playable = is_playable_character_row(reader, e.owner_row);
        }
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
    g_cached_server.store(c, std::memory_order_release);
    g_rtti = &rtti;
    // 클라이언트 쪽도 **여기서** 미리 잡아 둔다. RTTI 인스턴스 스캔은
    // 실측 30초가 넘는다(probe 로 재 보니 두 번에 1분 8초). 그리는
    // 스레드가 그것을 돌면 게임이 그만큼 멈춘다 - 사용자가 종 바꾸기에서
    // 겪은 멈춤이 이것이다(2026-09-09). 이 함수는 배경 분석 스레드가
    // 시작할 때 부르므로 여기서 치르는 것이 맞다.
    std::uintptr_t cli = 0;
    if (find_clan_of_class(reader, rtti, kClanClientClass, &cli)) {
        g_cached_client.store(cli, std::memory_order_release);
    }
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
        // 다시 찾는 것은 RTTI 인스턴스 스캔이라 기가바이트를 훑는다.
        // 그리는 스레드에서 2초마다 하면 화면이 쌓린다 - 간격을 둔다.
        static std::uint64_t s_last_scan = 0;
        const std::uint64_t now = GetTickCount64();
        if (s_last_scan != 0 && now - s_last_scan < 10000) return false;
        s_last_scan = now;
        std::uintptr_t again = 0;
        if (!find_clan_component(reader, *g_rtti, &again) || again == 0) return false;
        if (!read_clan_roster(reader, again, &list)) return false;
        g_clan.store(again, std::memory_order_release);
    }
    g_roster = std::move(list);
    return true;
}

const std::vector<ClanEntry>& clan_roster() { return g_roster; }

const mem::Rtti* clan_rtti() { return g_rtti; }

void tick_hire_cleanup(const mem::Rtti& rtti, const mem::Reader& reader) {
    static unsigned long long s_last = 0;
    const unsigned long long now = GetTickCount64();

    // (1) 우리가 획득한 것은 **살아있어도** 소환 판정을 지운다.
    //
    // 2338 은 월드에 서 있던 야생 개체를 그대로 등록하므로 그 순간의
    // 핸들이 레코드에 박힌다. 그 액터는 살아있지만 **제대로 소환된
    // 동반자 개체가 아니라** 게임은 "이미 나와 있음" 으로 보고 소환도
    // 해제도 거부한다 - 실측 2026-09-09: 참새·회색 앵무새가 살아있는
    // 야생 액터로 그대로 서 있었고 둘 다 소환이 안 됐다.
    //
    // 그래서 생존 여부를 보지 않고 지운다. 우리가 방금 등록한 것이라
    // 그 핸들은 어찌됐든 정상 소환 결과가 아니다.
    HireAck acks[kHireAckSlots];
    const int n = pending_hire_acks(acks, kHireAckSlots);
    for (int i = 0; i < n; ++i) {
        if (now - acks[i].at_ms < kHireCleanupDelayMs) continue;
        SpawnFlagTarget t;
        if (!resolve_spawn_flag(rtti, reader, acks[i].merc_no, &t)) continue;
        mark_hire_ack_handled(acks[i].merc_no);
        if (t.server_handle == 0 && t.client_handle == 0) continue;
        const std::uint32_t zero = 0;
        if (mem::safe_write_bytes(t.server, &zero, 4) &&
            mem::safe_write_bytes(t.client, &zero, 4)) {
            log::infof("획득 뒤처리: 번호 {} 의 소환 판정(핸들 0x{:08X})을 풀었다",
                       acks[i].merc_no, t.server_handle);
        }
    }
    if (n > 0) refresh_clan_roster(reader);

    // (2) 그 밖의 죽은 핸들을 훑는다. 이쪽은 살아있으면 건드리지
    // 않는다 - 진짜로 나와 있는 개체의 판정을 지우면 중복 소환이 된다.
    if (s_last != 0 && now - s_last < 15000) return;
    s_last = now;
    std::uintptr_t srv = 0;
    if (!clan_component_cached(reader, rtti, false, &srv)) return;
    std::vector<ClanEntry> list;
    if (!read_clan_roster(reader, srv, &list)) return;
    int cleared = 0;
    for (const auto& e : list) {
        if (e.handle == 0) continue;
        bool known = false;
        if (actor_handle_alive(reader, e.handle, &known)) continue;
        if (!known) return;
        SpawnFlagTarget t;
        if (!resolve_spawn_flag(rtti, reader, e.merc_no, &t)) continue;
        if (t.server_handle == 0 && t.client_handle == 0) continue;
        const std::uint32_t zero = 0;
        if (mem::safe_write_bytes(t.server, &zero, 4) &&
            mem::safe_write_bytes(t.client, &zero, 4)) {
            ++cleared;
            log::infof("소환 판정 정리: 번호 {} 의 죽은 핸들 0x{:08X} 를 지웠다",
                       e.merc_no, t.server_handle);
        }
    }
    if (cleared > 0) refresh_clan_roster(reader);
}

}  // namespace cdtb::game

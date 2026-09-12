#include "game/clan.h"

#include <atomic>
#include <string>
#include <windows.h>
#include <utility>

#include "game/actors.h"
#include "game/grant.h"
#include "game/companion.h"
#include "core/log.h"
#include "core/write_log.h"
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
// 서버 쪽 컴포넌트. clan_ready() 가 보는 값이라 캐시와 **함께**
// 갱신해야 한다 - 따로 두었더니 한쪽만 갱신돼 다른 쪽이 상했다.
std::atomic<std::uintptr_t> g_clan{0};

// 캐시가 상했을 때 **그리는 스레드에서** 다시 찾으면 안 된다.
//
// 실측 2026-09-10: 시작 로그가 `탐색 [동반자 명부] 10688ms` 다.
// 재탐색 한 번이 10초가 넘는다(클라이언트 쪽은 더 길다). 그런데
// 캐시를 다시 채우는 길이 없어서 - discover_clan 은 g_clan 이 차면
// 즉시 반환하고, refresh_clan_roster 는 g_clan 만 갱신했다 - 동반자를
// 얻어 게임이 컴포넌트를 새로 만들면 캐시가 상한 채로 남았다. 그
// 상태에서 tick_hire_cleanup 이 15초마다 이 함수를 부르니 15초마다
// 10초씩 멈췄다. 사용자가 "내 동반자 탭이 게임을 멈춘다"고 한 것이
// 이것이다.
//
// 그래서 배경 스레드에 맡긴다. 끝날 때까지 부르는 쪽은 false 를 받고,
// 화면은 옛 목록을 그대로 쓴다 - 멈추는 것보다 낫다.
std::atomic<bool> g_bg_rescan{false};      // DLL 만 켠다(probe 는 그냥 훑는다)
std::atomic<bool> g_rescan_busy{false};
// 워커는 자기 리더를 쓴다. 호출자의 리더(렌더 프레임의 스택 지역)를 들고 있으면
// 프레임이 끝난 뒤 사라진 스택으로 가상 호출을 한다(재리뷰 N4). LocalReader 는
// 상태가 없어 어느 스레드에서 써도 같다.
mem::LocalReader g_worker_reader;
const mem::Rtti* g_rescan_rtti = nullptr;
std::atomic<bool> g_rescan_want[2]{};      // [0] 서버 [1] 클라이언트
// discover_clan 이 쓴 RTTI(clan_rtti 로 공개). 배경 워커가 먼저 찾은 경우에도 채운다 -
// 비면 종 바꾸기·획득 뒤처리·캐시 복구가 전부 막힌다(리뷰 C3).
std::atomic<const mem::Rtti*> g_rtti{nullptr};
// 실패해도 남긴다 - 다시 찾기(배경 재탐색)용.
std::atomic<const mem::Rtti*> g_discovery_rtti{nullptr};

// 찾은 컴포넌트를 **한 자리에서** 갈무리한다. 캐시와 g_clan 이
// 따로 갱신되던 것이 이번 멈춤의 뿌리였다.
void store_component(bool client, std::uintptr_t found) {
    if (client) {
        g_cached_client.store(found, std::memory_order_release);
    } else {
        g_cached_server.store(found, std::memory_order_release);
        g_clan.store(found, std::memory_order_release);
    }
}

DWORD WINAPI rescan_worker(LPVOID) {
    for (int k = 0; k < 2; ++k) {
        if (!g_rescan_want[k].exchange(false)) continue;
        const mem::Reader* r = &g_worker_reader;
        const mem::Rtti* t = g_rescan_rtti;
        if (r == nullptr || t == nullptr) continue;
        const std::uint64_t t0 = GetTickCount64();
        std::uintptr_t found = 0;
        if (find_clan_of_class(*r, *t, k == 1 ? kClanClientClass : kClanClass,
                               &found)) {
            store_component(k == 1, found);
            if (k == 0) {
                const mem::Rtti* expected = nullptr;
                g_rtti.compare_exchange_strong(expected, t);
            }
            log::infof("명부 컴포넌트 재탐색({}) 완료 0x{:X} - {}ms",
                       k == 1 ? "클라이언트" : "서버", found,
                       GetTickCount64() - t0);
        } else {
            log::warnf("명부 컴포넌트 재탐색({}) 실패 - {}ms",
                       k == 1 ? "클라이언트" : "서버", GetTickCount64() - t0);
        }
    }
    g_rescan_busy.store(false, std::memory_order_release);
    return 0;
}

void request_rescan(const mem::Reader& reader, const mem::Rtti& rtti,
                    bool client) {
    (void)reader;   // 워커는 g_worker_reader 를 쓴다 - 호출자 리더의 수명을 믿지 않는다
    g_rescan_rtti = &rtti;
    g_rescan_want[client ? 1 : 0].store(true, std::memory_order_release);
    bool expected = false;
    if (!g_rescan_busy.compare_exchange_strong(expected, true)) return;
    const HANDLE h = ::CreateThread(nullptr, 0, rescan_worker, nullptr, 0,
                                    nullptr);
    if (h != nullptr) {
        ::CloseHandle(h);
        log::infof("명부 컴포넌트를 배경에서 찾는다 (RTTI 스캔 10초쯤, 화면은 멈추지 "
                   "않는다)");
    } else {
        g_rescan_busy.store(false, std::memory_order_release);
    }
}

bool clan_component_cached(const mem::Reader& reader, const mem::Rtti& rtti,
                           bool client, std::uintptr_t* out) {
    std::atomic<std::uintptr_t>& slot = client ? g_cached_client : g_cached_server;
    const std::uintptr_t have = slot.load(std::memory_order_acquire);
    if (have != 0 && looks_like_clan_component(reader, have)) {
        *out = have;
        return true;
    }
    if (g_bg_rescan.load(std::memory_order_acquire)) {
        request_rescan(reader, rtti, client);
        return false;   // 이번 프레임은 포기한다. 멈추는 것보다 낫다.
    }
    std::uintptr_t found = 0;
    if (!find_clan_of_class(reader, rtti,
                            client ? kClanClientClass : kClanClass, &found)) {
        return false;
    }
    store_component(client, found);
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

// 목록은 그리는 스레드만 만들고 읽는다 - actors 와 같은 규칙이다.
std::vector<ClanEntry> g_roster;

}  // namespace

bool discover_clan(const mem::Rtti& rtti, const mem::Reader& reader) {
    // RTTI 를 먼저 게시한다 - 다시 찾기(버튼)가 먼저 성공했더라도 뒤늦게 채우고(재리뷰
    // N1), 값싼 길에서도 g_rtti 가 g_clan 보다 먼저 보이게(R3).
    g_discovery_rtti.store(&rtti, std::memory_order_release);
    if (g_clan.load(std::memory_order_acquire) != 0) {
        const mem::Rtti* expected = nullptr;
        g_rtti.compare_exchange_strong(expected, &rtti);
        return true;
    }
    // 1) 값싼 길: 세션 표에 잡힌 세션의 [+0x68]+0x110 - 고용 경로(2338)가 쓰는 바로 그
    //    용병단 컴포넌트다. 읽기 두 번이라 매 바퀴 불러도 되고 세션 이름표도 필요
    //    없다(리뷰 O2). 클라 쪽 컴포넌트도 레코드 머리가 같으므로 클래스 이름까지
    //    본다(재리뷰 N2 - 클라를 잡으면 종 바꾸기·소환 판정이 서버에 안 닿는다).
    std::uintptr_t c = 0;
    if (clan_object(reader, 0, &c) && looks_like_clan_component(reader, c) &&
        rtti.class_of_object(c) == kClanClass) {
        g_rtti.store(&rtti, std::memory_order_release);
        store_component(false, c);
        log::infof("동반자 명부: 세션 사슬로 잡았다 0x{:X}", c);
        return true;
    }
    // 2) RTTI 스캔(10~17초)은 월드 안(세션 이름표까지 붙은 뒤)에서만, 30초에 한 번.
    //    월드 밖이면 헛수고고, 안인데 사슬이 비었으면 잠시 뒤 다시 보는 편이 낫다.
    if (pick_drive_session(reader) == 0) return false;
    static std::uint64_t s_last_scan_ms = 0;
    const std::uint64_t now = ::GetTickCount64();
    if (s_last_scan_ms != 0 && now - s_last_scan_ms < 30000) return false;
    s_last_scan_ms = now;
    if (!find_clan_component(reader, rtti, &c)) {
        log::infof("동반자 명부: 월드 안인데 RTTI 스캔으로도 못 찾았다 - 30초 뒤 다시");
        return false;
    }
    g_rtti.store(&rtti, std::memory_order_release);   // g_clan 보다 먼저(R3)
    store_component(false, c);
    // 클라이언트 쪽도 **여기서** 미리 잡아 둔다. RTTI 인스턴스 스캔은
    // 실측 30초가 넘는다(probe 로 재 보니 두 번에 1분 8초). 그리는
    // 스레드가 그것을 돌면 게임이 그만큼 멈춘다 - 사용자가 종 바꾸기에서
    // 겪은 멈춤이 이것이다(2026-09-09). 이 함수는 배경 분석 스레드가
    // 시작할 때 부르므로 여기서 치르는 것이 맞다.
    std::uintptr_t cli = 0;
    if (find_clan_of_class(reader, rtti, kClanClientClass, &cli)) {
        store_component(true, cli);
    }
    return true;
}

bool clan_ready() { return g_clan.load(std::memory_order_acquire) != 0; }

bool clan_request_discovery(const mem::Reader& reader) {
    if (clan_ready()) return true;
    // 분석이 아직 RTTI 를 넘기지 않았으면 아무것도 하지 않는다 - 값싼 길도 서버/클라를
    // 가르려면 RTTI 가 필요하고, RTTI 없이 g_clan 만 채우면 복구가 없다(재리뷰 N1·N2).
    const mem::Rtti* t = g_discovery_rtti.load(std::memory_order_acquire);
    if (t == nullptr) return false;
    // 값싼 길부터 - 그리는 스레드에서도 읽기 두 번 + 클래스 조회라 괜찮다.
    std::uintptr_t c = 0;
    if (clan_object(reader, 0, &c) && looks_like_clan_component(reader, c) &&
        t->class_of_object(c) == kClanClass) {
        const mem::Rtti* expected = nullptr;
        g_rtti.compare_exchange_strong(expected, t);
        store_component(false, c);
        log::infof("동반자 명부: 다시 찾기 - 세션 사슬로 잡았다 0x{:X}", c);
        return true;
    }
    if (!g_bg_rescan.load(std::memory_order_acquire)) return false;
    request_rescan(reader, *t, false);   // RTTI 스캔은 배경 워커에서
    return true;
}

void enable_background_clan_rescan() {
    g_bg_rescan.store(true, std::memory_order_release);
}

bool refresh_clan_roster(const mem::Reader& reader) {
    std::uintptr_t c = g_clan.load(std::memory_order_acquire);
    if (c == 0) return false;
    std::vector<ClanEntry> list;
    // 동반자 수가 바뀌면 게임이 컴포넌트를 통째로 새로 만든다(실측
    // 2026-09-09). 캐시한 것이 아직 컴포넌트 꼴인지 먼저 본다.
    if (!looks_like_clan_component(reader, c) ||
        !read_clan_roster(reader, c, &list)) {
        // 월드를 나갔다 들어오면 컴포넌트가 바뀐다. 한 번 다시 찾는다.
        const mem::Rtti* rt = g_rtti.load(std::memory_order_acquire);
        if (rt == nullptr) return false;
        // 다시 찾는 것은 10초가 넘는 RTTI 스캔이다. 그리는 스레드에서
        // 하면 그만큼 게임이 멈춘다 - clan_component_cached 와 같은
        // 배경 경로로 넘기고, 이번 판은 옛 목록을 그대로 둔다.
        std::uintptr_t again = 0;
        if (!clan_component_cached(reader, *rt, false, &again) ||
            again == 0) {
            return false;
        }
        if (!read_clan_roster(reader, again, &list)) return false;
    }
    g_roster = std::move(list);
    return true;
}

const std::vector<ClanEntry>& clan_roster() { return g_roster; }

const mem::Rtti* clan_rtti() { return g_rtti.load(std::memory_order_acquire); }

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

SpeciesApply apply_species(const mem::Reader& reader, std::uint64_t merc_no,
                           std::uint16_t row, std::string* msg) {
    const mem::Rtti* rtti = clan_rtti();
    if (rtti == nullptr) {
        *msg = "RTTI 준비 전입니다";
        return SpeciesApply::NoRtti;
    }
    SpeciesWriteTarget t;
    if (!resolve_species_write(*rtti, reader, merc_no, &t)) {
        *msg = "자리를 못 찾았습니다 - 월드 안인지 보세요";
        return SpeciesApply::NoTarget;
    }
    // 게임 상태를 바꾸는 일은 **반드시 로그에 남긴다.** 이것이 없어서 사용자가
    // "바꾸기 뒤 팅겼다" 고 했을 때 무엇을 무엇으로 바꿨는지 알 수 없었다
    // (2026-09-09).
    const RosterEntry* from = character_by_row(t.server_row);
    const RosterEntry* to = character_by_row(row);
    log_write("동반자 종 번호 " + std::to_string(merc_no), t.server,
              "행 " + std::to_string(t.server_row) + "(" +
                  (from != nullptr ? from->name : std::string("?")) + ")",
              "행 " + std::to_string(row) + "(" +
                  (to != nullptr ? to->name : std::string("?")) + ")");

    const std::uint8_t buf[2] = {static_cast<std::uint8_t>(row & 0xFF),
                                 static_cast<std::uint8_t>(row >> 8)};
    // 클라·서버 양쪽에 써야 한다. 서버만 쓰면 게임이 보는 사본은 그대로다
    // (실측 2026-09-09). 둘째 쓰기가 실패하면 첫째를 되돌린다 - 한쪽만 바뀐 채
    // 두면 두 사본이 어긋난다(Codex 지적 2026-09-11).
    std::uint8_t old_server[2]{};
    const bool have_old = reader.read(t.server, old_server, 2);
    if (!mem::safe_write_bytes(t.server, buf, 2)) {
        // 위의 감사 줄이 "바꿨다" 처럼 읽히지 않게 실패도 남긴다.
        log::warnf("동반자 종 번호 {}: 서버 사본 쓰기 실패 - 아무것도 바뀌지 않았다",
                   merc_no);
        *msg = "쓰기 실패 (서버 사본) - 바뀐 것 없음";
        return SpeciesApply::WriteFailed;
    }
    if (!mem::safe_write_bytes(t.client, buf, 2)) {
        const bool rolled =
            have_old && mem::safe_write_bytes(t.server, old_server, 2);
        log::warnf("동반자 종 번호 {}: 클라 사본 쓰기 실패 - 서버 사본 {}", merc_no,
                   rolled ? "되돌림" : "되돌리기 실패 (두 사본이 어긋남)");
        *msg = rolled ? "쓰기 실패 (클라 사본) - 서버 사본은 되돌렸습니다"
                      : "쓰기 실패 (클라 사본) - 서버 사본을 되돌리지 못했습니다";
        return SpeciesApply::WriteFailed;
    }
    SpeciesWriteTarget after;
    if (resolve_species_write(*rtti, reader, merc_no, &after) &&
        after.server_row == row && after.client_row == row) {
        *msg = "바꿨습니다 (행 " + std::to_string(row) + ")";
        return SpeciesApply::Ok;
    }
    *msg = "쓴 뒤 확인이 어긋났습니다 - 다시 보세요";
    return SpeciesApply::VerifyMismatch;
}


std::vector<std::string> clan_scan_classes() { return {kClanClass, kClanClientClass}; }

}  // namespace cdtb::game

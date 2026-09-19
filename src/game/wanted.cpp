#include "game/wanted.h"

#include <windows.h>   // GetTickCount64 - 재탐색 간격을 막는 데 쓴다

#include <atomic>
#include <cstring>
#include <string>

#include <map>

#include "core/log.h"
#include "game/grant.h"
#include "game/localization.h"
#include "mem/safe_read.h"

namespace cdtb::game {
namespace {

constexpr const char* kClearClass = "TrocTrClearWantedReq";
constexpr const char* kStateClass = "TrocTrChangeWantedStateReq";

MessageDesc g_clear_msg;
MessageDesc g_state_msg;

// 전송에 실제로 쓰는 ID. 해석 전에는 박아 둔 상수이고, 게임에서 클래스
// 이름으로 해석되면 그 값으로 갈린다. 렌더·명령 스레드가 같이 읽는다.
std::atomic<std::uint16_t> g_clear_id{kClearWantedId};
std::atomic<std::uint16_t> g_state_id{kChangeWantedStateId};

// --- 벌금 사슬 ---
constexpr const char* kCompClass = ".?AVClientSelfWantedActorComponent@pa@@";
// **벡터다.** {데이터 ptr, 크기 u32, 용량 u32} - 단일 포인터가 아니다
// (2026-09-19 정정, wanted.h 머리말).
constexpr std::size_t kCompRegionVec = 0x30;
constexpr std::size_t kVecSize = 0x38;
constexpr std::size_t kVecCap = 0x3C;
constexpr std::size_t kRegionKey = 0x28;    // u32 구역 키
constexpr std::size_t kRegionFine = 0x30;   // u64 벌금 (2자리 고정소수)

std::uintptr_t g_comp = 0;
std::uintptr_t g_comp_vtable = 0;    // 처음 찾을 때 적어 두고 대조에 쓴다

// 월드 안에서 몇 번까지 스스로 훑어 볼 것인가. 다 쓰면 그만두고, 화면에서
// 눌러야 다시 본다. 한 번에 ~45초짜리 힙 전수라 이 수를 늘리면 비싸진다.
constexpr int kFindTries = 3;
int g_find_tries = 0;
// 지역 객체의 vtable. 찾을 때 RTTI 이름으로 확인해 둔 것이라, 그 뒤에는
// 이름 조회 없이 이 값만 대조하면 된다(매 프레임 도는 자리다).
std::uintptr_t g_region_vtable = 0;

// 살아 있는가. 세이브를 다시 부르면 그 자리에 다른 객체가 들어앉는다 -
// 실제로 좌표·쿼터니언 뭉치를 읽을 뻔했다. vtable 로 거른다.
bool comp_alive(const mem::Reader& reader) {
    if (g_comp == 0 || g_comp_vtable == 0) return false;
    std::uint64_t vt = 0;
    if (!reader.read_value(g_comp, &vt)) return false;
    return static_cast<std::uintptr_t>(vt) == g_comp_vtable;
}

// 컴포넌트 -> 구역 기록 **전부**. 하나도 못 얻으면 빈 목록이다.
//
// **주소를 들고 있지 않는다.** 부를 때마다 벡터 머리부터 다시 읽는다 -
// 구역이 하나 늘면 재할당돼 옛 주소가 죽는다(wanted.h 머리말의 실측).
std::vector<WantedRegion> regions_now(const mem::Reader& reader) {
    std::vector<WantedRegion> out;
    if (!comp_alive(reader) || g_region_vtable == 0) return out;
    std::uint64_t data = 0;
    std::uint32_t size = 0, cap = 0;
    if (!reader.read_value(g_comp + kCompRegionVec, &data)) return out;
    if (!reader.read_value(g_comp + kVecSize, &size)) return out;
    if (!reader.read_value(g_comp + kVecCap, &cap)) return out;
    const std::size_t n = wanted_region_count(data, size, cap);
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uintptr_t a =
            static_cast<std::uintptr_t>(data) + i * kWantedRegionStride;
        // **원소마다** vtable 을 대조한다. 재할당 도중이면 절반만 성할 수 있고,
        // 엉뚱한 자리에 쓰면 게임을 팅긴다.
        std::uint64_t vt = 0;
        if (!reader.read_value(a, &vt) ||
            static_cast<std::uintptr_t>(vt) != g_region_vtable) {
            continue;
        }
        WantedRegion r;
        r.addr = a;
        if (!reader.read_value(a + kRegionKey, &r.key)) continue;
        if (!reader.read_value(a + kRegionFine, &r.raw)) continue;
        out.push_back(r);
    }
    return out;
}

}  // namespace

std::uint64_t bounty_to_raw(double shown) {
    // 음수를 u64 로 그냥 변환하면 거대한 값이 된다. 부호를 안 보고
    // 넘긴 실수가 이 레포에서 한 번 났다(TROUBLESHOOTING 6.20).
    if (!(shown > 0.0)) return 0;   // NaN 도 여기서 0 으로 떨어진다
    // 0.005 를 더해 반올림한다. 12.34 * 100 이 1233.9999 로 나오는
    // 부동소수 오차 때문에 그냥 자르면 한 칸씩 모자란다.
    const double raw = shown * 100.0 + 0.5;
    if (raw >= static_cast<double>(kBountyMaxRaw)) return kBountyMaxRaw;
    return static_cast<std::uint64_t>(raw);
}

double bounty_from_raw(std::uint64_t raw) {
    return static_cast<double>(raw) / 100.0;
}

bool wanted_component_find(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (comp_alive(reader)) return true;

    // 월드 밖에서는 아직 없다. 설치 지점에서 한 번만 부르고 끝냈더니
    // 시작 때 등록표 둘만 보고 포기했고, 그 뒤로 영영 못 찾았다 -
    // 화면에 벌금 칸이 통째로 안 그려졌다(2026-09-18).
    //
    // 그래서 여기서 스스로 다시 본다. 다만 힙 전수 탐색이라 비싸므로
    // **월드 안일 때만, 15초에 한 번만** 돈다(clan.cpp 와 같은 규칙).
    if (pick_drive_session(reader) == 0) return false;

    // ---- 그런데 못 찾으면 **영원히** 다시 돌았다 (2026-09-18 실측)
    //
    // 한 번 훑는 데 ~45초가 걸린다. 15초를 쉬어도 실질은 쉬지 않고 도는 것이라,
    // 로그가 이 경고로 도배되고 배경이 계속 무거웠다:
    //
    //   수배 컴포넌트를 못 찾았다 - 후보 8개, … 끝내 못 읽음 19,305,328KB
    //
    // 그래서 **몇 번 해 보고 그만둔다.** 다시 보려면 화면에서 눌러야 한다
    // (`wanted_find_rearm`). 한 번만 해 보고 그만두지 않는 이유는 위 문단이다 -
    // 월드에 막 들어온 순간에는 아직 없을 수 있다.
    if (g_find_tries >= kFindTries) return false;
    static std::uint64_t s_last_ms = 0;
    const std::uint64_t now = ::GetTickCount64();
    if (s_last_ms != 0 && now - s_last_ms < 15000) return false;
    s_last_ms = now;
    ++g_find_tries;

    // 힙 전수 탐색이라 비싸다. 죽었을 때만 다시 돈다.
    //
    // **"+0x30 이 0 이 아니다" 로는 못 가른다.** 등록표 쪽 객체에도
    // vtable 이 들어 있고 그 자리에 무엇이든 들어 있어서, 첫 후보를
    // 집었더니 0x1804F8C0(지역 0x1574128)이 나왔다 - 실제 힙 객체는
    // 0x22D5AC7A100 꼴이다. 벌금 칸이 통째로 안 그려졌다(2026-09-18).
    //
    // 그래서 **가리키는 것의 클래스 이름을 대조한다.** 주소 범위로
    // 추측하지 않는다.
    // 판정을 스캔에 넘겨 **첫 합격에서 멈춘다.** 훑은 수로 64 를 두면 낮은
    // 주소의 가짜가 앞자리를 다 차지해 진짜가 잘린다(TROUBLESHOOTING 4.33) -
    // 여기는 위에서 보듯 가짜 후보가 실제로 여럿 걸리는 자리라 특히 그렇다.
    std::size_t n_seen = 0;
    const auto hit = rtti.instances_of_class(
        kCompClass, 1, [&](std::uintptr_t addr) {
            ++n_seen;
            std::uint64_t vt = 0;
            if (!reader.read_value(addr, &vt) || vt == 0) return false;
            // 벡터의 **데이터 포인터**가 곧 원소 [0] 의 주소라, 그것이
            // WantedRegionData 인지 보는 것으로 컴포넌트를 가린다.
            std::uint64_t region = 0;
            if (!reader.read_value(addr + kCompRegionVec, &region) ||
                region == 0) {
                return false;
            }
            const std::string cls =
                rtti.class_of_object(static_cast<std::uintptr_t>(region));
            if (cls.find("WantedRegionData") == std::string::npos) {
                log::infof("수배 컴포넌트 후보 0x{:X} 는 건너뛴다 - +0x30 이 "
                           "{} 다",
                           addr, cls.empty() ? "이름 없음" : cls);
                return false;
            }
            std::uint64_t rvt = 0;
            if (!reader.read_value(static_cast<std::uintptr_t>(region), &rvt) ||
                rvt == 0) {
                return false;
            }
            return true;
        });
    if (!hit.empty()) {
        const std::uintptr_t addr = hit.front();
        std::uint64_t vt = 0, region = 0, rvt = 0;
        // 판정을 통과한 자리라 이 셋은 다시 읽힌다. 그래도 값이 사라졌으면
        // 잡지 않는다 - 힙은 스캔 도중에도 바뀐다.
        if (reader.read_value(addr, &vt) && vt != 0 &&
            reader.read_value(addr + kCompRegionVec, &region) && region != 0 &&
            reader.read_value(static_cast<std::uintptr_t>(region), &rvt) &&
            rvt != 0) {
            g_comp = addr;
            g_comp_vtable = static_cast<std::uintptr_t>(vt);
            g_region_vtable = static_cast<std::uintptr_t>(rvt);
            log::infof("수배 컴포넌트: 0x{:X} (지역 0x{:X} {})", g_comp, region,
                       rtti.class_of_object(static_cast<std::uintptr_t>(region)));
            return true;
        }
    }
    const auto st = mem::Rtti::scan_stats();
    const bool last = g_find_tries >= kFindTries;
    log::warnf("수배 컴포넌트를 못 찾았다 ({}/{}회{}) - 후보 {}개, "
               "힙 훑기 누적: 창 {} · 쪼갠 창 {} · 끝내 못 읽음 {}KB",
               g_find_tries, kFindTries,
               last ? ", **그만 찾는다 - 창에서 [다시 찾기]**" : "", n_seen,
               st.windows, st.retried, st.lost_kb);
    return false;
}

bool bounty_ready(const mem::Reader& reader) {
    return !regions_now(reader).empty();
}

std::size_t wanted_region_count(std::uint64_t data, std::uint32_t size,
                                std::uint32_t cap) {
    if (data == 0 || size == 0) return 0;
    if (size > cap) return 0;                 // 머리가 깨졌다
    if (cap > kWantedRegionCapMax) return 0;  // 말이 안 되는 용량
    return size;
}

bool wanted_regions(const mem::Reader& reader,
                    std::vector<WantedRegion>* out) {
    if (out == nullptr) return false;
    *out = regions_now(reader);
    return !out->empty();
}

bool wanted_component_ready() { return g_comp != 0 && g_comp_vtable != 0; }

bool wanted_find_gave_up() {
    return g_comp == 0 && g_find_tries >= kFindTries;
}

void wanted_find_rearm() {
    g_find_tries = 0;
    log::infof("수배 컴포넌트: 다시 찾는다 ({}회까지)", kFindTries);
}

bool bounty_write_region(const mem::Reader& reader, std::uint32_t key,
                         std::uint64_t raw) {
    // **여기서 다시 읽는다.** 화면이 들고 있던 주소로 쓰면 재할당된 뒤
    // 죽은 배열에 쓴다 - 실측 2026-09-19 에 실제로 그랬다.
    for (const auto& r : regions_now(reader)) {
        if (r.key != key) continue;
        std::uint64_t before = 0;
        reader.read_value(r.addr + kRegionFine, &before);
        if (!mem::safe_write_bytes(r.addr + kRegionFine, &raw, sizeof raw)) {
            log::warnf("벌금 쓰기 실패: 구역 {} 0x{:X}", key,
                       r.addr + kRegionFine);
            return false;
        }
        // 쓴 값을 그대로 남긴다. 되돌릴 일이 생기면 이 줄이 원본이다.
        log::infof("벌금: 구역 {} {} -> {} (0x{:X})", key, before, raw,
                   r.addr + kRegionFine);
        return true;
    }
    log::warnf("벌금 쓰기: 구역 {} 의 기록을 못 찾았다", key);
    return false;
}

std::string wanted_region_name(const mem::Rtti* rtti, const mem::Reader& reader,
                               std::uint32_t key) {
    if (rtti == nullptr || key == 0) return {};

    // 푼 것은 들고 있는다. 구역은 많아야 다섯이라 표가 작다.
    static std::map<std::uint32_t, std::string> s_cache;
    const auto hit = s_cache.find(key);
    if (hit != s_cache.end()) return hit->second;

    static LocSystem s_sys;
    static bool s_have_sys = false;
    if (!s_have_sys) {
        s_have_sys = find_loc_system(*rtti, reader, &s_sys);
        if (!s_have_sys) return {};   // 아직 안 올라왔다. 다음에 다시 본다
    }

    // 이름의 필드 번호는 표마다 다르다(아이템 0x70 · 캐릭터 0x30). 구역 것은
    // 모르므로 **한 번만** 훑어 잡는다. 잡으면 그 뒤로는 그 번호만 쓴다.
    static std::uint32_t s_field = 0;
    static bool s_have_field = false;
    constexpr std::uint32_t kFieldMax = 0x80;

    std::string text;
    if (s_have_field) {
        if (!resolve(reader, s_sys, loc_key(key, s_field), &text, nullptr)) {
            return {};
        }
    } else {
        for (std::uint32_t f = 0; f <= kFieldMax; ++f) {
            if (!resolve(reader, s_sys, loc_key(key, f), &text, nullptr)) {
                continue;
            }
            if (text.empty()) continue;
            s_field = f;
            s_have_field = true;
            log::infof("구역 이름 필드를 찾았다: 0x{:X} (구역 {} -> {})", f, key,
                       text);
            break;
        }
        if (!s_have_field) return {};
    }
    if (text.empty()) return {};
    s_cache.emplace(key, text);
    return text;
}

bool bounty_clear_all(const mem::Reader& reader, int* changed_out) {
    if (changed_out != nullptr) *changed_out = 0;
    const auto regions = regions_now(reader);
    if (regions.empty()) {
        log::warnf("벌금 전부 0: 구역 기록을 못 얻었다");
        return false;
    }
    int changed = 0;
    for (const auto& r : regions) {
        if (r.raw == 0) continue;
        if (bounty_write_region(reader, r.key, 0)) ++changed;
    }
    if (changed_out != nullptr) *changed_out = changed;
    log::infof("벌금 전부 0: 구역 {}개 중 {}개를 바꿨다", regions.size(),
               changed);
    return true;
}

bool build_clear_wanted_wire(std::uint32_t handle, std::uint8_t flag,
                             std::uint8_t* out, std::size_t cap,
                             std::size_t* len_out) {
    if (out == nullptr || cap < kClearWantedWireLen) return false;
    if (handle == 0) return false;   // 대상 없음

    // 본문 길이는 전체에서 머리 5 를 뺀 값이어야 한다. 상수로 박지
    // 않고 빼서 구한다 - 둘이 갈리면 게임이 메시지를 조용히 버린다.
    const std::uint16_t id = g_clear_id.load(std::memory_order_acquire);
    const std::uint16_t body =
        static_cast<std::uint16_t>(kClearWantedWireLen - 5);
    std::memcpy(out + 0, &id, 2);
    out[2] = 0;
    std::memcpy(out + 3, &body, 2);
    std::memcpy(out + 5, &handle, 4);
    out[9] = flag;
    if (len_out != nullptr) *len_out = kClearWantedWireLen;
    return true;
}

bool build_change_wanted_state_wire(std::uint32_t handle, std::uint8_t state,
                                    std::uint8_t extra, std::uint8_t* out,
                                    std::size_t cap, std::size_t* len_out) {
    if (out == nullptr || cap < kChangeWantedStateWireLen) return false;
    if (handle == 0) return false;   // 대상 없음

    const std::uint16_t id = g_state_id.load(std::memory_order_acquire);
    const std::uint16_t body =
        static_cast<std::uint16_t>(kChangeWantedStateWireLen - 5);
    std::memcpy(out + 0, &id, 2);
    out[2] = 0;
    std::memcpy(out + 3, &body, 2);
    std::memcpy(out + 5, &handle, 4);
    out[9] = state;
    out[10] = extra;
    if (len_out != nullptr) *len_out = kChangeWantedStateWireLen;
    return true;
}

// --- 게임에 붙는 배관 --------------------------------------------------

bool message_id_drifted(std::uint16_t baked, std::uint16_t resolved,
                        std::uint16_t* use) {
    // 0 은 "아직 못 풀었다" 다. 그때는 박아 둔 값을 그대로 쓴다.
    const std::uint16_t pick = (resolved != 0) ? resolved : baked;
    if (use != nullptr) *use = pick;
    return resolved != 0 && resolved != baked;
}

std::uint16_t wanted_effective_clear_id() {
    return g_clear_id.load(std::memory_order_acquire);
}

std::uint16_t wanted_effective_state_id() {
    return g_state_id.load(std::memory_order_acquire);
}

// 해석값을 전송용으로 채택하고, 박아 둔 상수와 갈리면 크게 남긴다.
static void adopt_message_id(const char* cls, std::uint16_t baked,
                      std::uint16_t resolved,
                      std::atomic<std::uint16_t>* slot) {
    std::uint16_t use = baked;
    const bool drift = message_id_drifted(baked, resolved, &use);
    slot->store(use, std::memory_order_release);
    if (drift) {
        log::warnf("메시지 ID 가 갈렸다: {} 은 코드에 {} 로 박혀 있는데 게임은 "
                   "{} 이다 - 전송에는 게임 값을 쓴다. 소스 상수를 고칠 것 "
                   "(tools/rtti/recheck.py 가 이 대조를 한 번에 한다)",
                   cls, baked, resolved);
    }
}

bool wanted_resolve(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_state_msg.descriptor == 0 &&
        resolve_message(rtti, reader, kStateClass, &g_state_msg)) {
        log::infof("수배 상태 준비: {} ID {}", kStateClass, g_state_msg.id);
        adopt_message_id(kStateClass, kChangeWantedStateId,
                         static_cast<std::uint16_t>(g_state_msg.id),
                         &g_state_id);
    }
    if (g_clear_msg.descriptor != 0) return true;
    if (!resolve_message(rtti, reader, kClearClass, &g_clear_msg)) {
        log::warnf("수배 해제: {} 를 해석하지 못했다", kClearClass);
        return false;
    }
    // 해석된 ID 를 남긴다. 게임이 갱신되면 여기가 먼저 달라진다 -
    // 값이 바뀐 것을 로그로 알 수 있어야 한다(실측 2026-09-17: 2646).
    log::infof("수배 해제 준비: {} ID {} 서술자 0x{:X}", kClearClass,
               g_clear_msg.id, g_clear_msg.descriptor);
    adopt_message_id(kClearClass, kClearWantedId,
                     static_cast<std::uint16_t>(g_clear_msg.id), &g_clear_id);
    return true;
}

bool wanted_ready() { return g_clear_msg.descriptor != 0; }

std::uint32_t wanted_clear_message_id() { return g_clear_msg.id; }

bool request_clear_wanted(const mem::Reader& reader, std::uint32_t handle,
                          std::uint8_t flag) {
    if (!wanted_ready()) {
        log::warnf("수배 해제: 메시지가 아직 준비되지 않았다");
        return false;
    }
    std::uint8_t wire[kClearWantedWireLen]{};
    std::size_t len = 0;
    if (!build_clear_wanted_wire(handle, flag, wire, sizeof(wire), &len)) {
        log::warnf("수배 해제: wire 를 못 만들었다 (핸들 0x{:08X})", handle);
        return false;
    }
    // 세션 고르기는 한 곳에만 둔다. 로드 직후 죽은 세션이 뽑히던
    // 일이 있어 다른 경로도 전부 이 함수를 쓴다(grant.h).
    const std::uintptr_t session = pick_drive_session(reader);
    if (session == 0) {
        log::warnf("수배 해제: 구동할 세션을 못 골랐다 (월드 안입니까?)");
        return false;
    }
    log::infof("수배 해제 요청: 핸들 0x{:08X} 플래그 {} 세션 0x{:X}", handle,
               flag, session);
    return request_message(session, g_clear_msg, wire, len);
}

bool request_change_wanted_state(const mem::Reader& reader,
                                 std::uint32_t handle, std::uint8_t state,
                                 std::uint8_t extra) {
    if (g_state_msg.descriptor == 0) {
        log::warnf("수배 상태: 메시지가 아직 준비되지 않았다");
        return false;
    }
    std::uint8_t wire[kChangeWantedStateWireLen]{};
    std::size_t len = 0;
    if (!build_change_wanted_state_wire(handle, state, extra, wire,
                                        sizeof(wire), &len)) {
        log::warnf("수배 상태: wire 를 못 만들었다 (핸들 0x{:08X})", handle);
        return false;
    }
    const std::uintptr_t session = pick_drive_session(reader);
    if (session == 0) {
        log::warnf("수배 상태: 구동할 세션을 못 골랐다 (월드 안입니까?)");
        return false;
    }
    // 보낸 값을 그대로 남긴다 - 뜻을 모르는 채 쓸어 보는 중이라,
    // 어느 조합에서 화면이 바뀌었는지 나중에 짝지을 수 있어야 한다.
    log::infof("수배 상태 요청: 핸들 0x{:08X} 상태 {} extra {} 세션 0x{:X}",
               handle, state, extra, session);
    return request_message(session, g_state_msg, wire, len);
}

}  // namespace cdtb::game

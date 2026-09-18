#include "game/mountvital.h"

#include <atomic>
#include <mutex>
#include <utility>

#include "core/log.h"
#include "core/write_log.h"
#include "game/actors.h"
#include "mem/safe_read.h"

namespace cdtb::game {
namespace {

std::mutex g_mtx;
MountPin g_pin;

// 권위 사본 찾기 결과: {핸들 -> ServerStatusActorComponent}. 주소를 바로 들지
// 않는 이유는 `mountvital.h` 머리에 적었다 - 쓸 때마다 컴포넌트에서 다시 내려간다.
std::mutex g_auth_mtx;
std::vector<std::pair<std::uint32_t, std::uintptr_t>> g_auth;
std::atomic<bool> g_auth_refresh{false};

std::uint64_t rd64(const mem::Reader& r, std::uintptr_t a) {
    std::uint64_t v = 0;
    return r.read_value(a, &v) ? v : 0;
}
std::uint32_t rd32(const mem::Reader& r, std::uintptr_t a) {
    std::uint32_t v = 0;
    return r.read_value(a, &v) ? v : 0;
}
std::int64_t rdi64(const mem::Reader& r, std::uintptr_t a) {
    std::int64_t v = 0;
    return r.read_value(a, &v) ? v : 0;
}

bool vp(std::uintptr_t p) { return p >= 0x10000; }

// 액터 -> 게이지 배열. 못 내려가면 0.
std::uintptr_t gauge_array(const mem::Reader& r, std::uintptr_t actor) {
    if (!vp(actor)) return 0;
    const auto sub = static_cast<std::uintptr_t>(rd64(r, actor + kMvActorSub));
    if (!vp(sub)) return 0;
    const auto marker = static_cast<std::uintptr_t>(rd64(r, sub + kMvMarker));
    if (!vp(marker)) return 0;
    const auto root = static_cast<std::uintptr_t>(rd64(r, marker + kMvRoot));
    if (!vp(root)) return 0;
    const auto arr = static_cast<std::uintptr_t>(rd64(r, root + kMvArray));
    return vp(arr) ? arr : 0;
}

// 배열에서 그 타입의 항목 번호. 없으면 -1.
int find_type(const mem::Reader& r, std::uintptr_t arr, std::uint32_t type) {
    for (int k = 0; k < kGaugeMaxEntries; ++k) {
        const std::uintptr_t e = arr + gauge_offset(k);
        if (rd32(r, e + kGaugeType) == type) return k;
    }
    return -1;
}

// 핸들로 살아있는 액터를 **다시 찾는다.** 주소는 들고 다니지 않는다.
std::uintptr_t actor_by_handle(std::uint32_t handle) {
    if (handle == 0) return 0;
    for (const auto& a : live_actors()) {
        if (a.handle == handle) return a.actor;
    }
    return 0;
}

// 한 칸을 쓴다. 안 쓸 칸(-1)이면 아무것도 안 한다.
bool write_field(std::uintptr_t entry, std::size_t off, std::int64_t value) {
    if (!mount_pin_wants(value)) return false;
    const std::int64_t v = mount_clamp(value);
    return mem::safe_write_bytes(entry + off, &v, sizeof v);
}

// 배열 하나에 설정을 적용한다. 쓴 칸 수를 돌려준다.
int apply_to(const mem::Reader& r, std::uintptr_t arr, const MountPin& p) {
    if (arr == 0) return 0;
    // 게이트: 항목[0] 이 체력이어야 게이지 배열이다(player.h 와 같은 판정).
    if (rd32(r, arr + kGaugeType) != kTypeHealth) return 0;

    int done = 0;
    if (mount_pin_wants(p.hp_cur) || mount_pin_wants(p.hp_max)) {
        const int k = find_type(r, arr, kTypeHealth);
        if (k >= 0) {
            const std::uintptr_t e = arr + gauge_offset(k);
            if (write_field(e, kGaugeCur, p.hp_cur)) ++done;
            // 최대는 **두 칸**이다 - 유도값과 기준값(mountvital.h 머리).
            if (write_field(e, kGaugeMax, p.hp_max)) ++done;
            write_field(e, kGaugeBaseMax, p.hp_max);
        }
    }
    if (mount_pin_wants(p.sta_cur) || mount_pin_wants(p.sta_max)) {
        const int k = find_type(r, arr, kTypeStamina);
        if (k >= 0) {
            const std::uintptr_t e = arr + gauge_offset(k);
            if (write_field(e, kGaugeCur, p.sta_cur)) ++done;
            if (write_field(e, kGaugeMax, p.sta_max)) ++done;
            write_field(e, kGaugeBaseMax, p.sta_max);
        }
    }
    return done;
}

// 한 대상에 설정을 적용한다 - **거울과 권위 둘 다**. 쓴 칸 수를 돌려준다.
//
// 거울에도 쓰는 이유는 화면이 곧바로 바뀌게 하려는 것이다(권위만 쓰면 다음
// 회복 틱까지 옛 숫자가 보인다). 진짜로 먹는 것은 권위 쪽이다.
int apply(const mem::Reader& r, std::uintptr_t actor, const MountPin& p) {
    int done = apply_to(r, gauge_array(r, actor), p);
    const std::uintptr_t auth = mount_authority_gauges(r, p.handle);
    if (auth != 0) done += apply_to(r, auth, p);
    return done;
}

}  // namespace

// --------------------------------------------------------- 순수 부분

bool mount_type_safe(std::uint32_t type) {
    // 17·18·48 은 `player.h` 가 "발열·자연발화·탈것 화염" 이라고 적어 둔 칸이다.
    // **그 이름도 위험하다는 판정도 측정된 적이 없다**(mountvital.h 머리 주석).
    // 여기서 빼 두는 이유는 "뜻을 모르는 칸을 건드릴 이유가 없다" 이지 "위험이
    // 확인됐다" 가 아니다 - 근거를 부풀리지 않으려고 이렇게 적는다.
    return type != 17 && type != 18 && type != 48;
}

std::size_t gauge_offset(int index) {
    return static_cast<std::size_t>(index) * kGaugeStride;
}

std::int64_t mount_clamp(std::int64_t value) {
    if (value < 0) return 0;
    return value > kMountVitalMax ? kMountVitalMax : value;
}

bool mount_pin_wants(std::int64_t field) { return field >= 0; }

bool mount_handle_plausible(std::uint32_t handle) {
    const std::uint32_t ns = handle >> 16;
    return ns == 0x9010 || ns == 0xA010 || ns == 0xB010;
}

bool mount_pin_active(const MountPin& pin) {
    if (pin.handle == 0) return false;
    return mount_pin_wants(pin.hp_cur) || mount_pin_wants(pin.hp_max) ||
           mount_pin_wants(pin.sta_cur) || mount_pin_wants(pin.sta_max);
}

// --------------------------------------------------------- 권위 사본

int mount_authority_discover(const mem::Rtti& rtti, const mem::Reader& reader) {
    const auto insts =
        rtti.instances_of_class(kMvServerStatusClass, kMvAuthorityMax);
    std::vector<std::pair<std::uint32_t, std::uintptr_t>> found;
    for (const std::uintptr_t comp : insts) {
        // **끝까지 내려가지는 것만 받는다.** 다중 상속이면 서브객체 주소도
        // 함께 오고(rtti.h), vtable 값을 우연히 담은 메모리도 섞인다.
        const auto actor =
            static_cast<std::uintptr_t>(rd64(reader, comp + kMvStatusActor));
        if (!vp(actor)) continue;
        const std::uint32_t h = rd32(reader, actor + kMvActorHandle);
        if (!mount_handle_plausible(h)) continue;
        const auto root =
            static_cast<std::uintptr_t>(rd64(reader, comp + kMvRoot));
        if (!vp(root)) continue;
        const auto arr =
            static_cast<std::uintptr_t>(rd64(reader, root + kMvArray));
        if (!vp(arr) || rd32(reader, arr + kGaugeType) != kTypeHealth) continue;
        bool dup = false;
        for (const auto& e : found) {
            if (e.first == h) dup = true;
        }
        if (!dup) found.emplace_back(h, comp);
    }
    const int n = static_cast<int>(found.size());
    {
        std::lock_guard<std::mutex> lk(g_auth_mtx);
        g_auth.swap(found);
    }
    log::infof("탈것 권위 사본: {} 후보 {}개 중 {}개를 짝지었다",
               kMvServerStatusClass, insts.size(), n);
    return n;
}

std::uintptr_t mount_authority_gauges(const mem::Reader& reader,
                                      std::uint32_t handle) {
    if (handle == 0) return 0;
    std::uintptr_t comp = 0;
    {
        std::lock_guard<std::mutex> lk(g_auth_mtx);
        for (const auto& e : g_auth) {
            if (e.first == handle) comp = e.second;
        }
    }
    if (!vp(comp)) return 0;
    // 핸들을 다시 확인한다 - 탈것을 돌려보내면 그 객체가 다른 것에 재사용된다.
    const auto actor =
        static_cast<std::uintptr_t>(rd64(reader, comp + kMvStatusActor));
    if (!vp(actor) || rd32(reader, actor + kMvActorHandle) != handle) return 0;
    const auto root = static_cast<std::uintptr_t>(rd64(reader, comp + kMvRoot));
    if (!vp(root)) return 0;
    const auto arr = static_cast<std::uintptr_t>(rd64(reader, root + kMvArray));
    if (!vp(arr) || rd32(reader, arr + kGaugeType) != kTypeHealth) return 0;
    return arr;
}

std::size_t mount_authority_count() {
    std::lock_guard<std::mutex> lk(g_auth_mtx);
    return g_auth.size();
}

void mount_authority_request_refresh() {
    g_auth_refresh.store(true, std::memory_order_release);
}

bool mount_authority_take_refresh() {
    return g_auth_refresh.exchange(false, std::memory_order_acq_rel);
}

// --------------------------------------------------------- 읽기

std::vector<MountVital> mount_vitals(const mem::Reader& reader) {
    std::vector<MountVital> out;
    for (const auto& a : live_actors()) {
        if (!a.is_companion()) continue;
        MountVital m;
        m.actor = a.actor;
        m.handle = a.handle;
        m.name = a.display();
        m.merc_row = a.merc_row;
        m.gauges = gauge_array(reader, a.actor);
        if (m.gauges == 0) {
            out.push_back(m);
            continue;
        }
        if (rd32(reader, m.gauges + kGaugeType) != kTypeHealth) {
            out.push_back(m);   // 게이트 실패 - 주소만 보여 준다
            continue;
        }
        m.ok = true;
        m.authority = mount_authority_gauges(reader, m.handle);
        m.hp_idx = find_type(reader, m.gauges, kTypeHealth);
        m.sta_idx = find_type(reader, m.gauges, kTypeStamina);
        if (m.hp_idx >= 0) {
            const std::uintptr_t e = m.gauges + gauge_offset(m.hp_idx);
            m.hp_cur = rdi64(reader, e + kGaugeCur);
            m.hp_max = rdi64(reader, e + kGaugeMax);
        }
        if (m.sta_idx >= 0) {
            const std::uintptr_t e = m.gauges + gauge_offset(m.sta_idx);
            m.sta_cur = rdi64(reader, e + kGaugeCur);
            m.sta_max = rdi64(reader, e + kGaugeMax);
        }
        out.push_back(m);
    }
    return out;
}

// --------------------------------------------------------- 쓰기 · 고정

bool mount_vital_write(const mem::Reader& reader, std::uint32_t handle,
                       const MountPin& what) {
    const std::uintptr_t actor = actor_by_handle(handle);
    if (actor == 0) {
        log::warnf("탈것 수치: 핸들 {:08X} 를 살아있는 목록에서 못 찾았다",
                   handle);
        return false;
    }
    MountPin p = what;
    p.handle = handle;
    const std::uintptr_t auth = mount_authority_gauges(reader, handle);
    const int n = apply(reader, actor, p);
    if (n == 0) {
        log::warnf("탈것 수치: 핸들 {:08X} 에 한 칸도 못 썼다", handle);
        return false;
    }
    log_write("탈것 수치", actor, "-", "적용");
    // **권위 사본까지 갔는지 남긴다.** 거울에만 쓰면 회복 틱에 곧 되돌아가는데,
    // 로그가 "썼다" 로 끝나면 그 차이를 나중에 못 가른다(2026-09-18 의 교훈).
    log::infof("탈것 수치: 핸들 {:08X} 에 {}칸 - 권위 사본 {}", handle, n,
               auth != 0 ? "포함" : "**없음(거울만, 곧 되돌아간다)**");
    return true;
}

void mount_pin_set(const MountPin& pin) {
    std::lock_guard<std::mutex> lk(g_mtx);
    const bool was = mount_pin_active(g_pin);
    g_pin = pin;
    const bool now = mount_pin_active(g_pin);
    if (was != now) {
        log_write("탈것 수치 고정", pin.handle, was ? "on" : "off",
                  now ? "on" : "off");
        log::infof("탈것 수치 고정 {} (핸들 {:08X})", now ? "켬" : "끔",
                   pin.handle);
    }
}

MountPin mount_pin_get() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_pin;
}

void mount_pin_clear() {
    MountPin empty;
    mount_pin_set(empty);
}

void mount_pin_tick(const mem::Reader& reader) {
    MountPin p;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        p = g_pin;
    }
    if (!mount_pin_active(p)) return;
    // **주소를 들고 다니지 않는다** - 매 틱 핸들로 다시 찾는다. 탈것이 사라지면
    // 그 틱은 조용히 건너뛴다(로그를 남기면 매 프레임 찍힌다).
    const std::uintptr_t actor = actor_by_handle(p.handle);
    if (actor == 0) return;
    apply(reader, actor, p);
}

}  // namespace cdtb::game

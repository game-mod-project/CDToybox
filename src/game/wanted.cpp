#include "game/wanted.h"

#include <windows.h>   // GetTickCount64 - 재탐색 간격을 막는 데 쓴다

#include <algorithm>   // std::sort - 합친 표를 키 순으로 낸다
#include <atomic>
#include <cstring>
#include <string>

#include "core/log.h"
#include "game/grant.h"
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
// **벡터다.** {데이터 ptr, 크기 u32, 용량 u32} - 단일 포인터가 아니다
// (2026-09-19 정정, wanted.h 머리말).
constexpr std::size_t kCompRegionVec = 0x30;
constexpr std::size_t kVecSize = 0x38;
constexpr std::size_t kVecCap = 0x3C;
constexpr std::size_t kCompOwner = 0x08;     // 주인 액터 (진짜/가짜를 가른다)
constexpr std::size_t kRegionKey = 0x28;    // u32 구역 키
constexpr std::size_t kRegionFine = 0x30;   // u64 벌금 (2자리 고정소수)

// 지역 레코드의 클래스. vtable 을 **힙을 안 훑고** 이름으로 얻는 데 쓴다.
constexpr const char* kRegionClass = ".?AVWantedRegionData@pa@@";

// realm 셋. 같은 모양의 컴포넌트가 realm 마다 하나씩 산다(wanted.h 머리말).
struct RealmState {
    const char* cls;
    const char* label;
    std::uintptr_t comp = 0;
    std::uintptr_t comp_vtable = 0;
};

RealmState g_realm[kWantedRealmCount] = {
    {".?AVClientSelfWantedActorComponent@pa@@", "클라"},
    {".?AVServerWantedActorComponent@pa@@", "서버"},
    {".?AVCommonWantedActorComponent@pa@@", "커먼"},
};

RealmState* realm_at(WantedRealm realm) {
    const int i = static_cast<int>(realm);
    if (i < 0 || i >= kWantedRealmCount) return nullptr;
    return &g_realm[i];
}

// 월드 안에서 몇 번까지 스스로 훑어 볼 것인가. 다 쓰면 그만두고, 화면에서
// 눌러야 다시 본다. 한 번에 ~45초짜리 힙 전수라 이 수를 늘리면 비싸진다.
constexpr int kFindTries = 3;
int g_find_tries = 0;
// 지역 객체의 vtable. 찾을 때 RTTI 이름으로 확인해 둔 것이라, 그 뒤에는
// 이름 조회 없이 이 값만 대조하면 된다(매 프레임 도는 자리다).
std::uintptr_t g_region_vtable = 0;

// 살아 있는가. 세이브를 다시 부르면 그 자리에 다른 객체가 들어앉는다 -
// 실제로 좌표·쿼터니언 뭉치를 읽을 뻔했다. vtable 로 거른다.
bool comp_alive(const mem::Reader& reader, const RealmState& rs) {
    if (rs.comp == 0 || rs.comp_vtable == 0) return false;
    std::uint64_t vt = 0;
    if (!reader.read_value(rs.comp, &vt)) return false;
    return static_cast<std::uintptr_t>(vt) == rs.comp_vtable;
}

// 컴포넌트 -> 구역 기록 **전부**. 하나도 못 얻으면 빈 목록이다.
//
// **주소를 들고 있지 않는다.** 부를 때마다 벡터 머리부터 다시 읽는다 -
// 구역이 하나 늘면 재할당돼 옛 주소가 죽는다(wanted.h 머리말의 실측).
std::vector<WantedRegion> regions_now(const mem::Reader& reader,
                                      const RealmState& rs) {
    std::vector<WantedRegion> out;
    if (!comp_alive(reader, rs) || g_region_vtable == 0) return out;
    std::uint64_t data = 0;
    std::uint32_t size = 0, cap = 0;
    if (!reader.read_value(rs.comp + kCompRegionVec, &data)) return out;
    if (!reader.read_value(rs.comp + kVecSize, &size)) return out;
    if (!reader.read_value(rs.comp + kVecCap, &cap)) return out;
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

const char* wanted_realm_name(WantedRealm realm) {
    const RealmState* rs = realm_at(realm);
    return rs != nullptr ? rs->label : "?";
}

bool wanted_component_find(const mem::Rtti& rtti, const mem::Reader& reader) {
    // 죽은 자리는 먼저 비운다. 세이브를 다시 부르면 그 자리에 다른 객체가
    // 들어앉으므로, 비워 두지 않으면 영영 다시 안 찾는다.
    bool all = (g_region_vtable != 0);
    for (auto& rs : g_realm) {
        if (rs.comp != 0 && !comp_alive(reader, rs)) {
            rs.comp = 0;
            rs.comp_vtable = 0;
        }
        if (rs.comp == 0) all = false;
    }
    if (all) return true;

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
    // 월드에 막 들어온 순간에는 아직 없을 수 있고, realm 이 셋이라 한 통과에
    // 다 안 잡힐 수도 있다.
    if (g_find_tries >= kFindTries) return false;
    static std::uint64_t s_last_ms = 0;
    const std::uint64_t now = ::GetTickCount64();
    if (s_last_ms != 0 && now - s_last_ms < 15000) return false;
    s_last_ms = now;
    ++g_find_tries;

    // 지역 레코드의 vtable 은 **힙을 안 훑고** 얻는다 - RTTI 표에서 이름으로
    // 바로 내려간다. 예전에는 컴포넌트를 잡을 때 딸려 온 첫 원소에서 읽었고,
    // 그래서 범죄 기록이 없으면 못 얻었다(오늘 3회 실패의 절반이 이것이다).
    if (g_region_vtable == 0) {
        for (const auto& t : rtti.find_types(kRegionClass, 8)) {
            if (t.name != kRegionClass) continue;
            const auto vts = rtti.vtables_for(t.descriptor);
            if (!vts.empty()) {
                g_region_vtable = vts.front();
                log::infof("수배 지역 레코드 vtable: 0x{:X}", g_region_vtable);
                break;
            }
        }
    }

    // **힙은 한 번만 훑는다.** 클래스마다 instances_of_class 를 부르면 realm
    // 수만큼 전수 탐색이 된다 - 카메라에서 vtable 7개로 122초를 겪었다.
    std::vector<std::string> names;
    names.reserve(kWantedRealmCount);
    for (const auto& rs : g_realm) names.emplace_back(rs.cls);
    // 상한은 전체 수다(클래스별이 아니다) **그리고 낮은 주소부터 채운다**.
    // 등록표 행이 낮은 주소에 있어 예산을 먼저 먹으므로 넉넉히 준다
    // (TROUBLESHOOTING 4.33). 실측 총 5개라 32 면 남는다.
    const auto found = rtti.find_objects_of(names, 32);

    int got = 0;
    const std::size_t seen = found.size();
    for (const auto& f : found) {
        RealmState* rs = nullptr;
        for (auto& r : g_realm) {
            if (f.cls == r.cls) {
                rs = &r;
                break;
            }
        }
        if (rs == nullptr || rs->comp != 0) continue;
        std::uint64_t vt = 0;
        if (!reader.read_value(f.address, &vt) || vt == 0) continue;
        // **진짜는 주인이 액터다.** 가짜는 등록표 행이고 여기가 작은 정수다
        // (0x565 · 0x37E2). wanted.h 머리말의 실측.
        std::uint64_t owner = 0;
        if (!reader.read_value(f.address + kCompOwner, &owner) || owner == 0) {
            continue;
        }
        const std::string ocls =
            rtti.class_of_object(static_cast<std::uintptr_t>(owner));
        if (ocls.find("Actor") == std::string::npos) {
            log::infof("수배 컴포넌트 후보 0x{:X}({}) 는 건너뛴다 - 주인이 {}",
                       f.address, rs->label,
                       ocls.empty() ? "이름 없음" : ocls);
            continue;
        }
        rs->comp = f.address;
        rs->comp_vtable = static_cast<std::uintptr_t>(vt);
        ++got;
        log::infof("수배 컴포넌트 {}: 0x{:X} (주인 0x{:X} {})", rs->label,
                   rs->comp, owner, ocls);
    }
    if (got > 0 || wanted_component_ready()) {
        if (g_region_vtable == 0) {
            log::warnf("수배: 지역 레코드 vtable 을 못 얻었다 - 값은 못 읽는다");
        }
        return true;
    }
    const auto st = mem::Rtti::scan_stats();
    const bool last = g_find_tries >= kFindTries;
    log::warnf("수배 컴포넌트를 못 찾았다 ({}/{}회{}) - 후보 {}개, "
               "힙 훑기 누적: 창 {} · 쪼갠 창 {} · 끝내 못 읽음 {}KB",
               g_find_tries, kFindTries,
               last ? ", **그만 찾는다 - 창에서 [다시 찾기]**" : "", seen,
               st.windows, st.retried, st.lost_kb);
    return false;
}

bool bounty_ready(const mem::Reader& reader) {
    for (const auto& rs : g_realm) {
        if (!regions_now(reader, rs).empty()) return true;
    }
    return false;
}

std::size_t wanted_region_count(std::uint64_t data, std::uint32_t size,
                                std::uint32_t cap) {
    if (data == 0 || size == 0) return 0;
    if (size > cap) return 0;                 // 머리가 깨졌다
    if (cap > kWantedRegionCapMax) return 0;  // 말이 안 되는 용량
    return size;
}

bool WantedRegionRow::disagrees() const {
    bool seen = false;
    std::uint64_t first = 0;
    for (int i = 0; i < kWantedRealmCount; ++i) {
        if (!have[i]) continue;
        if (!seen) {
            first = raw[i];
            seen = true;
        } else if (raw[i] != first) {
            return true;
        }
    }
    return false;
}

std::vector<WantedRegionRow> merge_region_rows(
    const std::vector<WantedRegion> (&per_realm)[kWantedRealmCount]) {
    std::vector<WantedRegionRow> out;
    for (int i = 0; i < kWantedRealmCount; ++i) {
        for (const auto& r : per_realm[i]) {
            WantedRegionRow* row = nullptr;
            for (auto& x : out) {
                if (x.key == r.key) {
                    row = &x;
                    break;
                }
            }
            if (row == nullptr) {
                out.push_back(WantedRegionRow{});
                row = &out.back();
                row->key = r.key;
            }
            // 한 realm 에 같은 키가 두 번 나오면 **첫 칸을 지키지 않는다** -
            // 뒤엣것이 최신이라고 볼 근거가 없으므로 먼저 읽은 것을 남긴다.
            if (!row->have[i]) {
                row->have[i] = true;
                row->raw[i] = r.raw;
            }
        }
    }
    std::sort(out.begin(), out.end(),
              [](const WantedRegionRow& a, const WantedRegionRow& b) {
                  return a.key < b.key;
              });
    return out;
}

bool wanted_regions_of(const mem::Reader& reader, WantedRealm realm,
                       std::vector<WantedRegion>* out) {
    if (out == nullptr) return false;
    const RealmState* rs = realm_at(realm);
    if (rs == nullptr) {
        out->clear();
        return false;
    }
    *out = regions_now(reader, *rs);
    return !out->empty();
}

bool wanted_region_rows(const mem::Reader& reader,
                        std::vector<WantedRegionRow>* out) {
    if (out == nullptr) return false;
    std::vector<WantedRegion> per_realm[kWantedRealmCount];
    for (int i = 0; i < kWantedRealmCount; ++i) {
        per_realm[i] = regions_now(reader, g_realm[i]);
    }
    *out = merge_region_rows(per_realm);
    return !out->empty();
}

bool wanted_component_ready() {
    for (const auto& rs : g_realm) {
        if (rs.comp != 0 && rs.comp_vtable != 0) return true;
    }
    return false;
}

bool wanted_find_gave_up() {
    return !wanted_component_ready() && g_find_tries >= kFindTries;
}

void wanted_find_rearm() {
    g_find_tries = 0;
    log::infof("수배 컴포넌트: 다시 찾는다 ({}회까지)", kFindTries);
}

int bounty_write_region(const mem::Reader& reader, std::uint32_t key,
                        std::uint64_t raw) {
    // **realm 마다 따로 쓴다.** 클라만 쓰면 서버 값이 그대로 남는다 -
    // 2026-09-20 실측에서 서버 100.00 / 클라 30.00 이었다(wanted.h 머리말).
    int wrote = 0;
    for (const auto& rs : g_realm) {
        // **여기서 다시 읽는다.** 화면이 들고 있던 주소로 쓰면 재할당된 뒤
        // 죽은 배열에 쓴다 - 실측 2026-09-19 에 실제로 그랬다.
        for (const auto& r : regions_now(reader, rs)) {
            if (r.key != key) continue;
            std::uint64_t before = 0;
            reader.read_value(r.addr + kRegionFine, &before);
            if (!mem::safe_write_bytes(r.addr + kRegionFine, &raw,
                                       sizeof raw)) {
                log::warnf("벌금 쓰기 실패: {} 구역 {} 0x{:X}", rs.label, key,
                           r.addr + kRegionFine);
                break;
            }
            // 쓴 값을 그대로 남긴다. 되돌릴 일이 생기면 이 줄이 원본이다.
            log::infof("벌금: {} 구역 {} {} -> {} (0x{:X})", rs.label, key,
                       before, raw, r.addr + kRegionFine);
            ++wrote;
            break;
        }
    }
    if (wrote == 0) {
        log::warnf("벌금 쓰기: 구역 {} 의 기록을 어느 realm 에서도 못 찾았다",
                   key);
    }
    return wrote;
}

bool bounty_clear_all(const mem::Reader& reader, int* changed_out) {
    if (changed_out != nullptr) *changed_out = 0;
    std::vector<WantedRegionRow> rows;
    if (!wanted_region_rows(reader, &rows)) {
        log::warnf("벌금 전부 0: 구역 기록을 못 얻었다");
        return false;
    }
    int changed = 0;
    for (const auto& row : rows) {
        bool need = false;
        for (int i = 0; i < kWantedRealmCount; ++i) {
            if (row.have[i] && row.raw[i] != 0) need = true;
        }
        if (!need) continue;
        if (bounty_write_region(reader, row.key, 0) > 0) ++changed;
    }
    if (changed_out != nullptr) *changed_out = changed;
    log::infof("벌금 전부 0: 구역 {}개 중 {}개를 바꿨다", rows.size(), changed);
    return true;
}

const char* wanted_region_name(std::uint32_t key) {
    // **화면에서 본 것만 적는다**(wanted.h 머리말). 추측은 안 적는다.
    switch (key) {
        case 1000131: return "에르난드 공국";   // 2026-09-20 지도 확인
        case 1000138: return "데메니스 왕국";   // 2026-09-18 지도 확인
        default: return nullptr;
    }
}

const char* wanted_state_name(std::uint8_t state) {
    // 표본으로 확인한 것만 이름을 준다(2026-09-20, 전단 둘째 줄과 대조).
    switch (state) {
        case 1: return "수색";
        case 2: return "체포";
        default: return "?";
    }
}

bool wanted_now(const mem::Reader& reader, WantedNow* out) {
    if (out == nullptr) return false;
    *out = WantedNow{};
    bool any = false;
    for (int i = 0; i < kWantedRealmCount; ++i) {
        const RealmState& rs = g_realm[i];
        if (!comp_alive(reader, rs)) continue;
        std::uint8_t st = 0;
        std::uint32_t nr = 0, nw = 0, nc = 0;
        if (!reader.read_value(rs.comp + kCompState, &st)) continue;
        if (!reader.read_value(rs.comp + kVecSize, &nr)) continue;
        if (!reader.read_value(rs.comp + kCompWitnessSize, &nw)) continue;
        if (!reader.read_value(rs.comp + kCompRecordSize, &nc)) continue;
        out->have[i] = true;
        out->state[i] = st;
        out->regions[i] = nr;
        out->witness[i] = nw;
        out->records[i] = nc;
        any = true;
    }
    return any;
}

int wanted_purge(const mem::Reader& reader, const WantedPurge& what) {
    // 벌금부터. 레코드를 없애기 전에 써야 값이 남지 않는다 - 벡터만 비우면
    // 원소는 용량 안에 그대로 있고, 게임이 그 자리에 다시 push 하면 옛 값을
    // 덮어쓰는 대신 **읽을** 수도 있다. 순서가 공짜니 안전한 쪽으로 둔다.
    if (what.bounty) {
        int changed = 0;
        bounty_clear_all(reader, &changed);
    }

    const std::uint32_t zero32 = 0;
    const std::uint8_t zero8 = 0;
    int touched = 0;
    for (int i = 0; i < kWantedRealmCount; ++i) {
        const RealmState& rs = g_realm[i];
        if (!comp_alive(reader, rs)) continue;
        bool did = false;

        // 지역 벡터. **크기만** 0 으로 내린다 - 원소를 지우거나 포인터를
        // 건드리면 게임의 해제 경로가 무엇을 할지 모른다.
        if (what.regions) {
            std::uint32_t before = 0;
            reader.read_value(rs.comp + kVecSize, &before);
            if (before != 0 && mem::safe_write_bytes(rs.comp + kVecSize, &zero32,
                                                    sizeof zero32)) {
                log::infof("수배 지우기: {} 지역 {} -> 0 (0x{:X})", rs.label,
                           before, rs.comp + kVecSize);
                did = true;
            }
        }
        if (what.witness) {
            for (const auto off : {kCompWitnessSize, kCompRecordSize}) {
                std::uint32_t before = 0;
                reader.read_value(rs.comp + off, &before);
                if (before == 0) continue;
                if (mem::safe_write_bytes(rs.comp + off, &zero32,
                                          sizeof zero32)) {
                    log::infof("수배 지우기: {} {} {} -> 0 (0x{:X})", rs.label,
                               off == kCompWitnessSize ? "목격자" : "범죄기록",
                               before, rs.comp + off);
                    did = true;
                }
            }
        }
        if (what.state) {
            std::uint8_t before = 0;
            reader.read_value(rs.comp + kCompState, &before);
            if (before != 0 && mem::safe_write_bytes(rs.comp + kCompState,
                                                    &zero8, sizeof zero8)) {
                log::infof("수배 지우기: {} 상태 {}({}) -> 0 (0x{:X})",
                           rs.label, before, wanted_state_name(before),
                           rs.comp + kCompState);
                did = true;
            }
        }
        if (did) ++touched;
    }
    if (touched == 0) {
        log::warnf("수배 지우기: 손댈 것이 없었다 (이미 비었거나 못 읽었다)");
    }
    return touched;
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

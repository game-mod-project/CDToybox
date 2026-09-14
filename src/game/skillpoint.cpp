#include "game/skillpoint.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include "core/log.h"
#include "core/write_log.h"

namespace cdtb::game {
namespace {

constexpr const char* kKnowClass = ".?AVServerKnowledgeActorComponent@pa@@";
constexpr const char* kKnowClassClient = ".?AVClientKnowledgeActorComponent@pa@@";
// 구조체로 가는 포인터가 있는 자리(실측 2026-09-13).
constexpr std::size_t kBondPtr = 0xC8;

std::atomic<std::uintptr_t> g_know{0};
std::atomic<std::uintptr_t> g_know_client{0};
// **발견할 때 본 vtable.** 생존 검사의 기준이다 - RVA 를 코드에 박지 않아도 되고,
// 게임이 갱신돼도 저절로 따라간다.
std::atomic<std::uint64_t> g_know_vt[2] = {};

std::mutex g_mtx;
std::vector<BondBackup> g_backup;

// **쓰기 전체를 직렬화한다.** 화면(렌더 스레드)이 더하기·되돌리기를 부르고, 분석
// 스레드가 생존 검사로 컴포넌트를 버릴 수 있다. 읽고 쓰는 사이에 그 일이 나면
// 낡은 주소에 쓰게 된다(가방에서 같은 자리가 중대였다).
std::mutex g_op_mtx;

// 탐색 스로틀. RTTI 인스턴스 탐색은 힙 전수라 값싸지 않다 - 못 찾은 동안 매 바퀴
// 훑으면 2초마다 11GB 를 읽는다. 이웃들(인벤토리·명부)이 전부 막아 둔 비용이다.
std::atomic<int> g_tries{0};
constexpr int kGiveUp = 12;        // 이만큼 해 보고 그만둔다
constexpr int kEverySpins = 5;     // 그 전에는 5바퀴(약 10초)에 한 번

// 캐시한 컴포넌트를 **언제부터** 못 읽고 있나(realm 별, 0 이면 멀쩡하다).
// 분석 스레드 한 곳에서만 읽고 쓴다.
std::chrono::steady_clock::time_point g_dead_since[2];
constexpr auto kDeadFor = std::chrono::seconds(10);

// 인프로세스 직접 쓰기(주입 DLL 전용). SEH 로 감싼다.
bool wr16(std::uintptr_t a, std::uint16_t v) {
    __try {
        *reinterpret_cast<volatile std::uint16_t*>(a) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// **컴포넌트가 진짜 그것인지 구조로 본다.** RTTI 탐색은 "그 vtable 값이 들어 있는
// 메모리" 도 잡는다 - 실측 2026-09-14: 후보 둘이 **16바이트 간격**으로 나왔고
// (0x110D4CEE90 / 0x110D4CEEA0), 결속 포인터가 0x100000001 같은 값이었다. 실제
// 컴포넌트는 0x1A8 바이트가 넘는 객체라 16바이트 간격일 수 없다. 그 쓰레기를 들고
// 화면이 "보유 4724 · 총합 44302" 를 그렸다.
//
// 지식 레벨 표(+0x18 데이터 / +0x20 개수 / +0x24 용량)는 Initialize 가 전체 지식
// 수만큼 한 번에 잡으므로, 그 셋이 앞뒤가 맞는지 보면 가짜가 걸린다.
bool comp_looks_real(const mem::Reader& reader, std::uintptr_t comp) {
    if (comp == 0) return false;
    std::uint64_t data = 0;
    std::uint32_t count = 0, cap = 0;
    if (!reader.read_value(comp + 0x18, &data) || data < 0x10000) return false;
    if (!reader.read_value(comp + 0x20, &count) || count == 0 ||
        count > 100000) {
        return false;
    }
    if (!reader.read_value(comp + 0x24, &cap) || cap < count) return false;
    // 표의 첫 레코드와 마지막 레코드가 읽혀야 한다 - 개수가 진짜라는 뜻이다.
    std::uint64_t probe = 0;
    const std::uintptr_t last =
        static_cast<std::uintptr_t>(data) +
        (static_cast<std::uintptr_t>(count) - 1) * 24;
    return reader.read_value(static_cast<std::uintptr_t>(data), &probe) &&
           reader.read_value(last, &probe);
}

// **플레이어 액터에서 내려오는 사슬.** 이것이 정공법이다 - 신원이 확실하다.
//   comp = *(u64*)( *(u64*)(액터 + 0x68) + 0x150 )
// 게임 코드 세 곳이 같은 사슬을 쓴다(RVA 0x0208BBF5 / 0x0208738B / 0x026BA3A0).
std::uintptr_t comp_from_player(const mem::Reader& reader,
                                std::uintptr_t actor) {
    if (actor == 0) return 0;
    std::uint64_t sub = 0;
    if (!reader.read_value(actor + 0x68, &sub) || sub == 0) return 0;
    std::uint64_t comp = 0;
    if (!reader.read_value(static_cast<std::uintptr_t>(sub) + 0x150, &comp) ||
        comp == 0) {
        return 0;
    }
    const std::uintptr_t c = static_cast<std::uintptr_t>(comp);
    // 되짚어 확인한다 - 그 컴포넌트의 +0x08 이 우리가 온 액터여야 한다.
    std::uint64_t back = 0;
    if (!reader.read_value(c + 0x08, &back) ||
        static_cast<std::uintptr_t>(back) != actor) {
        return 0;
    }
    return comp_looks_real(reader, c) ? c : 0;
}

std::uintptr_t comp_of(int realm) {
    return realm == 1 ? knowledge_component_client() : knowledge_component();
}

}  // namespace

BondPlan plan_bond_add(int have, int total, int add, bool also_total) {
    BondPlan p;
    if (have < 0 || total < 0) {
        p.skip = "값이 음수다";
        return p;
    }
    // **보유는 총합을 넘을 수 없다.** 화면이 `사용 = 총합 - 보유` 로 그리므로, 넘으면
    // 사용이 음수가 되는 모양이다 - 우리가 아는 구조가 아니다.
    if (have > total) {
        p.skip = "보유가 총합보다 크다";
        return p;
    }
    if (add <= 0) {
        p.skip = "더할 것이 없다";
        return p;
    }
    if (add > kBondAddMax) add = kBondAddMax;
    p.have = have + add;
    p.total = also_total ? total + add : total;
    // u16 칸이라 구조적 한계는 65535 지만, 근거 없이 그 근처까지 가지 않는다.
    if (p.have > kBondCeiling || p.total > kBondCeiling) {
        p.skip = "천장에 닿았다";
        return p;
    }
    // 총합을 안 올리면 보유가 총합을 넘을 수 있다 - 그 모양은 만들지 않는다.
    if (p.have > p.total) {
        p.skip = "총합도 같이 올려야 한다";
        return p;
    }
    p.apply = true;
    return p;
}

bool discover_knowledge(const mem::Rtti& rtti, const mem::Reader& reader,
                        int spin, std::uintptr_t player_actor) {
    const bool have_server = g_know.load(std::memory_order_acquire) != 0;
    const bool have_client = g_know_client.load(std::memory_order_acquire) != 0;
    if (have_server && have_client) return true;

    // **못 찾은 동안이 비싼 쪽이다.** find_objects_of 는 프리페치 밖이라 부를 때마다
    // 힙 영역을 전부 읽는다. 분석 루프 한 바퀴가 2초이므로 그대로 두면 2초마다
    // 11GB 다 - 새 캐릭터나 월드 밖이면 영구히.
    const int n = g_tries.load(std::memory_order_acquire);
    if (n >= kGiveUp) return have_server;
    if ((spin % kEverySpins) != 0) return have_server;

    // **플레이어 액터에서 먼저 내려가 본다.** 힙 스캔이 필요 없고 신원이 확실하다.
    // 이 사슬이 주는 것은 **서버** 컴포넌트다(게임 코드가 같은 자리에서 쓴다).
    if (!have_server) {
        const std::uintptr_t c = comp_from_player(reader, player_actor);
        if (c != 0) {
            std::uint64_t vt = 0;
            reader.read_value(c, &vt);
            g_know_vt[0].store(vt, std::memory_order_release);
            g_know.store(c, std::memory_order_release);
            log::infof("지식 컴포넌트 0x{:X} (플레이어 사슬) vtable 0x{:X}", c, vt);
            if (g_know_client.load(std::memory_order_acquire) != 0) return true;
        }
    }

    // **아직 못 찾은 클래스만** 넣는다. find_objects_of 의 상한은 클래스별이 아니라
    // 전체이고 낮은 주소부터 채운다(인벤토리에서 같은 함정을 겪었다).
    std::vector<std::string> want;
    if (!have_server) want.emplace_back(kKnowClass);
    if (!have_client) want.emplace_back(kKnowClassClient);
    constexpr std::size_t kFindMax = 64;
    const auto found = rtti.find_objects_of(want, kFindMax);
    if (found.size() >= kFindMax) {
        log::warnf("지식 컴포넌트 후보가 상한 {}에 닿았다 - 잘렸을 수 있다", kFindMax);
    }
    for (const auto& f : found) {
        const bool is_client = f.cls == kKnowClassClient;
        auto& slot = is_client ? g_know_client : g_know;
        if (slot.load(std::memory_order_acquire) != 0) continue;
        // **구조체까지 따라가 본다.** 모듈 이미지 안의 타입 등록 칸도 인스턴스로
        // 잡히므로(실측: 클래스마다 2개 중 하나가 그것이다), 포인터가 가리키는
        // 값까지 읽히는 것만 받는다.
        // **구조까지 본다.** 예전에는 `+0xC8` 을 따라가 총합이 0 이 아니면 받았는데,
        // 그것만으로는 vtable 값이 든 표를 걸러내지 못했다(머리 주석 참조).
        if (!comp_looks_real(reader, f.address)) continue;
        std::uint64_t ptr = 0;
        if (!reader.read_value(f.address + kBondPtr, &ptr) || ptr < 0x10000) {
            continue;
        }
        std::uint16_t total = 0;
        if (!reader.read_value(static_cast<std::uintptr_t>(ptr) + kBondTotal,
                               &total) ||
            total == 0) {
            continue;
        }
        std::uint64_t vt = 0;
        reader.read_value(f.address, &vt);
        g_know_vt[is_client ? 1 : 0].store(vt, std::memory_order_release);
        slot.store(f.address, std::memory_order_release);
        log::infof("지식 컴포넌트{} 0x{:X} -> 결속 0x{:X} (총합 {}) vtable 0x{:X}",
                   is_client ? "(클라)" : "", f.address, ptr, total, vt);
    }
    const bool ok = g_know.load(std::memory_order_acquire) != 0 &&
                    g_know_client.load(std::memory_order_acquire) != 0;
    if (!ok) {
        const int t = g_tries.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (t == kGiveUp) {
            log::warnf("지식 컴포넌트를 {}번 만에 다 못 찾았다 - 그만 찾는다"
                       " (스킬 포인트 기능이 꺼진다)", t);
        }
    }
    return g_know.load(std::memory_order_acquire) != 0;
}

bool knowledge_ready() {
    // **두 realm 을 다 봐야 한다.** 서버만 보고 참을 내면 화면이 "준비됐다" 고
    // 하면서 클라에는 영영 안 쓴다.
    return g_know.load(std::memory_order_acquire) != 0 &&
           g_know_client.load(std::memory_order_acquire) != 0;
}

void knowledge_check_alive(const mem::Reader& reader) {
    const std::uintptr_t comps[2] = {
        g_know.load(std::memory_order_acquire),
        g_know_client.load(std::memory_order_acquire)};
    const auto now = std::chrono::steady_clock::now();
    for (int realm = 0; realm < 2; ++realm) {
        if (comps[realm] == 0) {
            g_dead_since[realm] = {};   // 아직 못 찾았다 - 탐색이 할 일이다
            continue;
        }
        // **vtable 이 그대로인지 본다.** 예전에는 `+0xC8` 을 따라가 총합이 읽히는지만
        // 봤는데, 그것으로는 **해제된 뒤 다른 용도로 재사용된 메모리를 못 걸렀다** -
        // 실측 2026-09-14: 클라 컴포넌트의 소유 액터 칸에 0x726F6C6F432E7475
        // (= 바이트로 "ut.Color", 문자열 조각)이 들어 있는데도 살아 있다고 봤다.
        // 우연히 널 아닌 값을 읽는 것과 "그 클래스의 객체다" 는 다르다.
        std::uint64_t vt = 0;
        const std::uint64_t want_vt = g_know_vt[realm].load(std::memory_order_acquire);
        const bool vt_ok = reader.read_value(comps[realm], &vt) && vt != 0 &&
                           (want_vt == 0 || vt == want_vt);
        std::uint64_t ptr = 0;
        std::uint16_t total = 0;
        const bool alive =
            vt_ok && comp_looks_real(reader, comps[realm]) &&
            reader.read_value(comps[realm] + kBondPtr, &ptr) && ptr >= 0x10000 &&
            reader.read_value(static_cast<std::uintptr_t>(ptr) + kBondTotal,
                              &total);
        if (alive) {
            g_dead_since[realm] = {};
            continue;
        }
        // 로딩 화면에서는 잠깐 안 읽힐 수 있다. 바퀴 수가 아니라 경과 시간으로 잰다.
        if (g_dead_since[realm].time_since_epoch().count() == 0) {
            g_dead_since[realm] = now;
            continue;
        }
        if (now - g_dead_since[realm] < kDeadFor) continue;
        g_dead_since[realm] = {};
        log::warnf("지식 컴포넌트{} 0x{:X} 가 죽었다 - 다시 찾는다",
                   realm == 1 ? "(클라)" : "", comps[realm]);
        if (realm == 1) {
            g_know_client.store(0, std::memory_order_release);
        } else {
            g_know.store(0, std::memory_order_release);
        }
        g_know_vt[realm].store(0, std::memory_order_release);
        g_tries.store(0, std::memory_order_release);   // 다시 찾을 기회를 준다
    }
}

const char* bond_restore_blocked(const BondBackup& s, int now_have,
                                 int now_total) {
    if (now_have < 0 || now_total < 0) return "값을 읽을 수 없습니다";
    if (s.wrote_have == 0 && s.wrote_total == 0) {
        return "무엇을 써 놓았는지 모릅니다";
    }
    // **우리가 써 놓은 값 그대로인가.** 다르면 그 사이 게임이 결속을 주었거나
    // 사용자가 썼다는 뜻이다. 그 위에 옛 원본을 쓰면 정당하게 얻은 것을 지운다
    // (가방에서 같은 관문이 없어 산 칸을 지우는 길이 열렸었다).
    if (now_have != static_cast<int>(s.wrote_have) ||
        now_total != static_cast<int>(s.wrote_total)) {
        return "더한 뒤 값이 바뀌었습니다 - 되돌리지 않습니다";
    }
    // 복원이 **증가**가 되는 상황은 우리가 만든 것이 아니다.
    if (now_total < static_cast<int>(s.total)) {
        return "지금 총합이 원본보다 작습니다";
    }
    return nullptr;
}

std::uintptr_t knowledge_component() {
    return g_know.load(std::memory_order_acquire);
}

std::uintptr_t knowledge_component_client() {
    return g_know_client.load(std::memory_order_acquire);
}

void forget_knowledge() {
    g_know.store(0, std::memory_order_release);
    g_know_client.store(0, std::memory_order_release);
    // 백업은 **버리지 않는다.** bond_restore 가 주소를 다시 읽어 쓰므로, 여기서
    // 선제적으로 버리면 곧 다시 잡히는 흔한 경우에 되돌릴 수단만 잃는다.
}

BondState bond_read(const mem::Reader& reader, int realm) {
    BondState s;
    const std::uintptr_t comp = comp_of(realm);
    if (comp == 0) return s;
    // **포인터를 매번 다시 읽는다.** 컴포넌트가 살아 있어도 구조체가 옮겨 갈 수
    // 있고, 낡은 주소에 쓰는 것은 이 저장소가 반복해서 데인 함정이다
    // (clan-roster-volatile-writes).
    std::uint64_t ptr = 0;
    if (!reader.read_value(comp + kBondPtr, &ptr) || ptr == 0) return s;
    const std::uintptr_t a = static_cast<std::uintptr_t>(ptr);
    std::uint16_t have = 0, total = 0, total2 = 0, other = 0, total3 = 0;
    if (!reader.read_value(a + kBondHave, &have) ||
        !reader.read_value(a + kBondTotal, &total) ||
        !reader.read_value(a + kBondTotal2, &total2) ||
        !reader.read_value(a + kBondOther, &other) ||
        !reader.read_value(a + kBondTotal3, &total3)) {
        return s;
    }
    s.address = a;
    s.have = have;
    s.total = total;
    s.total2 = total2;
    s.other = other;
    s.total3 = total3;
    return s;
}

namespace {

// **잠금을 이미 쥔 채** 부른다.
void add_to(const mem::Reader& reader, int realm, int add, bool also_total,
            BondResult* r) {
    const BondState s = bond_read(reader, realm);
    if (s.address == 0) return;   // 그 realm 은 아직 못 잡았다
    const BondPlan p = plan_bond_add(s.have, s.total, add, also_total);
    if (!p.apply) {
        ++r->skip;
        r->last_skip = p.skip;
        log::infof("결속 건너뜀: realm {} 0x{:X} - {} (보유 {} 총합 {})", realm,
                   s.address, p.skip, s.have, s.total);
        return;
    }
    {
        // **realm 당 처음 본 값만** 남긴다. 여러 번 눌러도 처음 값이 지켜진다.
        std::lock_guard<std::mutex> lk(g_mtx);
        bool seen = false;
        for (const auto& x : g_backup) {
            if (x.realm == realm) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            // 주소는 안 적는다 - 쓸 때마다 +0xC8 을 다시 읽으므로 적어 둘 이유가
            // 없고, 적어 두면 낡은 주소를 쓰게 되는 길만 생긴다.
            g_backup.push_back(BondBackup{
                realm, static_cast<std::uint16_t>(s.have),
                static_cast<std::uint16_t>(s.total),
                static_cast<std::uint16_t>(s.total2),
                static_cast<std::uint16_t>(s.total3), 0, 0});
        }
    }
    // **총합 사본 셋을 다 쓴다.** 화면이 어느 것을 읽는지 아직 모른다 - 하나만
    // 올리면 화면과 저장이 어긋난 채 남을 수 있다. 셋이 지금 다 같은 값이라
    // (실측 151/151/151) 같이 올리는 것이 그 모양을 지키는 길이다.
    log_write("결속 보유", s.address + kBondHave, std::to_string(s.have),
              std::to_string(p.have));
    bool ok = wr16(s.address + kBondHave, static_cast<std::uint16_t>(p.have));
    if (also_total) {
        log_write("결속 총합", s.address + kBondTotal, std::to_string(s.total),
                  std::to_string(p.total));
        ok = wr16(s.address + kBondTotal,
                  static_cast<std::uint16_t>(p.total)) && ok;
        // 사본 둘은 지금 값이 총합과 같을 때만 따라 올린다 - 다른 값이면 우리가
        // 뜻을 모르는 칸이므로 건드리지 않는다.
        if (s.total2 == s.total) {
            ok = wr16(s.address + kBondTotal2,
                      static_cast<std::uint16_t>(p.total)) && ok;
        }
        if (s.total3 == s.total) {
            ok = wr16(s.address + kBondTotal3,
                      static_cast<std::uint16_t>(p.total)) && ok;
        }
    }
    const BondState back = bond_read(reader, realm);
    if (ok && back.address == s.address && back.have == p.have &&
        back.total == p.total) {
        ++r->changed;
        {
            // **우리가 써 놓은 값을 적어 둔다.** 되돌리기가 이것과 지금 값을
            // 대조해, 그 사이 게임이 결속을 주었으면 쓰지 않는다.
            std::lock_guard<std::mutex> lk(g_mtx);
            for (auto& x : g_backup) {
                if (x.realm != realm) continue;
                x.wrote_have = static_cast<std::uint16_t>(p.have);
                x.wrote_total = static_cast<std::uint16_t>(p.total);
                break;
            }
        }
        log::infof("결속: realm {} 보유 {} -> {}, 총합 {} -> {} (사본 {} {})",
                   realm, s.have, back.have, s.total, back.total, back.total2,
                   back.total3);
    } else {
        // 반쯤 써진 채로 두지 않는다. **지금 주소**에 쓰고(옮겨 갔다고 판정한
        // 바로 그 경우에 옛 주소를 쓰면 남의 자리를 때린다), **사본까지** 되돌린다
        // (안 그러면 총합과 사본이 영구히 어긋난 채 남는다).
        const std::uintptr_t at = back.address != 0 ? back.address : s.address;
        wr16(at + kBondHave, static_cast<std::uint16_t>(s.have));
        wr16(at + kBondTotal, static_cast<std::uint16_t>(s.total));
        wr16(at + kBondTotal2, static_cast<std::uint16_t>(s.total2));
        wr16(at + kBondTotal3, static_cast<std::uint16_t>(s.total3));
        ++r->fail;
        log::warnf("결속 쓰기 실패(되돌림): realm {} 0x{:X}", realm, at);
    }
}

}  // namespace

BondResult bond_add(const mem::Reader& reader, int add, bool also_total) {
    std::lock_guard<std::mutex> op(g_op_mtx);
    BondResult r;
    add_to(reader, 0, add, also_total, &r);
    add_to(reader, 1, add, also_total, &r);
    log::infof("결속 더하기: +{} (총합도 {}) -> 바꾼 것 {}개, 건너뜀 {}, 실패 {}",
               add, also_total ? "함께" : "안 함", r.changed, r.skip, r.fail);
    return r;
}

bool bond_has_backup() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return !g_backup.empty();
}

BondResult bond_restore(const mem::Reader& reader) {
    std::lock_guard<std::mutex> op(g_op_mtx);
    BondResult r;
    std::vector<BondBackup> saved;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        saved = g_backup;
    }
    std::vector<BondBackup> keep;
    for (const auto& s : saved) {
        // **주소를 다시 읽는다.** 기록해 둔 주소가 아니라 지금 컴포넌트가 가리키는
        // 자리에 쓴다 - 낡은 주소는 남의 객체가 된다.
        const BondState now = bond_read(reader, s.realm);
        if (now.address == 0) {
            ++r.skip;
            r.last_skip = "지금은 지식 컴포넌트를 읽을 수 없습니다";
            keep.push_back(s);   // 버리지 않는다
            continue;
        }
        if (now.have == s.have && now.total == s.total) {
            ++r.skip;
            r.last_skip = "이미 원래 값입니다";
            continue;   // 되돌릴 것이 없다 - 기록을 지운다
        }
        // **우리가 써 놓은 값 그대로인가.** 아니면 그 사이 게임이 결속을 주었거나
        // 사용자가 썼다는 뜻이라, 옛 원본을 쓰면 정당하게 얻은 것을 지운다.
        if (const char* why = bond_restore_blocked(s, now.have, now.total)) {
            ++r.skip;
            r.last_skip = why;
            keep.push_back(s);
            log::warnf("결속 복원 건너뜀: realm {} - {} (지금 {}/{}, 우리가 쓴 {}/{})",
                       s.realm, why, now.have, now.total, s.wrote_have,
                       s.wrote_total);
            continue;
        }
        log_write("결속 복원", now.address + kBondHave, std::to_string(now.have),
                  std::to_string(s.have));
        bool ok = wr16(now.address + kBondHave, s.have);
        ok = wr16(now.address + kBondTotal, s.total) && ok;
        if (now.total2 == now.total) {
            ok = wr16(now.address + kBondTotal2, s.total2) && ok;
        }
        if (now.total3 == now.total) {
            ok = wr16(now.address + kBondTotal3, s.total3) && ok;
        }
        const BondState back = bond_read(reader, s.realm);
        if (ok && back.have == s.have && back.total == s.total) {
            ++r.changed;
        } else {
            ++r.fail;
            keep.push_back(s);
            log::warnf("결속 복원 실패: realm {} 0x{:X}", s.realm, now.address);
        }
    }
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_backup = keep;
    }
    log::infof("결속 복원: 되돌린 것 {}개, 건너뜀 {}, 실패 {}", r.changed, r.skip,
               r.fail);
    return r;
}

}  // namespace cdtb::game

#include "game/skillpoint.h"

#include <windows.h>

#include <atomic>
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

// 이번 실행에서 **처음 본** 값. 되돌리기가 진짜 복원이 되게 한다.
struct BondBackup {
    int realm = 0;
    std::uintptr_t address = 0;
    std::uint16_t have = 0, total = 0, total2 = 0, total3 = 0;
};
std::mutex g_mtx;
std::vector<BondBackup> g_backup;

// 인프로세스 직접 쓰기(주입 DLL 전용). SEH 로 감싼다.
bool wr16(std::uintptr_t a, std::uint16_t v) {
    __try {
        *reinterpret_cast<volatile std::uint16_t*>(a) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
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

bool discover_knowledge(const mem::Rtti& rtti, const mem::Reader& reader) {
    const bool have_server = g_know.load(std::memory_order_acquire) != 0;
    const bool have_client = g_know_client.load(std::memory_order_acquire) != 0;
    if (have_server && have_client) return true;

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
        std::uint64_t ptr = 0;
        if (!reader.read_value(f.address + kBondPtr, &ptr) || ptr == 0) continue;
        std::uint16_t total = 0;
        if (!reader.read_value(static_cast<std::uintptr_t>(ptr) + kBondTotal,
                               &total) ||
            total == 0) {
            continue;
        }
        slot.store(f.address, std::memory_order_release);
        log::infof("지식 컴포넌트{} 0x{:X} -> 결속 0x{:X} (총합 {})",
                   is_client ? "(클라)" : "", f.address, ptr, total);
    }
    return g_know.load(std::memory_order_acquire) != 0;
}

bool knowledge_ready() {
    return g_know.load(std::memory_order_acquire) != 0;
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
        for (auto& x : g_backup) {
            if (x.realm == realm) {
                x.address = s.address;   // 주소는 따라간다, 값은 덮지 않는다
                seen = true;
                break;
            }
        }
        if (!seen) {
            g_backup.push_back(BondBackup{
                realm, s.address, static_cast<std::uint16_t>(s.have),
                static_cast<std::uint16_t>(s.total),
                static_cast<std::uint16_t>(s.total2),
                static_cast<std::uint16_t>(s.total3)});
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
        log::infof("결속: realm {} 보유 {} -> {}, 총합 {} -> {} (사본 {} {})",
                   realm, s.have, back.have, s.total, back.total, back.total2,
                   back.total3);
    } else {
        // 반쯤 써진 채로 두지 않는다.
        wr16(s.address + kBondHave, static_cast<std::uint16_t>(s.have));
        wr16(s.address + kBondTotal, static_cast<std::uint16_t>(s.total));
        ++r->fail;
        log::warnf("결속 쓰기 실패(되돌림): realm {} 0x{:X}", realm, s.address);
    }
}

}  // namespace

BondResult bond_add(const mem::Reader& reader, int add, bool also_total) {
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

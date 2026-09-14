#include "game/knowledge.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <atomic>
#include <mutex>

#include "core/log.h"
#include "core/write_log.h"
#include "game/localization.h"
#include "game/skillpoint.h"

namespace cdtb::game {
namespace {

// 쓰기를 직렬화한다. 화면(렌더 스레드)이 부르고, 분석 스레드가 생존 검사로
// 컴포넌트를 버릴 수 있다 - 읽고 쓰는 사이에 그 일이 나면 낡은 주소에 쓴다.
std::mutex g_op_mtx;

std::uintptr_t comp_of(int realm) {
    return realm == 1 ? knowledge_component_client() : knowledge_component();
}

std::uint64_t rd64(const mem::Reader& r, std::uintptr_t a) {
    std::uint64_t v = 0;
    return r.read_value(a, &v) ? v : 0;
}
std::int32_t rd32(const mem::Reader& r, std::uintptr_t a) {
    std::int32_t v = 0;
    return r.read_value(a, &v) ? v : 0;
}

// 인프로세스 직접 쓰기(주입 DLL 전용). SEH 로 감싼다.
bool wr32(std::uintptr_t a, std::int32_t v) {
    __try {
        *reinterpret_cast<volatile std::int32_t*>(a) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool wr64(std::uintptr_t a, std::uint64_t v) {
    __try {
        *reinterpret_cast<volatile std::uint64_t*>(a) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool wr8(std::uintptr_t a, std::uint8_t v) {
    __try {
        *reinterpret_cast<volatile std::uint8_t*>(a) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// 게임의 스킬 등록 함수. 인자 4개, 스택 인자 없음(프롤로그 실측).
using RegisterFn = void*(__fastcall*)(void*, std::uint16_t, std::int32_t,
                                      std::uint8_t);

// **게임 코드를 부르는 유일한 자리.** SEH 로 감싼다 - 접근 위반은 잡을 수 있다
// (힙 손상 같은 것은 못 잡으니, 부르기 전의 관문이 진짜 방어선이다).
bool call_register(RegisterFn f, void* comp, std::uint16_t key,
                   std::int32_t level, std::uint8_t silent) {
    __try {
        f(comp, key, level, silent);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// 정상적으로 배운 지식의 습득 시각 하나. `+0x08` 이 0 이면 "이 지식 없음" 으로 보는
// 소비자가 있어서(RVA 0x028348EA), 우리가 올린 레코드도 같이 채운다. 임의의 큰 수
// 대신 **실제 값을 복사**한다 - 그 칸을 "습득 후 경과" 로 쓰는 로직이 있다.
std::atomic<std::uint64_t> g_time_donor{0};

// 등록 호출이 한 번이라도 예외로 끝나면 잠근다(실행 단위). 되돌릴 길이 없으므로
// 다시 시도하게 두지 않는다.
std::atomic<bool> g_register_locked{false};

std::uint64_t time_donor(const mem::Reader& r, const KnowTable& t) {
    const std::uint64_t cached = g_time_donor.load(std::memory_order_acquire);
    if (cached != 0) return cached;
    for (int i = 0; i < t.count; ++i) {
        const std::uintptr_t rec = know_record(t.data, i);
        if (rec == 0) continue;
        std::int32_t lv = 0;
        if (!r.read_value(rec + kKnowRecLevel, &lv) || lv < 1) continue;
        std::uint64_t v = 0;
        if (!r.read_value(rec + kKnowRecObj, &v) || v == 0) continue;
        g_time_donor.store(v, std::memory_order_release);
        return v;
    }
    return 0;
}

}  // namespace

// ------------------------------------------------------------ 순수 부분

std::uintptr_t know_record(std::uintptr_t data, int n) {
    if (data == 0 || n < 0) return 0;
    return data + static_cast<std::uintptr_t>(n) * kKnowRecStride;
}

bool know_mgr_sane(int count, std::uintptr_t array) {
    if (count <= 0 || count > kKnowMaxCount) return false;
    if (array == 0) return false;
    // 사용자 주소공간의 아래쪽 64KB 는 절대 유효하지 않다.
    if (array < 0x10000) return false;
    return true;
}

void know_need_add(std::vector<KnowNeed>* v, int number, int need, bool any_of) {
    if (v == nullptr || number < 0) return;
    if (need < 1) need = 1;
    for (auto& e : *v) {
        if (e.number != number) continue;
        // 여러 곳이 요구하면 **가장 높은 요구 레벨**을 남긴다 - 그만큼 배워야
        // 전부 풀린다.
        if (need > e.need_level) e.need_level = need;
        if (any_of) e.any_of = true;
        ++e.wanted_by;
        return;
    }
    KnowNeed e;
    e.number = number;
    e.need_level = need;
    e.any_of = any_of;
    e.wanted_by = 1;
    v->push_back(std::move(e));
}

void know_need_sort(std::vector<KnowNeed>* v) {
    if (v == nullptr) return;
    std::sort(v->begin(), v->end(), [](const KnowNeed& a, const KnowNeed& b) {
        if (a.wanted_by != b.wanted_by) return a.wanted_by > b.wanted_by;
        return a.number < b.number;   // 완전 순서 - 화면이 흔들리지 않게
    });
}

void know_need_drop_satisfied(std::vector<KnowNeed>* v) {
    if (v == nullptr) return;
    v->erase(std::remove_if(v->begin(), v->end(),
                            [](const KnowNeed& e) {
                                return e.have_level >= e.need_level;
                            }),
             v->end());
}

// ------------------------------------------------------------------ 읽기

bool know_manager(const mem::Reader& reader, KnowMgr* out) {
    if (out == nullptr) return false;
    const std::uintptr_t base = reader.module_base();
    if (base == 0) return false;
    if (kKnowMgrGlobalRva + 8 > reader.module_size()) return false;
    const std::uintptr_t obj = rd64(reader, base + kKnowMgrGlobalRva);
    if (obj == 0) return false;
    KnowMgr m;
    m.object = obj;
    m.count = rd32(reader, obj + kKnowMgrCount);
    m.array = static_cast<std::uintptr_t>(rd64(reader, obj + kKnowMgrArray));
    if (!know_mgr_sane(m.count, m.array)) return false;
    *out = m;
    return true;
}

bool know_table(const mem::Reader& reader, int realm, KnowTable* out) {
    if (out == nullptr) return false;
    const std::uintptr_t comp = comp_of(realm);
    if (comp == 0) return false;
    KnowTable t;
    t.comp = comp;
    t.data = static_cast<std::uintptr_t>(rd64(reader, comp + kKnowCompData));
    t.count = rd32(reader, comp + kKnowCompCount);
    t.cap = rd32(reader, comp + kKnowCompCap);
    if (t.data == 0 || t.count <= 0 || t.count > kKnowMaxCount) return false;
    if (t.cap < t.count) return false;
    *out = t;
    return true;
}

int know_level(const mem::Reader& reader, const KnowTable& t, int n) {
    if (n < 0 || n >= t.count) return -1;
    const std::uintptr_t rec = know_record(t.data, n);
    if (rec == 0) return -1;
    return rd32(reader, rec + kKnowRecLevel);
}

KnowScan know_scan(const mem::Rtti* rtti, const mem::Reader& reader, int realm) {
    KnowScan s;
    s.realm = realm;

    KnowTable t;
    if (!know_table(reader, realm, &t)) {
        s.skip = "지식 컴포넌트를 아직 못 잡았습니다";
        return s;
    }
    KnowMgr m;
    if (!know_manager(reader, &m)) {
        s.skip = "지식 정보 매니저를 못 찾았습니다(게임이 갱신됐을 수 있습니다)";
        return s;
    }
    // **교차 검증.** Initialize 가 매니저의 개수로 레코드 표를 잡으므로 두 값은
    // 같아야 한다. 다르면 우리가 엉뚱한 전역을 읽은 것이다 - 쓰레기를 따라가느니
    // 여기서 멈춘다.
    if (m.count != t.count) {
        log::warnf("지식: 매니저 개수 {} 와 표 개수 {} 가 다르다 - 멈춘다", m.count,
                   t.count);
        s.skip = "매니저와 컴포넌트의 지식 개수가 어긋납니다";
        return s;
    }
    s.knowledge = m.count;

    LocSystem loc;
    const bool has_loc =
        rtti != nullptr && find_loc_system(*rtti, reader, &loc);

    // 1) 내 레벨을 통째로 읽는다. 레코드 24바이트짜리 배열이라 한 번에 뜬다.
    std::vector<std::uint8_t> recs(static_cast<std::size_t>(t.count) *
                                   kKnowRecStride);
    if (!reader.read(t.data, recs.data(), recs.size())) {
        s.skip = "지식 표를 못 읽었습니다";
        return s;
    }
    std::vector<int> level(static_cast<std::size_t>(t.count), 0);
    for (int i = 0; i < t.count; ++i) {
        std::int32_t v = 0;
        std::memcpy(&v, recs.data() + static_cast<std::size_t>(i) * kKnowRecStride +
                             kKnowRecLevel,
                    4);
        level[static_cast<std::size_t>(i)] = v;
        if (v >= 1) ++s.learned;
    }

    // 2) 정보 포인터 배열도 한 번에.
    std::vector<std::uint64_t> infos(static_cast<std::size_t>(m.count), 0);
    if (!reader.read(m.array, infos.data(), infos.size() * 8)) {
        s.skip = "지식 정보 배열을 못 읽었습니다";
        return s;
    }

    // 3) 선행 조건을 모은다. 태그 3(스킬 트리 경로)만 본다.
    for (int n = 0; n < m.count; ++n) {
        const std::uintptr_t info =
            static_cast<std::uintptr_t>(infos[static_cast<std::size_t>(n)]);
        if (info == 0) continue;
        ++s.walked;
        const std::uintptr_t lv_base =
            static_cast<std::uintptr_t>(rd64(reader, info + kInfoLevels));
        const int lv_count = rd32(reader, info + kInfoLevelCount);
        if (lv_base == 0 || lv_count <= 0 || lv_count > kKnowMaxLevels) continue;

        for (int lv = 0; lv < lv_count; ++lv) {
            const std::uintptr_t ld =
                lv_base + static_cast<std::uintptr_t>(lv) * kLevelStride;
            const std::uintptr_t lf_base =
                static_cast<std::uintptr_t>(rd64(reader, ld + kLevelLearnFrom));
            const int lf_count = rd32(reader, ld + kLevelLearnFromCount);
            if (lf_base == 0 || lf_count <= 0 || lf_count > kKnowMaxLearnFrom) {
                continue;
            }
            for (int j = 0; j < lf_count; ++j) {
                const std::uintptr_t lf =
                    lf_base + static_cast<std::uintptr_t>(j) * kLearnFromStride;
                std::uint8_t tag = 0, mode = 0;
                if (!reader.read_value(lf + kLearnFromTag, &tag)) continue;
                if (tag != kTagSkillTree) continue;
                reader.read_value(lf + kLearnFromMode, &mode);
                const std::uintptr_t nb = static_cast<std::uintptr_t>(
                    rd64(reader, lf + kLearnFromNeed));
                const int nc = rd32(reader, lf + kLearnFromNeedCount);
                if (nb == 0 || nc <= 0 || nc > kKnowMaxNeeds) continue;
                std::vector<std::uint8_t> need(static_cast<std::size_t>(nc) *
                                               kNeedStride);
                if (!reader.read(nb, need.data(), need.size())) continue;
                for (int k = 0; k < nc; ++k) {
                    std::uint16_t num = 0;
                    std::int32_t req = 0;
                    const std::size_t at =
                        static_cast<std::size_t>(k) * kNeedStride;
                    std::memcpy(&num, need.data() + at, 2);
                    std::memcpy(&req, need.data() + at + 4, 4);
                    if (num >= m.count) continue;   // 표 밖은 버린다
                    know_need_add(&s.needs, num, req, mode == kModeAnyOf);
                }
            }
        }
    }

    // 4) 내 레벨을 붙이고 충분한 것을 뺀다.
    for (auto& e : s.needs) {
        e.have_level = level[static_cast<std::size_t>(e.number)];
    }
    know_need_drop_satisfied(&s.needs);
    know_need_sort(&s.needs);

    // 5) 이름. 못 풀려도 번호는 쓸 수 있으므로 실패를 실패로 치지 않는다.
    if (has_loc) {
        for (auto& e : s.needs) {
            const std::uintptr_t info = static_cast<std::uintptr_t>(
                infos[static_cast<std::size_t>(e.number)]);
            if (info == 0) continue;
            const std::uintptr_t lv_base =
                static_cast<std::uintptr_t>(rd64(reader, info + kInfoLevels));
            if (lv_base == 0) continue;
            const std::uint64_t key = rd64(reader, lv_base + kLevelName);
            if (key == 0) continue;
            resolve(reader, loc, key, &e.name, nullptr);
        }
    }

    s.ok = true;
    return s;
}

// ------------------------------------------------------------------ 쓰기

KnowWrite know_learn(const mem::Reader& reader, int number, int level) {
    KnowWrite w;
    if (number < 0 || level < 1) {
        w.skip = 2;
        w.last_skip = "번호나 레벨이 말이 안 됩니다";
        return w;
    }
    std::lock_guard<std::mutex> lk(g_op_mtx);
    for (int realm = 0; realm < 2; ++realm) {
        KnowTable t;
        // **쓸 때마다 표를 다시 읽는다.** 리로드로 컴포넌트가 새로 만들어지면
        // 예전 주소는 남의 메모리다(명부에서 같은 실수로 게임을 튕겼다).
        if (!know_table(reader, realm, &t)) {
            ++w.skip;
            w.last_skip = "그쪽 컴포넌트를 아직 못 잡았습니다";
            continue;
        }
        if (number >= t.count) {
            ++w.skip;
            w.last_skip = "그 번호는 이 표의 범위 밖입니다";
            continue;
        }
        const std::uintptr_t rec = know_record(t.data, number);
        const int now = rd32(reader, rec + kKnowRecLevel);
        if (now >= level) {
            ++w.skip;
            w.last_skip = "이미 그 레벨 이상입니다";
            continue;
        }
        log_write("지식 습득", rec, "레벨 " + std::to_string(now),
                  "레벨 " + std::to_string(level));
        if (!wr32(rec + kKnowRecLevel, level)) {
            ++w.fail;
            continue;
        }
        // 게임도 같이 세우는 플래그.
        wr8(rec + kKnowRecFlag, 1);
        // **습득 시각도 채운다.** 0 이면 "이 지식 없음" 으로 보는 소비자가 있다
        // (RVA 0x028348EA). 객체 포인터가 아니라 u64 시각이다 - 0x0201E3F0 은
        // 할당자가 아니라 {일,시,분,초,ms} -> 밀리초 변환 함수였다(전문 확인).
        std::uint64_t when = 0;
        if (reader.read_value(rec + kKnowRecObj, &when) && when == 0) {
            const std::uint64_t donor = time_donor(reader, t);
            if (donor != 0) wr64(rec + kKnowRecObj, donor);
        }
        if (rd32(reader, rec + kKnowRecLevel) != level) {
            ++w.fail;
            continue;
        }
        ++w.changed;
    }
    if (w.changed > 0) {
        log::infof("지식 {} 번을 레벨 {} 로 - realm {}개", number, level,
                   w.changed);
    }
    return w;
}

// ------------------------------------------ 스킬 등록 (게임 함수 호출)

// 본체. **g_op_mtx 를 이미 쥔 채로** 부른다 - 게임 스레드가 그 락을 기다리며
// 멈추지 않게 하려고 잠그기와 일하기를 갈라 두었다.
namespace {
KnowRegister register_held(const mem::Reader& reader, int number, int level);
}  // namespace

KnowRegister know_register_skill(const mem::Reader& reader, int number,
                                 int level) {
    std::lock_guard<std::mutex> lk(g_op_mtx);
    return register_held(reader, number, level);
}

namespace {
KnowRegister register_held(const mem::Reader& reader, int number,
                           int level) {
    KnowRegister r;
    if (number < 0 || number > 0xFFFF || level < 1) {
        r.skip = "번호나 레벨이 말이 안 됩니다";
        return r;
    }
    const std::uintptr_t base = reader.module_base();
    if (base == 0 || kKnowRegisterRva + 16 > reader.module_size() ||
        kServerCompVtableRva + 8 > reader.module_size()) {
        r.skip = "이미지 범위 밖입니다";
        return r;
    }

    if (g_register_locked.load(std::memory_order_acquire)) {
        r.skip = "이번 실행에서 호출이 한 번 실패해 잠갔습니다 - 게임을 다시 켜십시오";
        return r;
    }

    // **서버 컴포넌트에만.** 맵은 Server 고유 필드다 - 클라에 같은 자리가 있는지
    // 모르므로, 잘못 부르면 엉뚱한 필드를 해시맵으로 취급해 그 자리에서 죽는다.
    const std::uintptr_t comp = knowledge_component();
    if (comp == 0) {
        r.skip = "서버 지식 컴포넌트를 아직 못 잡았습니다";
        return r;
    }
    const std::uintptr_t vt = static_cast<std::uintptr_t>(rd64(reader, comp));
    if (vt != base + kServerCompVtableRva) {
        log::warnf("지식 등록: vtable 0x{:X} 가 서버 것(0x{:X})이 아니다 - 안 부른다",
                   vt, base + kServerCompVtableRva);
        r.skip = "서버 컴포넌트가 아닙니다(게임이 갱신됐을 수 있습니다)";
        return r;
    }

    // **소유 액터를 확인한다.** 등록 함수가 초입에서 `[comp+8]` 을 역참조하므로
    // (0x02AA5421 -> 0x02AA5433), 0 이거나 못 읽으면 그 자리에서 죽는다.
    std::uint64_t owner = 0;
    if (!reader.read_value(comp + kKnowCompOwner, &owner) || owner == 0) {
        log::warnf("지식 등록: 소유 액터가 0 이다(comp 0x{:X}) - 안 부른다", comp);
        r.skip = "그 컴포넌트에 소유 액터가 없습니다";
        return r;
    }
    // 그 액터의 `+0x08` 도 읽힌다는 것까지 본다 - 함수가 바로 다음에 그것을 읽는다.
    std::uint64_t owner_field = 0;
    if (!reader.read_value(static_cast<std::uintptr_t>(owner) + 8, &owner_field)) {
        log::warnf("지식 등록: 소유 액터 0x{:X} 의 +8 을 못 읽는다 - 안 부른다", owner);
        r.skip = "소유 액터를 읽을 수 없습니다";
        return r;
    }

    // **참조 카운트 객체까지 확인한다.** 등록 함수가 초입에 부르는 0x0038D7F0 이
    // `[[comp+8]+8]` 의 카운트를 올리고 `call [*(obj)+0x30]` 으로 가상 호출을 한다.
    // 그 vtable 이 이미지 밖이면 그 자리에서 죽는다.
    {
        const std::uintptr_t obj = static_cast<std::uintptr_t>(owner_field);
        std::uint64_t objvt = 0;
        const std::uint64_t limit =
            base + static_cast<std::uint64_t>(reader.module_size());
        if (obj == 0 || !reader.read_value(obj, &objvt) || objvt < base ||
            objvt >= limit) {
            log::warnf("지식 등록: [소유액터+8]=0x{:X} 의 vtable 0x{:X} 이 이미지 밖이다"
                       " - 안 부른다", obj, objvt);
            r.skip = "소유 객체가 온전하지 않습니다";
            return r;
        }
        std::uint64_t fn = 0;
        if (!reader.read_value(static_cast<std::uintptr_t>(objvt) + 0x30, &fn) ||
            fn < base || fn >= limit) {
            log::warnf("지식 등록: vtable+0x30 = 0x{:X} 이 이미지 밖이다 - 안 부른다",
                       fn);
            r.skip = "소유 객체의 가상 함수가 온전하지 않습니다";
            return r;
        }
    }

    // 표와 매니저가 서로 맞는지도 본다 - 어긋나면 우리가 잘못 보고 있는 것이다.
    KnowTable t;
    KnowMgr m;
    if (!know_table(reader, 0, &t) || !know_manager(reader, &m) ||
        m.count != t.count || number >= m.count) {
        r.skip = "지식 표가 말이 안 됩니다";
        return r;
    }

    // 붙을 스킬이 없는 지식이면 부를 이유가 없다.
    const std::uintptr_t info = static_cast<std::uintptr_t>(
        rd64(reader, m.array + static_cast<std::uintptr_t>(number) * 8));
    if (info == 0) {
        r.skip = "그 번호의 지식 정보가 없습니다";
        return r;
    }
    std::uint16_t apply = kNoApplySkill;
    reader.read_value(info + kInfoApplySkill, &apply);
    if (apply == kNoApplySkill) {
        r.skip = "이 지식에는 붙는 스킬이 없습니다";
        return r;
    }
    r.skill_key = apply;

    r.before = rd32(reader, comp + kKnowMapCount);
    log_write("지식 스킬 등록(게임 함수)", base + kKnowRegisterRva,
              "맵 원소 " + std::to_string(r.before), "호출");
    const auto f = reinterpret_cast<RegisterFn>(base + kKnowRegisterRva);
    // 조용히=1 로 부른다 - 부수 갱신 경로를 건너뛰어 더 안전하다.
    if (!call_register(f, reinterpret_cast<void*>(comp),
                       static_cast<std::uint16_t>(number), level, 1)) {
        r.skip = "게임 함수 호출이 예외로 끝났습니다 - 이번 실행에서는 잠급니다";
        g_register_locked.store(true, std::memory_order_release);
        log::errorf("지식 등록 {}번: 호출이 예외로 끝났다 - 이번 실행에서 잠근다."
                    " 저장하지 말고 이전 세이브를 부르십시오", number);
        return r;
    }
    r.after = rd32(reader, comp + kKnowMapCount);
    // 이미 들어 있던 지식이면 개수가 안 늘고 갱신만 된다 - 줄지만 않으면 성공이다.
    r.ok = r.after >= r.before;
    log::infof("지식 {}번 스킬 등록: 맵 원소 {} -> {} (스킬키 {})", number, r.before,
               r.after, static_cast<int>(apply));
    return r;
}
}  // namespace

// ------------------------------------------------------------ 자동 재적용

namespace {
std::mutex g_auto_mtx;
std::vector<KnowWant> g_auto;
}  // namespace

void know_auto_upsert(std::vector<KnowWant>* v, int number, int level) {
    if (v == nullptr || number < 0 || level < 1) return;
    for (auto& e : *v) {
        if (e.number != number) continue;
        // **낮추지 않는다.** 낮은 값으로 덮으면 재적용이 이미 올려 둔 것을 되돌리려
        // 들고, 그러면 know_learn 이 "이미 그 레벨 이상" 으로 건너뛰어 조용히 굳는다.
        if (level > e.level) e.level = level;
        return;
    }
    v->push_back(KnowWant{number, level});
}

void know_auto_remember(int number, int level) {
    std::lock_guard<std::mutex> lk(g_auto_mtx);
    know_auto_upsert(&g_auto, number, level);
}

void know_auto_forget() {
    std::lock_guard<std::mutex> lk(g_auto_mtx);
    if (g_auto.empty()) return;
    log::infof("지식 자동 재적용 해제: {}개를 잊는다",
               static_cast<int>(g_auto.size()));
    g_auto.clear();
}

std::vector<KnowWant> know_auto_list() {
    std::lock_guard<std::mutex> lk(g_auto_mtx);
    return g_auto;
}

void know_auto_tick(const mem::Reader& reader) {
    std::vector<KnowWant> want;
    {
        std::lock_guard<std::mutex> lk(g_auto_mtx);
        if (g_auto.empty()) return;   // 사용자가 건 것이 없으면 아무 일도 안 한다
        want = g_auto;
    }
    int again = 0;
    for (const KnowWant& w : want) {
        // 두 realm 중 **어느 쪽이든** 모자라면 다시 건다. 한쪽만 보면 다른 쪽이
        // 조용히 어긋난 채 남는다(결속에서 같은 자리를 놓쳤었다).
        bool low = false;
        for (int realm = 0; realm < 2 && !low; ++realm) {
            KnowTable t;
            if (!know_table(reader, realm, &t)) continue;
            const int now = know_level(reader, t, w.number);
            if (now >= 0 && now < w.level) low = true;
        }
        if (!low) continue;
        const KnowWrite r = know_learn(reader, w.number, w.level);
        if (r.changed > 0) ++again;
    }
    if (again > 0) {
        log::infof("지식 자동 재적용: {}개를 다시 걸었다", again);
    }
}

void know_diagnose(const mem::Reader& reader) {
    const std::uintptr_t base = reader.module_base();
    log::infof("지식 진단 ----- 모듈 0x{:X} 크기 0x{:X}", base,
               static_cast<std::uint64_t>(reader.module_size()));
    KnowMgr m;
    if (know_manager(reader, &m)) {
        log::infof("  매니저 0x{:X} 개수 {} 배열 0x{:X}", m.object, m.count,
                   m.array);
    } else {
        log::warnf("  매니저를 못 찾았다");
    }
    for (int realm = 0; realm < 2; ++realm) {
        const char* who = realm == 0 ? "서버" : "클라";
        const std::uintptr_t comp =
            realm == 1 ? knowledge_component_client() : knowledge_component();
        if (comp == 0) {
            log::warnf("  {} 컴포넌트: 아직 못 잡았다", who);
            continue;
        }
        const std::uintptr_t vt = static_cast<std::uintptr_t>(rd64(reader, comp));
        log::infof("  {} comp 0x{:X} vtable 0x{:X} (서버 기대 0x{:X}) 일치={}", who,
                   comp, vt, base + kServerCompVtableRva,
                   vt == base + kServerCompVtableRva ? 1 : 0);
        const std::uint64_t owner = rd64(reader, comp + kKnowCompOwner);
        std::uint64_t owner8 = 0;
        const bool owner8_ok =
            owner != 0 && reader.read_value(static_cast<std::uintptr_t>(owner) + 8,
                                            &owner8);
        log::infof("    +0x08 소유액터 0x{:X} (그 +8 읽힘={} 값 0x{:X})", owner,
                   owner8_ok ? 1 : 0, owner8);
        // **등록 함수가 여기서 가상 호출을 한다.** 0x0038D7F0 이
        // `[[comp+8]+8]` 을 참조 카운트 객체로 보고 `lock xadd [obj+8]` 로 카운트를
        // 올린 뒤 `call [*(obj)+0x30]` 을 부른다. 그 vtable 이 모듈 이미지 안이
        // 아니면 그 자리에서 죽는다 - 4번 다 예외가 난 자리로 가장 유력하다.
        if (owner8_ok && owner8 != 0) {
            const std::uintptr_t obj = static_cast<std::uintptr_t>(owner8);
            std::uint64_t objvt = 0;
            std::uint32_t rc = 0, flags = 0;
            const bool vt_ok = reader.read_value(obj, &objvt);
            const bool rc_ok = reader.read_value(obj + 8, &rc);
            reader.read_value(obj + 0xC, &flags);
            const bool in_image =
                vt_ok && objvt >= base &&
                objvt < base + static_cast<std::uint64_t>(reader.module_size());
            log::infof("    참조객체 0x{:X}: vtable 0x{:X} 읽힘={} 이미지안={}"
                       " 카운트={}({}) flags=0x{:X}",
                       obj, objvt, vt_ok ? 1 : 0, in_image ? 1 : 0, rc,
                       rc_ok ? "읽힘" : "못읽음", flags);
            if (in_image) {
                // 가상 호출 대상 [vtable+0x30] 도 이미지 안인지 본다.
                std::uint64_t fn = 0;
                const bool fn_ok =
                    reader.read_value(static_cast<std::uintptr_t>(objvt) + 0x30,
                                      &fn);
                const bool fn_in =
                    fn_ok && fn >= base &&
                    fn < base + static_cast<std::uint64_t>(reader.module_size());
                log::infof("      vtable+0x30 = 0x{:X} (RVA 0x{:X}) 이미지안={}", fn,
                           fn_in ? fn - base : 0, fn_in ? 1 : 0);
            }
        }
        log::infof("    맵: +0xE8 버킷수 {} · +0xEC {} · +0xF4 원소수 {}",
                   rd32(reader, comp + 0xE8), rd32(reader, comp + 0xEC),
                   rd32(reader, comp + kKnowMapCount));
        log::infof("    맵: +0xF8 버킷배열 0x{:X} · +0x100 값배열 0x{:X}",
                   rd64(reader, comp + 0xF8), rd64(reader, comp + 0x100));
        KnowTable t;
        if (know_table(reader, realm, &t)) {
            log::infof("    레벨표 0x{:X} 개수 {} 용량 {}", t.data, t.count, t.cap);
        } else {
            log::warnf("    레벨표를 못 읽었다");
        }
    }
    log::infof("지식 진단 ----- 끝");
}

bool know_register_locked() {
    return g_register_locked.load(std::memory_order_acquire);
}

// ------------------------------------------ 게임 스레드에 걸어 두기

namespace {
std::mutex g_q_mtx;
KnowQueue g_q;
// 디투어가 매 호출마다 보는 값. 뮤텍스 없이 읽는다.
std::atomic<bool> g_q_pending{false};
}  // namespace

bool knowledge_has_pending() {
    return g_q_pending.load(std::memory_order_acquire);
}

bool knowledge_queue_register(int number, int level) {
    std::lock_guard<std::mutex> lk(g_q_mtx);
    if (g_q.pending) return false;
    g_q.pending = true;
    g_q.number = number;
    g_q.level = level;
    g_q.has_result = false;
    g_q_pending.store(true, std::memory_order_release);
    log::infof("지식 등록 요청: {}번 레벨 {} - 게임 스레드를 기다린다", number,
               level);
    return true;
}

void knowledge_run_pending() {
    if (!g_q_pending.load(std::memory_order_acquire)) return;

    // **게임 스레드를 세우지 않는다.** 화면이 레벨 쓰기나 진단으로 이 락을 쥐고
    // 있을 수 있는데, 그때 기다리면 게임 로직 스레드가 그만큼 멈춘다. 못 잡으면
    // 요청을 그대로 두고 물러난다 - 다음 기회에 집어 가면 된다.
    std::unique_lock<std::mutex> op(g_op_mtx, std::try_to_lock);
    if (!op.owns_lock()) return;

    int number = 0;
    int level = 0;
    {
        std::lock_guard<std::mutex> lk(g_q_mtx);
        if (!g_q.pending) return;
        number = g_q.number;
        level = g_q.level;
        // **집어 가는 즉시 지운다.** 호출이 죽어도 같은 요청이 다시 실행되면
        // 안 된다(디투어는 곧바로 또 돈다).
        g_q.pending = false;
        g_q_pending.store(false, std::memory_order_release);
    }
    const mem::LocalReader reader;
    const KnowRegister r = register_held(reader, number, level);
    std::lock_guard<std::mutex> lk(g_q_mtx);
    g_q.has_result = true;
    g_q.result = r;
}

KnowQueue knowledge_queue_state() {
    std::lock_guard<std::mutex> lk(g_q_mtx);
    return g_q;
}

void knowledge_queue_clear_result() {
    std::lock_guard<std::mutex> lk(g_q_mtx);
    g_q.has_result = false;
}

}  // namespace cdtb::game

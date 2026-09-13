#include "game/knowledge.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
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
bool wr8(std::uintptr_t a, std::uint8_t v) {
    __try {
        *reinterpret_cast<volatile std::uint8_t*>(a) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
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
        // 게임도 같이 세우는 플래그. +0x08 은 건드리지 않는다(널이면 게임이 만든다).
        wr8(rec + kKnowRecFlag, 1);
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

}  // namespace cdtb::game

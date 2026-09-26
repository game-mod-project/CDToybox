// @build 1.0.0.2976  kGpvMgrGlobalRva -0x18 (2026-09-26, 실행 중 인스턴스를 담은 칸).
//   **kCondNameTableRva 를 2850 이후 처음 다시 짚었다** - 0x5955150, 468개
//   (`GetLevel` 로 시작하는 char* 열). kEmptyStrRva 는 여전히 미도출.
//   근거: specs/2026-09-26-game-update-2976.md §10 · §11
// @build 1.0.0.2949  kGpvMgrGlobalRva 가 그대로다(2026-09-22) - 2949 실행 중 그 전역이
//   `GamePlayVariableInfoManager`(개수 59)를 가리켰다(읽기 전용 탐침).
// @build 1.0.0.2850  **kCondNameTableRva · kEmptyStrRva 는 아직 미도출.**
//   원소 조건 이름 표시에만 쓰이고, 못 읽으면 번호로 떨어진다.

#include "game/reserveslot.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/log.h"
#include "game/knowledge.h"
#include "game/clan.h"
#include "game/wheelfill.h"
#include "game/roster.h"
#include "mem/safe_read.h"

namespace cdtb::game {
namespace {

std::uint64_t rd64(const mem::Reader& r, std::uintptr_t a) {
    std::uint64_t v = 0;
    return r.read_value(a, &v) ? v : 0;
}
std::uint32_t rd32(const mem::Reader& r, std::uintptr_t a) {
    std::uint32_t v = 0;
    return r.read_value(a, &v) ? v : 0;
}
std::uint16_t rd16(const mem::Reader& r, std::uintptr_t a) {
    std::uint16_t v = 0;
    return r.read_value(a, &v) ? v : 0;
}
std::uint8_t rd8(const mem::Reader& r, std::uintptr_t a) {
    std::uint8_t v = 0;
    return r.read_value(a, &v) ? v : 0;
}

// 매니저 하나(두 매니저가 같은 배치다). 말이 안 되면 array 가 0 이다.
struct Mgr {
    std::uintptr_t object = 0;
    std::uintptr_t array = 0;
    int count = 0;
};

// `quiet` 는 **매 프레임 부르는 자리**를 위한 것이다. 이 함수는 부를 때마다 한 줄씩
// 찍는데, 화면 절이 프레임마다 부르면 진단 로그가 통째로 묻힌다.
Mgr read_mgr(const mem::Reader& r, std::uintptr_t rva, const char* what,
             bool quiet = false) {
    Mgr m;
    const std::uintptr_t base = r.module_base();
    if (base == 0 || rva + 8 > r.module_size()) return m;
    const std::uintptr_t obj = static_cast<std::uintptr_t>(rd64(r, base + rva));
    if (obj < 0x10000) {
        if (!quiet) log::warnf("  {} 전역이 비었다(0x{:X})", what, obj);
        return m;
    }
    const int cnt = static_cast<int>(rd32(r, obj + kMgrCount));
    const std::uintptr_t arr =
        static_cast<std::uintptr_t>(rd64(r, obj + kMgrArray));
    if (!quiet) {
        log::infof("  {} 0x{:X} 개수 {} 배열 0x{:X}", what, obj, cnt, arr);
    }
    if (cnt <= 0 || cnt > kSlotMaxCount || arr < 0x10000) {
        if (!quiet) log::warnf("  {} 가 말이 안 된다 - 안 따라간다", what);
        return m;
    }
    m.object = obj;
    m.array = arr;
    m.count = cnt;
    return m;
}

std::uintptr_t info_at(const mem::Reader& r, const Mgr& m, int key) {
    if (m.array == 0 || key < 0 || key >= m.count) return 0;
    return static_cast<std::uintptr_t>(
        rd64(r, m.array + static_cast<std::uintptr_t>(key) * 8));
}

// 조건 객체를 **넓게** 날로 찍는다. 16바이트만 떴다가 문자열이 잘렸다
// (2026-09-14: "07 63 5F 43 6C 6F 61 6B" 까지만 보여 "c_Cloak" 인지 확신 못 했다).
//
// 모양을 가정하지 않는다. `_originalString` 이 SSO 인지 포인터인지 모르므로,
// 구간을 통째로 16바이트씩 찍고 **인쇄 가능한 조각은 옆에 같이** 낸다.
void dump_raw(const mem::Reader& r, std::uintptr_t at, int bytes,
              const char* what) {
    log::infof("    {} 0x{:X} ({}바이트)", what, at, bytes);
    for (int off = 0; off < bytes; off += 16) {
        std::uint8_t b[16] = {};
        if (!r.read(at + static_cast<std::uintptr_t>(off), b, 16)) break;
        std::string hex, txt;
        for (int i = 0; i < 16; ++i) {
            char t[4];
            std::snprintf(t, sizeof(t), "%02X ", b[i]);
            hex += t;
            txt += (b[i] >= 0x20 && b[i] < 0x7F) ? static_cast<char>(b[i]) : '.';
        }
        log::infof("      +{:02X}: {}| {}", off, hex, txt);
    }
    // 포인터로 보이면 그쪽도 한 번 따라가 본다.
    std::uint64_t p = 0;
    if (r.read_value(at, &p) && p >= 0x10000) {
        char buf[129] = {};
        if (r.read(static_cast<std::uintptr_t>(p), buf, 128)) {
            buf[128] = 0;
            bool ok = buf[0] >= 0x20 && buf[0] < 0x7F;
            if (ok) log::infof("      [{}+0 을 포인터로] \"{}\"", what, buf);
        }
    }
}

void dump_slot_info(const mem::Reader& r, const Mgr& cond, std::uintptr_t info,
                    const char* label) {
    if (info == 0) {
        log::warnf("  {}: ReserveSlotInfo 가 없다", label);
        return;
    }
    log::infof("  {} flags +0xAC..AF: {} {} {} {} · fill 0x{:X} x{} ·"
               " target 0x{:X} x{}",
               label, rd8(r, info + kRsFlags), rd8(r, info + kRsFlags + 1),
               rd8(r, info + kRsFlags + 2), rd8(r, info + kRsFlags + 3),
               rd64(r, info + kRsFillData), rd32(r, info + kRsFillCount),
               rd64(r, info + kRsTargetList), rd32(r, info + kRsTargetCount));

    // **가장 중요한 것** - 휠 칸의 후보 목록과 각 칸의 조건식.
    const std::uintptr_t e =
        static_cast<std::uintptr_t>(rd64(r, info + kRsNameHash));
    const int n = static_cast<int>(rd32(r, info + kRsNameHashCount));
    log::infof("    후보 목록 0x{:X} x{}", e, n);
    if (e < 0x10000 || n <= 0 || n > 256) return;
    for (int i = 0; i < n; ++i) {
        const std::uintptr_t at = e + static_cast<std::uintptr_t>(i) * 4;
        const std::uint16_t name = rd16(r, at);
        const std::uint16_t ck = rd16(r, at + 2);
        log::infof("    [{}] SpecialName {} · 조건키 {}", i, name,
                   ck == 0xFFFF ? -1 : static_cast<int>(ck));
        if (ck == 0xFFFF) continue;
        const std::uintptr_t ci = info_at(r, cond, ck);
        if (ci == 0) {
            log::warnf("      조건 {} 를 못 찾았다", ck);
            continue;
        }
        log::infof("      cond 0x{:X}: blocked {} · gameCondition 0x{:X} ·"
                   " parser 0x{:X}",
                   ci, rd8(r, ci + kCiBlocked), rd64(r, ci + kCiCondition),
                   rd32(r, ci + kCiParser));
        dump_raw(r, ci, 0x60, "ConditionInfo 전체");
    }
}

}  // namespace

void reserveslot_diagnose(const mem::Reader& reader,
                          std::uintptr_t player_actor) {
    log::infof("휠 진단 ----- 모듈 0x{:X}", reader.module_base());
    const Mgr slot = read_mgr(reader, kSlotMgrGlobalRva, "예약슬롯 매니저");
    const Mgr cond = read_mgr(reader, kCondMgrGlobalRva, "조건 매니저");

    int well[kWellKnownCount] = {};
    if (slot.object != 0) {
        static const char* kNames[kWellKnownCount] = {
            "ActionUseItem", "ActionUseItem_Vehicle", "ReserveQuickslot",
            "ArrowItem",     "VehicleSlot",           "ElementalSelectSlot",
            "Skill?",        "Skill?"};
        for (int i = 0; i < kWellKnownCount; ++i) {
            well[i] = rd16(reader, slot.object + kMgrWellKnown +
                                       static_cast<std::uintptr_t>(i) * 2);
            log::infof("  잘 알려진 키[{}] {} = {}", i, kNames[i], well[i]);
        }
    }

    const int elem = well[kElemSlotIndex];
    const int veh = well[4];
    // **전부 훑는다.** 원소와 탈것만 보다가 "깨달음 칸은?" 을 못 답했다 - 어느
    // 슬롯이 무엇인지 이름표가 없으므로, 28개를 다 찍고 눈으로 고르는 편이
    // 왕복 한 번보다 싸다. 후보 목록이 있는 슬롯만 자세히 판다.
    for (int k = 0; slot.object != 0 && k < slot.count; ++k) {
        const std::uintptr_t info = info_at(reader, slot, k);
        if (info == 0) continue;
        const int n = static_cast<int>(rd32(reader, info + kRsNameHashCount));
        const char* tag = k == elem   ? " <<< 원소"
                          : k == veh  ? " (탈것)"
                                      : "";
        log::infof("  슬롯정보[{}]{} key {} · blocked {} · type {} · usingType {}"
                   " · 후보 {}개",
                   k, tag, rd32(reader, info + kRsKey),
                   rd8(reader, info + kRsBlocked), rd8(reader, info + kRsType),
                   rd8(reader, info + kRsUsingType), n);
        if (n > 0) dump_slot_info(reader, cond, info, "  ^");
    }

    // ---- 런타임 컨테이너
    if (player_actor == 0) {
        log::warnf("  플레이어 액터를 아직 못 잡았다 - 런타임 슬롯은 건너뛴다");
        log::infof("휠 진단 ----- 끝");
        return;
    }
    const std::uintptr_t sub =
        static_cast<std::uintptr_t>(rd64(reader, player_actor + kActorSub));
    const std::uintptr_t sc =
        sub == 0 ? 0 : static_cast<std::uintptr_t>(rd64(reader, sub + kSubSlotComp));
    if (sc < 0x10000) {
        log::warnf("  슬롯 컴포넌트를 못 잡았다(액터 0x{:X} -> 0x{:X})",
                   player_actor, sc);
        log::infof("휠 진단 ----- 끝");
        return;
    }
    const std::uint64_t owner = rd64(reader, sc + kScOwner);
    const std::uintptr_t arr =
        static_cast<std::uintptr_t>(rd64(reader, sc + kScData));
    const int n = static_cast<int>(rd32(reader, sc + kScCount));
    log::infof("  슬롯컴프 0x{:X} 소유 0x{:X}(액터와 같은가 {}) 배열 0x{:X} 개수 {}",
               sc, owner, owner == player_actor ? 1 : 0, arr, n);
    if (arr < 0x10000 || n <= 0 || n > kSlotDumpMax * 4) {
        log::warnf("  슬롯 배열이 말이 안 된다 - 안 따라간다");
        log::infof("휠 진단 ----- 끝");
        return;
    }
    const int lim = n < kSlotDumpMax ? n : kSlotDumpMax;
    for (int i = 0; i < lim; ++i) {
        const std::uintptr_t ent =
            arr + static_cast<std::uintptr_t>(i) * kScStride;
        const std::uint16_t key = rd16(reader, ent);
        const std::uintptr_t rec = ent + kScRec;
        const char* tag = key == elem ? " <<< 원소"
                          : key == veh ? " (탈것)"
                                       : "";
        log::infof("  슬롯[{}] key {}{}: C8 {} · CA {} · CC {} · D8 {} · DC {} ·"
                   " E0 {} · E8 {} · EA {} · EC {}",
                   i, key, tag, rd16(reader, rec + 0xC8), rd16(reader, rec + 0xCA),
                   rd32(reader, rec + 0xCC), rd16(reader, rec + 0xD8),
                   rd32(reader, rec + 0xDC), rd32(reader, rec + 0xE0),
                   rd16(reader, rec + 0xE8), rd16(reader, rec + 0xEA),
                   rd16(reader, rec + 0xEC));
    }
    if (n > lim) log::infof("  (나머지 {}개 생략)", n - lim);
    log::infof("휠 진단 ----- 끝");
}


// ------------------------------------------------ 원소 조건 해독 (2026-09-15)

namespace {

inline constexpr std::uintptr_t kCondNameTableRva = 0x5955150;   // char* x468 (2949 까지 0x0584FA10)
inline constexpr int kCondNameCount = 468;
inline constexpr std::uintptr_t kEmptyStrRva = 0x0692E4C0;
inline constexpr std::uintptr_t kGpvMgrGlobalRva = 0x06D6E400;   // 2949 0x06D6E418 · 2850 까지 0x06C32880
inline constexpr std::size_t kGpvComp = 0x168;   // [액터+0x68] + 0x168

// 문자열 객체 하나를 읽는다. `p` 는 **객체 포인터가 든 자리**가 아니라 객체 자체다.
std::string read_str(const mem::Reader& r, std::uintptr_t obj,
                     std::uintptr_t image) {
    if (obj < 0x10000) return "(널)";
    if (obj == image + kEmptyStrRva) return "(빈 문자열 싱글턴)";
    std::uint64_t chars = 0;
    std::uint32_t len = 0;
    if (!r.read_value(obj, &chars) || chars < 0x10000) return "(못 읽음)";
    r.read_value(obj + 8, &len);
    if (len > 256) len = 256;
    std::string out(len ? len : 64, '\0');
    if (!r.read(static_cast<std::uintptr_t>(chars), out.data(), out.size())) {
        return "(본문 못 읽음)";
    }
    const std::size_t z = out.find('\0');
    if (z != std::string::npos) out.resize(z);
    return out;
}

std::string cond_fn_name(const mem::Reader& r, std::uintptr_t image, int idx) {
    if (idx < 0 || idx >= kCondNameCount) return "(색인 밖)";
    std::uint64_t p = 0;
    if (!r.read_value(image + kCondNameTableRva +
                          static_cast<std::uintptr_t>(idx) * 8,
                      &p) ||
        p < 0x10000) {
        return "(이름 못 읽음)";
    }
    char buf[65] = {};
    if (!r.read(static_cast<std::uintptr_t>(p), buf, 64)) return "(이름 못 읽음)";
    buf[64] = 0;
    return buf;
}

void dump_condition(const mem::Reader& r, const Mgr& cond, int key,
                    std::uintptr_t image) {
    const std::uintptr_t ci = info_at(r, cond, key);
    if (ci == 0) {
        log::warnf("  조건 {}: 없다", key);
        return;
    }
    // **0x40 을 넘지 않는다** - 넘겨서 이웃 객체를 읽고 틀린 결론을 낸 전례가 있다.
    log::infof("  조건 {} @0x{:X}: _key {} · blocked {} · parser {}", key, ci,
               rd32(r, ci + 0x00), rd8(r, ci + 0x10), rd8(r, ci + 0x30));
    log::infof("    _stringKey    = \"{}\"",
               read_str(r, static_cast<std::uintptr_t>(rd64(r, ci + 0x08)),
                        image));
    log::infof("    _originalStr  = \"{}\"",
               read_str(r, static_cast<std::uintptr_t>(rd64(r, ci + 0x28)),
                        image));
    log::infof("    bool 셋 +20/21/22 = {} {} {}", rd8(r, ci + 0x20),
               rd8(r, ci + 0x21), rd8(r, ci + 0x22));

    const std::uintptr_t gc = static_cast<std::uintptr_t>(rd64(r, ci + 0x18));
    if (gc < 0x10000) {
        log::warnf("    gameCondition 이 없다(0x{:X})", gc);
        return;
    }
    const std::uint64_t vt = rd64(r, gc);
    const int idx = static_cast<int>(rd16(r, gc + 0x08));
    log::infof("    gameCondition 0x{:X} vtable RVA 0x{:X} · 색인 {} = {}", gc,
               vt >= image ? vt - image : 0, idx,
               cond_fn_name(r, image, idx));
    // 인자 구간. 클래스마다 다르므로 날로 본다(0x40 만).
    for (int off = 0x10; off < 0x50; off += 16) {
        std::uint8_t b[16] = {};
        if (!r.read(gc + static_cast<std::uintptr_t>(off), b, 16)) break;
        std::string hex, txt;
        for (int i = 0; i < 16; ++i) {
            char t[4];
            std::snprintf(t, sizeof(t), "%02X ", b[i]);
            hex += t;
            txt += (b[i] >= 0x20 && b[i] < 0x7F) ? static_cast<char>(b[i]) : '.';
        }
        log::infof("      gc+{:02X}: {}| {}", off, hex, txt);
    }
    log::infof("      (u16 @gc+0x18 = {} · u32 @gc+0x30 = {})", rd16(r, gc + 0x18),
               rd32(r, gc + 0x30));
}

bool name_interesting(const std::string& a, const std::string& b) {
    static const char* kWords[] = {"byss", "ate", "lement", "lem_", "torm",
                                   "ight", "ire",  "ind",   "ce"};
    for (const char* w : kWords) {
        if (a.find(w) != std::string::npos) return true;
        if (b.find(w) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

void element_diagnose(const mem::Reader& reader, std::uintptr_t player_actor) {
    const std::uintptr_t image = reader.module_base();
    log::infof("원소 진단 ----- 모듈 0x{:X}", image);

    // 7.1 조건 넷의 정체
    const Mgr cond = read_mgr(reader, kCondMgrGlobalRva, "조건 매니저");
    if (cond.object != 0) {
        for (int k = 9198; k <= 9201; ++k) dump_condition(reader, cond, k, image);
    }

    // 7.2 GamePlayVariable 정적 표 - 이름/메모에 단서가 있는 것만
    const Mgr gpv = read_mgr(reader, kGpvMgrGlobalRva, "진행변수 매니저");
    int hit = 0;
    for (int k = 0; gpv.object != 0 && k < gpv.count; ++k) {
        const std::uintptr_t info = info_at(reader, gpv, k);
        if (info == 0) continue;
        const std::string nm =
            read_str(reader, static_cast<std::uintptr_t>(rd64(reader, info + 8)),
                     image);
        const std::string memo = read_str(
            reader, static_cast<std::uintptr_t>(rd64(reader, info + 0x18)),
            image);
        if (!name_interesting(nm, memo)) continue;
        if (++hit > 80) continue;
        log::infof("  진행변수[{}] key {} · 기본 {} · blocked {} · \"{}\" / \"{}\"",
                   k, rd32(reader, info), rd8(reader, info + 0x11),
                   rd8(reader, info + 0x10), nm, memo);
    }
    if (gpv.object != 0) {
        log::infof("  진행변수 표 {}개 중 단서 있는 것 {}개(80개까지만 찍음)",
                   gpv.count, hit);
    }

    // ---- 지식의 **내부 이름**으로 원소 지식 넷을 찾는다 (2026-09-15)
    //
    // 조건 넷이 평문으로 나왔다:
    //   CheckEquipSlotName(Bracelet) && CheckKnowledge(Knowledge_MpFire|Ice|Lightning|Wind)
    // 즉 필요한 것은 **팔찌 장착 + 그 지식**이다. 사용자가 배운 "원소 : …" 여섯은
    // 원소 강화 스킬이지 이 Mp* 지식이 아니었다.
    //
    // `ConditionInfo` 와 `GamePlayVariableInfo` 가 둘 다 `+0x08` 에 내부 이름 문자열을
    // 들고 있었으므로 `KnowledgeInfo` 도 같은 배치일 것으로 본다. **가정이므로**
    // 못 찾으면 앞쪽 몇 개를 그대로 찍어 무엇이 들어 있는지 보이게 한다.
    {
        const Mgr kn = read_mgr(reader, kKnowMgrGlobalRva, "지식 매니저");
        int found = 0, sampled = 0;
        for (int k = 0; kn.object != 0 && k < kn.count; ++k) {
            const std::uintptr_t info = info_at(reader, kn, k);
            if (info == 0) continue;
            const std::string nm = read_str(
                reader, static_cast<std::uintptr_t>(rd64(reader, info + 8)),
                image);
            if (nm.find("Mp") != std::string::npos ||
                nm.find("Elem") != std::string::npos) {
                ++found;
                if (found <= 40) {
                    log::infof("  지식[{}] 내부이름 \"{}\" · 붙는스킬 {}", k, nm,
                               rd16(reader, info + 0x104));
                }
            } else if (sampled < 5 && !nm.empty() && nm[0] != '(') {
                ++sampled;
                log::infof("  (표본) 지식[{}] +0x08 = \"{}\"", k, nm);
            }
        }
        if (kn.object != 0) {
            log::infof("  지식 {}개 중 이름에 Mp/Elem 이 든 것 {}개", kn.count,
                       found);
            if (found == 0 && sampled == 0) {
                log::warnf("  +0x08 이 이름이 아닌 것 같다 - 앞 3개를 날로 찍는다");
                for (int k = 0; k < 3; ++k) {
                    const std::uintptr_t info = info_at(reader, kn, k);
                    if (info == 0) continue;
                    dump_raw(reader, info, 0x20, "KnowledgeInfo 머리");
                }
            }
        }
    }

    // 7.3 플레이어의 진행변수 표
    if (player_actor != 0) {
        const std::uintptr_t sub =
            static_cast<std::uintptr_t>(rd64(reader, player_actor + kActorSub));
        const std::uintptr_t gc =
            sub == 0 ? 0
                     : static_cast<std::uintptr_t>(rd64(reader, sub + kGpvComp));
        if (gc >= 0x10000) {
            log::infof("  플레이어 진행변수 컴프 0x{:X}: 버킷 {} · 원소 {} ·"
                       " 버킷배열 0x{:X} · 값배열 0x{:X}",
                       gc, rd32(reader, gc + 0x1F8), rd32(reader, gc + 0x1FC),
                       rd64(reader, gc + 0x208), rd64(reader, gc + 0x210));
        } else {
            log::warnf("  플레이어 진행변수 컴프를 못 잡았다(0x{:X})", gc);
        }
    }
    log::infof("원소 진단 ----- 끝");
}


// ------------------------------------------------ 원소 습득 (2026-09-15)

namespace {
// 이름 훑기 결과. 매니저 주소가 그대로면 다시 안 찾는다.
std::uintptr_t g_elem_mgr = 0;
int g_elem_num[kElementCount] = {-1, -1, -1, -1};
}  // namespace

bool element_knowledge(const mem::Reader& reader,
                       ElementKnow out[kElementCount]) {
    static const char* kLabel[kElementCount] = {"화염", "냉기", "벼락", "바람"};
    static const char* kName[kElementCount] = {
        "Knowledge_MpFire", "Knowledge_MpIce", "Knowledge_MpLightning",
        "Knowledge_MpWind"};
    for (int i = 0; i < kElementCount; ++i) {
        out[i] = ElementKnow{kLabel[i], kName[i], -1, 0};
    }
    const std::uintptr_t image = reader.module_base();
    const Mgr kn = read_mgr(reader, kKnowMgrGlobalRva, "지식 매니저", true);
    if (kn.object == 0) {
        g_elem_mgr = 0;
        return false;
    }

    // 이름으로 찾는다 - 번호를 박으면 게임 갱신에 밀린다. 다만 이름 훑기는 6천 개를
    // 도는 일이라 **화면이 프레임마다 할 짓이 아니다.** 매니저가 그대로면 한 번
    // 찾은 것을 그대로 쓴다.
    if (g_elem_mgr != kn.object) {
        int found[kElementCount] = {-1, -1, -1, -1};
        int hit = 0;
        for (int k = 0; k < kn.count && hit < kElementCount; ++k) {
            const std::uintptr_t info = info_at(reader, kn, k);
            if (info == 0) continue;
            const std::string nm =
                read_str(reader,
                         static_cast<std::uintptr_t>(rd64(reader, info + 8)),
                         image);
            if (nm.empty() || nm.rfind("Knowledge_Mp", 0) != 0) continue;
            for (int i = 0; i < kElementCount; ++i) {
                if (found[i] >= 0 || nm != kName[i]) continue;
                found[i] = k;
                ++hit;
                break;
            }
        }
        if (hit == 0) return false;
        for (int i = 0; i < kElementCount; ++i) g_elem_num[i] = found[i];
        g_elem_mgr = kn.object;
        // **증거를 남긴다.** 다음에 "안 켜진다" 가 오면 어느 번호를 쓴 건지부터 본다.
        log::infof("원소 지식을 이름으로 찾았다: 화염 {} · 냉기 {} · 벼락 {} ·"
                   " 바람 {} (지식 {}개 중)",
                   found[0], found[1], found[2], found[3], kn.count);
    }
    for (int i = 0; i < kElementCount; ++i) out[i].number = g_elem_num[i];

    // 지금 레벨. 서버 realm 의 표를 본다.
    KnowTable t;
    if (know_table(reader, 0, &t)) {
        for (int i = 0; i < kElementCount; ++i) {
            if (out[i].number < 0) continue;
            const int lv = know_level(reader, t, out[i].number);
            out[i].level = lv > 0 ? lv : 0;
        }
    }
    return true;
}

// ------------------------------------------------------------- 탈것 휠 해금

namespace {

// 우리가 가리키게 할 배열. **게임이 이 포인터를 들고 읽으므로 DLL 수명 내내
// 살아 있어야 한다.** 정적이라 해제되지 않고, 프록시 DLL 은 사실상 언로드되지
// 않는다(nofall 케이브와 같은 판단).
std::uint16_t g_wheel_buf[kWheelMaxCats] = {};

struct WheelBackup {
    bool held = false;
    bool repointed = false;   // 포인터·용량까지 바꿨는가
    std::uintptr_t info = 0;
    std::uint64_t ptr = 0;
    std::uint32_t count = 0;
    std::uint32_t cap = 0;
};
WheelBackup g_wheel_bak;

bool wr(std::uintptr_t a, const void* src, std::size_t n) {
    return mem::safe_write_bytes(a, src, n);
}
bool wr32(std::uintptr_t a, std::uint32_t v) { return wr(a, &v, sizeof v); }
bool wr64(std::uintptr_t a, std::uint64_t v) { return wr(a, &v, sizeof v); }

bool read_wheel_slot(const mem::Reader& r, std::uintptr_t info, WheelSlot* out) {
    out->info = info;
    out->key = static_cast<int>(rd32(r, info + kRsKey));
    const std::uintptr_t p =
        static_cast<std::uintptr_t>(rd64(r, info + kRsMercList));
    const int n = static_cast<int>(rd32(r, info + kRsMercCount));
    // 목록이 비었거나 터무니없으면 그 슬롯은 안 믿는다.
    if (p < 0x10000 || n <= 0 || n > kWheelMaxCats) return false;
    out->count = n;
    for (int i = 0; i < n; ++i) {
        out->cats[i] = rd16(r, p + static_cast<std::uintptr_t>(i) * 2);
    }
    return true;
}

// 배열은 런타임 색인(0..count-1)으로 걷고, 데이터 키는 레코드 `+0x00` 에 있다.
// 색인을 박지 않고 키로 찾는 이유는 갱신 때 색인이 밀려도 따라가기 위해서다.
std::uintptr_t find_slot_by_key(const mem::Reader& r, const Mgr& m, int key) {
    for (int i = 0; i < m.count; ++i) {
        const std::uintptr_t info = info_at(r, m, i);
        if (info == 0) continue;
        if (static_cast<int>(rd32(r, info + kRsKey)) == key) return info;
    }
    return 0;
}

void set_note(WheelState* s, const char* msg) {
    std::snprintf(s->note, sizeof s->note, "%s", msg);
}

}  // namespace

int wheel_merge(const int* base, int base_n, const int* add, int add_n,
                int* out, int out_cap) {
    if (out == nullptr || out_cap <= 0) return 0;
    int n = 0;
    if (base != nullptr) {
        for (int i = 0; i < base_n && n < out_cap; ++i) out[n++] = base[i];
    }
    if (add == nullptr) return n;
    for (int i = 0; i < add_n && n < out_cap; ++i) {
        bool dup = false;
        for (int j = 0; j < n; ++j) {
            if (out[j] == add[i]) {
                dup = true;
                break;
            }
        }
        if (!dup) out[n++] = add[i];
    }
    return n;
}

WheelState wheel_state(const mem::Reader& reader) {
    WheelState s;
    // 화면이 프레임마다 부른다 - 조용히 읽는다.
    const Mgr m = read_mgr(reader, kSlotMgrGlobalRva, "예약슬롯 매니저", true);
    if (m.object == 0) {
        set_note(&s, "예약 슬롯 표를 아직 못 잡았습니다");
        return s;
    }
    const std::uintptr_t vi = find_slot_by_key(reader, m, kVehSlotKey);
    const std::uintptr_t di = find_slot_by_key(reader, m, kDragonSlotKey);
    const std::uintptr_t mi = find_slot_by_key(reader, m, kMechSlotKey);
    if (vi == 0 || di == 0 || mi == 0) {
        set_note(&s, "탈것 슬롯 셋 중 일부를 못 찾았습니다(게임 갱신?)");
        return s;
    }
    if (!read_wheel_slot(reader, vi, &s.main_slot) ||
        !read_wheel_slot(reader, di, &s.dragon) ||
        !read_wheel_slot(reader, mi, &s.mech)) {
        set_note(&s, "허용 목록이 말이 안 됩니다 - 안 건드립니다");
        return s;
    }
    s.ready = true;
    // 더할 것은 **드래곤·메카닉 슬롯이 지금 들고 있는 값 그대로**다.
    int add[kWheelMaxCats * 2] = {};
    int an = 0;
    for (int i = 0; i < s.dragon.count; ++i) add[an++] = s.dragon.cats[i];
    for (int i = 0; i < s.mech.count; ++i) add[an++] = s.mech.cats[i];
    s.want_count = wheel_merge(s.main_slot.cats, s.main_slot.count, add, an,
                               s.want, kWheelMaxCats);
    s.on = g_wheel_bak.held && g_wheel_bak.info == vi;
    return s;
}

bool wheel_unlock(const mem::Reader& reader, bool on) {
    // **끄는 길은 두지 않는다(2026-09-15, 실측 근거).** 실측 순서는 이랬다 -
    // **켠 직후** 특수 탑승물이 먹통이 됐고, 체크를 풀어도 **안 돌아왔고**,
    // 게임 재시작으로 돌아왔다. 즉 되돌리기는 고치지 못한다.
    //
    // 그러므로 줄이는 연산은 얻는 것 없이 위험만 보탠다. 정적 표는 세이브에
    // 안 남으므로 **재시작이 유일하고 확실한 되돌리기**다.
    if (!on) return false;
    if (g_wheel_bak.held) return true;   // 이미 걸려 있다

    const WheelState s = wheel_state(reader);
    if (!s.ready) return false;
    if (s.want_count <= s.main_slot.count) return true;  // 더할 것이 없다

    const std::uintptr_t info = s.main_slot.info;
    const std::uint64_t ptr = rd64(reader, info + kRsMercList);
    const std::uint32_t cnt = rd32(reader, info + kRsMercCount);
    const std::uint32_t cap = rd32(reader, info + kRsMercCap);
    if (ptr < 0x10000 || static_cast<int>(cnt) != s.main_slot.count) return false;

    g_wheel_bak = WheelBackup{true, false, info, ptr, cnt, cap};

    bool ok = true;
    if (cap >= static_cast<std::uint32_t>(s.want_count)) {
        // 용량이 남으면 **제자리로 뒤에 붙인다** - 포인터를 안 바꾸는 쪽이
        // 훨씬 안전하다(게임이 이 배열을 해제할 일이 생겨도 자기 것이다).
        for (int i = s.main_slot.count; i < s.want_count && ok; ++i) {
            const std::uint16_t v = static_cast<std::uint16_t>(s.want[i]);
            ok = wr(static_cast<std::uintptr_t>(ptr) +
                        static_cast<std::uintptr_t>(i) * 2,
                    &v, sizeof v);
        }
    } else {
        for (int i = 0; i < s.want_count; ++i) {
            g_wheel_buf[i] = static_cast<std::uint16_t>(s.want[i]);
        }
        ok = wr64(info + kRsMercList,
                  static_cast<std::uint64_t>(
                      reinterpret_cast<std::uintptr_t>(g_wheel_buf)));
        ok = wr32(info + kRsMercCap, kWheelMaxCats) && ok;
        g_wheel_bak.repointed = true;
    }
    // **개수는 맨 마지막에 올린다** - 값이 다 들어가기 전에 게임이 읽으면
    // 쓰레기 카테고리를 본다.
    ok = wr32(info + kRsMercCount, static_cast<std::uint32_t>(s.want_count)) && ok;

    if (!ok) {
        // **줄이지 않는다.** 개수는 맨 마지막에 올리므로 여기까지 왔다면 게임이
        // 보는 개수는 아직 옛 값이다 - 그대로 두는 것이 가장 안전하다. 목록을
        // 줄이는 연산이 이 파일에 아예 없어야 §3.12 의 어긋남이 안 생긴다.
        g_wheel_bak = WheelBackup{};
        log::warnf("탈것 휠: 쓰기에 실패했다 - 개수는 안 올렸으니 그대로다");
        return false;
    }
    log::infof("탈것 휠: 메인 슬롯 허용 {}개 -> {}개 ({})", s.main_slot.count,
               s.want_count, g_wheel_bak.repointed ? "새 배열" : "제자리");
    return true;
}

// **일부러 아무것도 안 한다.** 다른 기능들과 다르다 - 여기서 목록을 줄이면
// 모드를 내리는 순간(F10) 게임 쪽 휠이 어긋나 특수 탑승물이 먹통이 된다.
// 정적 표는 세이브에 안 남으므로 게임을 껐다 켜면 저절로 원복된다.
void wheel_teardown() {
    if (!g_wheel_bak.held) return;
    log::infof("탈것 휠: 늘린 목록은 그대로 둔다 - 되돌리려면 게임을 다시 시작한다"
               " (줄이면 게임 쪽 휠이 어긋난다)");
    g_wheel_bak = WheelBackup{};
}

// ------------------------------------------- 탈것 쿨다운·시간제한

namespace {

// wr / wr32 / wr64 는 위 휠 절의 익명 네임스페이스에 있다 - 같은 파일이므로
// 그대로 보인다.

struct MountBak {
    std::uintptr_t rec = 0;
    std::uint64_t cool = 0;
    std::uint64_t dur = 0;
};
MountBak g_mount_bak[kMountPatchMax];
int g_mount_bak_n = 0;

// 캐릭터 표를 걷는다. 매니저는 카탈로그가 RTTI 로 찾아 둔 것이다 - 고정 전역은
// 갱신마다 고르지 않게 밀린다(roster.h 의 소환 표 주석).
bool char_table(const mem::Reader& r, std::uintptr_t* records, int* count) {
    const std::uintptr_t mgr = roster_char_manager();
    if (mgr == 0) return false;
    std::uint32_t n = 0;
    std::uintptr_t recs = 0;
    if (!roster_header(r, mgr, &n, &recs)) return false;
    if (n == 0 || recs < 0x10000) return false;
    *records = recs;
    *count = static_cast<int>(n);
    return true;
}

}  // namespace

bool mount_needs_free(std::uint16_t vehicle_info, std::uint64_t cool,
                      std::uint64_t dur) {
    if (vehicle_info == 0) return false;               // 탈것이 아니다
    if (cool > kCoolTimeFree) return true;             // 쿨다운이 남았다
    if (dur > 0 && dur < kDurationFree) return true;   // 시간제한이 남았다
    return false;
}

MountTimerState mount_timer_state(const mem::Reader& reader) {
    MountTimerState s;
    std::uintptr_t recs = 0;
    int n = 0;
    if (!char_table(reader, &recs, &n)) {
        std::snprintf(s.note, sizeof s.note,
                      "%s", "캐릭터 표를 아직 못 잡았습니다 (로스터 준비 중)");
        return s;
    }
    s.ready = true;
    for (int i = 0; i < n; ++i) {
        const std::uintptr_t rec = static_cast<std::uintptr_t>(
            rd64(reader, recs + static_cast<std::uintptr_t>(i) * 8));
        if (rec < 0x10000) continue;
        const std::uint16_t vi = rd16(reader, rec + kCiVehicleInfo);
        if (vi == 0) continue;
        ++s.mounts;
        std::uint64_t cool = 0, dur = 0;
        reader.read_value(rec + kCiCoolTime, &cool);
        reader.read_value(rec + kCiSpawnDuration, &dur);
        if (mount_needs_free(vi, cool, dur)) ++s.timed;
    }
    s.on = g_mount_bak_n > 0;
    return s;
}

bool mount_timer_free(const mem::Reader& reader, bool on) {
    if (!on) {
        // 되돌리기는 리더가 필요 없다 - 주소와 원본을 같이 들고 있다.
        mount_timer_teardown();
        return true;
    }
    if (g_mount_bak_n > 0) return true;   // 이미 걸려 있다

    std::uintptr_t recs = 0;
    int n = 0;
    if (!char_table(reader, &recs, &n)) return false;

    int done = 0, skipped = 0;
    for (int i = 0; i < n && g_mount_bak_n < kMountPatchMax; ++i) {
        const std::uintptr_t rec = static_cast<std::uintptr_t>(
            rd64(reader, recs + static_cast<std::uintptr_t>(i) * 8));
        if (rec < 0x10000) continue;
        const std::uint16_t vi = rd16(reader, rec + kCiVehicleInfo);
        std::uint64_t cool = 0, dur = 0;
        if (!reader.read_value(rec + kCiCoolTime, &cool)) continue;
        if (!reader.read_value(rec + kCiSpawnDuration, &dur)) continue;
        if (!mount_needs_free(vi, cool, dur)) continue;
        // **원본을 먼저 적는다.** 쓰다 실패해도 되돌릴 수 있어야 한다.
        g_mount_bak[g_mount_bak_n] = MountBak{rec, cool, dur};
        ++g_mount_bak_n;
        bool ok = wr64(rec + kCiCoolTime, kCoolTimeFree);
        // 시간제한은 **원래 걸려 있던 것만** 늘린다. 0(제한 없음)을 큰 수로
        // 바꾸면 제한이 없던 탈것에 제한을 새로 거는 셈이다.
        if (ok && dur > 0) ok = wr64(rec + kCiSpawnDuration, kDurationFree);
        if (ok) {
            ++done;
        } else {
            ++skipped;
        }
    }
    if (done == 0) {
        mount_timer_teardown();
        log::warnf("탈것 타이머: 한 건도 못 썼다 - 그대로 둔다");
        return false;
    }
    log::infof("탈것 타이머: {}개 풀었다 (쿨다운 {}초 · 시간제한 {}초){}", done,
               kCoolTimeFree, kDurationFree,
               skipped != 0 ? " · 일부 쓰기 실패" : "");
    return true;
}

// ------------------------------------------- 호출 장소 제한

namespace {

struct VehBak {
    std::uintptr_t rec = 0;
    float dist = 0.0f;
};
VehBak g_veh_bak[kVehiclePatchMax];
int g_veh_bak_n = 0;

bool veh_table(const mem::Reader& r, std::uintptr_t* records, int* count) {
    const std::uintptr_t mgr = roster_vehicle_manager();
    if (mgr == 0) return false;
    std::uint32_t n = 0;
    std::uintptr_t recs = 0;
    if (!roster_header(r, mgr, &n, &recs)) return false;
    if (n == 0 || n > 4096 || recs < 0x10000) return false;
    *records = recs;
    *count = static_cast<int>(n);
    return true;
}

float rdf(const mem::Reader& r, std::uintptr_t a) {
    float v = 0.0f;
    return r.read_value(a, &v) ? v : 0.0f;
}

}  // namespace

bool vehicle_place_gated(float ground_dist) {
    // NaN 은 비교가 전부 거짓이라 여기서도 조용히 빠진다 - 그것이 맞다
    // (모르는 값을 0 으로 덮지 않는다).
    return ground_dist > 0.0f;
}

bool disguise_wants_swap(int in_world_count) {
    // 한 마리라도 나와 있으면 제 타입으로 둔다.
    return in_world_count <= 0;
}

bool escape_road_differs(std::uint8_t mine, std::uint8_t donor) {
    // 0 은 "없음" 으로 보고 안 건드린다 - 기증자가 0 이면 베낄 것이 없다.
    return donor != 0 && mine != donor;
}

bool spawn_voxel_gated(std::uint32_t count) {
    // 목록이 비면 아무 데서나 부를 수 있다 - A.T.A.G. 가 그 상태다.
    return count > 0;
}

bool vehicle_flies(float max_allowable_height) {
    // 천장이 있어야 나는 것이다. FLT_MAX(3.40282e+38)는 "상한 없음" = 땅 것.
    // NaN 도 여기서 거짓으로 떨어진다.
    return max_allowable_height > 0.0f && max_allowable_height < 1.0e30f;
}

CallPlaceState call_place_state(const mem::Reader& reader) {
    CallPlaceState s;
    std::uintptr_t recs = 0;
    int n = 0;
    if (!veh_table(reader, &recs, &n)) {
        std::snprintf(s.note, sizeof s.note, "%s",
                      "탈것 표를 아직 못 잡았습니다 (로스터 준비 중)");
        return s;
    }
    s.ready = true;
    s.rows = n;
    for (int i = 0; i < n; ++i) {
        const std::uintptr_t rec = static_cast<std::uintptr_t>(
            rd64(reader, recs + static_cast<std::uintptr_t>(i) * 8));
        if (rec < 0x10000) continue;
        if (vehicle_place_gated(rdf(reader, rec + kViGroundDist))) ++s.gated;
    }
    s.on = g_veh_bak_n > 0;
    return s;
}

bool call_place_free(const mem::Reader& reader, bool on) {
    if (!on) {
        call_place_teardown();
        return true;
    }
    if (g_veh_bak_n > 0) return true;

    std::uintptr_t recs = 0;
    int n = 0;
    if (!veh_table(reader, &recs, &n)) return false;

    int done = 0;
    for (int i = 0; i < n && g_veh_bak_n < kVehiclePatchMax; ++i) {
        const std::uintptr_t rec = static_cast<std::uintptr_t>(
            rd64(reader, recs + static_cast<std::uintptr_t>(i) * 8));
        if (rec < 0x10000) continue;
        const float d = rdf(reader, rec + kViGroundDist);
        if (!vehicle_place_gated(d)) continue;
        // 원본을 먼저 적는다 - 쓰다 실패해도 되돌릴 수 있어야 한다.
        g_veh_bak[g_veh_bak_n] = VehBak{rec, d};
        ++g_veh_bak_n;
        const float zero = 0.0f;
        if (mem::safe_write_bytes(rec + kViGroundDist, &zero, sizeof zero)) {
            ++done;
        }
    }
    if (done == 0) {
        call_place_teardown();
        log::warnf("호출 장소 제한: 한 건도 못 썼다 - 그대로 둔다");
        return false;
    }
    log::infof("호출 장소 제한: {}개 풀었다 (지면 거리 검사 -> 0)", done);
    return true;
}

// ---------------------------------- 드래곤·ATAG 를 "되는 탈것" 규칙으로

namespace {

struct DisguiseBak {
    std::uintptr_t rec = 0;
    std::uint16_t merc = 0;        // 원래 타입행
    std::uint16_t veh = 0;         // 원래 탈것규칙(안 쓰지만 기록해 둔다)
    std::uint16_t row = 0xFFFF;    // 종행
    std::uint16_t donor = 0;       // 휠에서 부를 때 쓸 타입행
    bool applied = false;          // 지금 휠용으로 바꿔 둔 상태인가
};
DisguiseBak g_dis_bak[kDisguiseMax];
int g_dis_bak_n = 0;

// 대상이 **자기** 탈것 규칙에 가진 지면 거리 검사를 푼 자국. 규칙 행 자체는
// 안 바꾸므로(그건 무엇이 나오는지를 정한다) 이 칸 하나만 되돌리면 된다.
VehBak g_dis_vbak[kDisguiseMax];
int g_dis_vbak_n = 0;

// 호출 지형 요구를 푼 자국. 목록 포인터와 용량은 그대로 두고 개수만 0 으로
// 둔다 - 되돌릴 때 개수만 쓰면 원래 목록이 그대로 살아난다.
struct VoxBak {
    std::uintptr_t rec = 0;
    std::uint32_t count = 0;
};
VoxBak g_dis_xbak[kDisguiseMax];
int g_dis_xbak_n = 0;

// 탈출로 그룹을 기증자 값으로 맞춘 자국(탈것 규칙 행의 u8 한 칸).
struct RoadBak {
    std::uintptr_t rec = 0;
    std::uint8_t group = 0;
};
RoadBak g_dis_rbak[kDisguiseMax];
int g_dis_rbak_n = 0;

bool wr16(std::uintptr_t a, std::uint16_t v) {
    return mem::safe_write_bytes(a, &v, sizeof v);
}


// 대상·기증자를 한 번에 고른다.
//
// **메인 휠 목록에 "없는" 것을 대상으로 삼지 않는다.** 얹기가 거기에 2·3·4 를
// 붙여 놓으면 경계가 뒤집혀 정작 드래곤·ATAG 는 빠지고 반려 동물·용병이
// 대상이 된다(2026-09-15 사고). 경계는 얹기가 건드리지 않는 드래곤 슬롯·정비
// 슬롯의 목록이 정한다.
struct DisguisePlan {
    std::uintptr_t rec[kDisguiseMax] = {};
    std::uint16_t row[kDisguiseMax] = {};
    int n = 0;
    int donor_merc = -1;
    int donor_veh = -1;
    int donor_row = -1;
};

bool build_plan(const mem::Reader& r, DisguisePlan* p, char* note,
                std::size_t note_cap) {
    const mem::Rtti* rtti = clan_rtti();
    if (rtti == nullptr) {
        std::snprintf(note, note_cap, "%s", "RTTI 준비 전입니다");
        return false;
    }
    const WheelState w = wheel_state(r);
    if (!w.ready) {
        std::snprintf(note, note_cap, "%s", w.note);
        return false;
    }
    if (w.dragon.count <= 0 && w.mech.count <= 0) {
        std::snprintf(note, note_cap, "%s",
                      "드래곤·정비 슬롯 목록이 비었습니다");
        return false;
    }
    std::uintptr_t clan = 0;
    if (!clan_component_fast(r, *rtti, false, &clan)) {
        std::snprintf(note, note_cap, "%s", "명부를 아직 못 잡았습니다");
        return false;
    }
    std::vector<ClanEntry> list;
    if (!read_clan_roster(r, clan, &list)) {
        std::snprintf(note, note_cap, "%s", "명부를 읽지 못했습니다");
        return false;
    }
    std::uintptr_t recs = 0;
    int cn = 0;
    {
        const std::uintptr_t mgr = roster_char_manager();
        std::uint32_t n = 0;
        if (mgr == 0 || !roster_header(r, mgr, &n, &recs)) {
            std::snprintf(note, note_cap, "%s", "캐릭터 표를 아직 못 잡았습니다");
            return false;
        }
        cn = static_cast<int>(n);
    }
    auto char_rec = [&](std::uint32_t row) -> std::uintptr_t {
        if (static_cast<int>(row) >= cn) return 0;
        return static_cast<std::uintptr_t>(
            rd64(r, recs + static_cast<std::uintptr_t>(row) * 8));
    };
    // 잠긴 목록 = 드래곤 슬롯 + 정비 슬롯. 얹기는 이 둘을 안 건드리므로, 여기서
    // 얹기 전의 경계를 그대로 되찾는다.
    int lock[kWheelMaxCats * 2] = {};
    int ln = 0;
    for (int i = 0; i < w.dragon.count && ln < kWheelMaxCats * 2; ++i) {
        lock[ln++] = w.dragon.cats[i];
    }
    for (int i = 0; i < w.mech.count && ln < kWheelMaxCats * 2; ++i) {
        lock[ln++] = w.mech.cats[i];
    }
    auto role = [&](int merc_row) {
        return disguise_role(w.main_slot.cats, w.main_slot.count, lock, ln,
                             merc_row);
    };
    auto locked = [&](int merc_row) {
        return role(merc_row) == DisguiseRole::Target;
    };
    auto native = [&](int merc_row) {
        return role(merc_row) == DisguiseRole::DonorCat;
    };
    // 기증자는 **드래곤과 같은 부류**, 즉 나는 것에서 고른다. 땅 것은 높이
    // 상한이 아예 없고(FLT_MAX) 나는 것은 천장이 있다 - 실측 와이번 1350 ·
    // 말 3.40282e+38(2026-09-15). 처음엔 지면 거리 검사로 갈랐는데 말도 0 이라
    // 안 갈렸고, 그래서 말이 기증자가 됐다.
    std::uintptr_t veh_recs = 0;
    int veh_n = 0;
    veh_table(r, &veh_recs, &veh_n);
    auto flies = [&](std::uint16_t veh) {
        if (veh_recs == 0 || static_cast<int>(veh) >= veh_n) return false;
        const std::uintptr_t rec = static_cast<std::uintptr_t>(
            rd64(r, veh_recs + static_cast<std::uintptr_t>(veh) * 8));
        if (rec < 0x10000) return false;
        return vehicle_flies(rdf(r, rec + kViMaxHeight));
    };

    // 기증자: 메인 휠이 **원래** 받던 타입 + 진짜 탈것. 1차는 나는 것만, 없으면
    // 2차에서 아무거나.
    for (int pass = 0; pass < 2 && p->donor_merc < 0; ++pass) {
        for (const auto& e : list) {
            if (e.merc_row == kInfoNone || !native(e.merc_row)) continue;
            const std::uintptr_t rec = char_rec(e.row);
            if (rec == 0) continue;
            const std::uint16_t veh = rd16(r, rec + kCiVehicleInfo);
            if (veh == kInfoNone) continue;   // 탈것이 아니면 못 베낀다
            if (pass == 0 && !flies(veh)) continue;
            p->donor_merc = e.merc_row;
            p->donor_veh = veh;
            p->donor_row = static_cast<int>(e.row);
            break;
        }
    }
    if (p->donor_merc < 0) {
        std::snprintf(note, note_cap, "%s",
                      "베껴 올 탈것이 없습니다 (특수 탑승물을 하나 가지십시오)");
        return false;
    }
    // 대상: 드래곤·정비 슬롯이 가진 타입행 + 진짜 탈것. 반려 동물·용병은 탈것
    // 규칙이 0xFFFF 라 여기 못 들어온다. 같은 종이 여럿이어도 한 번만.
    for (const auto& e : list) {
        if (e.merc_row == kInfoNone || !locked(e.merc_row)) continue;
        const std::uintptr_t rec = char_rec(e.row);
        if (rec == 0) continue;
        if (rd16(r, rec + kCiVehicleInfo) == kInfoNone) continue;
        bool dup = false;
        for (int i = 0; i < p->n; ++i) {
            if (p->rec[i] == rec) { dup = true; break; }
        }
        if (dup || p->n >= kDisguiseMax) continue;
        p->rec[p->n] = rec;
        p->row[p->n] = static_cast<std::uint16_t>(e.row);
        ++p->n;
    }
    return true;
}

}  // namespace

// **잠긴 쪽을 먼저 본다.** 얹기가 메인 목록에 2·3·4 를 붙여 놔도 드래곤은 여전히
// 대상이고, 목록 밖의 반려 동물은 여전히 None 이다 - 2026-09-15 사고가 난 자리.
DisguiseRole disguise_role(const int* main_cats, int main_n,
                           const int* locked_cats, int locked_n, int merc) {
    for (int i = 0; i < locked_n; ++i) {
        if (locked_cats[i] == merc) return DisguiseRole::Target;
    }
    for (int i = 0; i < main_n; ++i) {
        if (main_cats[i] == merc) return DisguiseRole::DonorCat;
    }
    return DisguiseRole::None;
}

DisguiseState disguise_state(const mem::Reader& reader) {
    DisguiseState s;
    DisguisePlan p;
    if (!build_plan(reader, &p, s.note, sizeof s.note)) return s;
    s.ready = true;
    s.targets = p.n;
    s.donor_merc = p.donor_merc;
    s.donor_veh = p.donor_veh;
    s.donor_row = p.donor_row;
    for (int i = 0; i < p.n && i < kDisguiseMax; ++i) s.rows[i] = p.row[i];
    s.on = g_dis_bak_n > 0;
    return s;
}

bool disguise_apply(const mem::Reader& reader, bool on) {
    if (!on) {
        disguise_teardown();
        return true;
    }
    if (g_dis_bak_n > 0) return true;

    DisguisePlan p;
    char note[96] = {};
    if (!build_plan(reader, &p, note, sizeof note) || p.n == 0) {
        log::warnf("휠 칸 바꾸기: 할 일이 없다 ({})", note);
        return false;
    }
    std::uintptr_t veh_recs = 0;
    int veh_n = 0;
    veh_table(reader, &veh_recs, &veh_n);
    // 기증자의 탈것 규칙 행. 장소 관련 칸을 여기서 베껴 온다.
    std::uintptr_t donor_vrec = 0;
    if (veh_recs != 0 && p.donor_veh >= 0 && p.donor_veh < veh_n) {
        donor_vrec = static_cast<std::uintptr_t>(
            rd64(reader, veh_recs + static_cast<std::uintptr_t>(p.donor_veh) * 8));
        if (donor_vrec < 0x10000) donor_vrec = 0;
    }

    log::infof("휠 칸 바꾸기: 기증자 종행 {} - 타입행 {} (탈것규칙 {} 은 안 베낀다)"
               " · 대상 {}개",
               p.donor_row, p.donor_merc, p.donor_veh, p.n);
    for (int i = 0; i < p.n && g_dis_bak_n < kDisguiseMax; ++i) {
        const std::uintptr_t rec = p.rec[i];
        const std::uint16_t om = rd16(reader, rec + kCiMercInfo);
        const std::uint16_t ov = rd16(reader, rec + kCiVehicleInfo);
        // **마지막 안전선.** 탈것 규칙이 없는 종(반려 동물·용병)은 어떤 이유로도
        // 안 건드린다. 2026-09-15 에 바뀐 열여섯은 전부 여기 걸렸을 것들이다.
        if (ov == kInfoNone) {
            log::warnf("휠 칸 바꾸기: 종행 {} 는 탈것이 아니다 - 건너뛴다",
                       p.row[i]);
            continue;
        }
        // **타입은 여기서 안 쓴다.** 월드에 나와 있는지에 따라 `disguise_tick`
        // 이 정한다 - 나와 있는 동안 타입을 바꿔 두면 개조 UI 같은 다른 계통이
        // 이 종을 특수 탑승물로 보고 엉뚱한 화면을 연다(사용자 실측 2026-09-15:
        // A.T.A.G. 개조 UI 자리에 말 UI 가 떴다).
        g_dis_bak[g_dis_bak_n] =
            DisguiseBak{rec, om, ov, p.row[i],
                        static_cast<std::uint16_t>(p.donor_merc), false};
        ++g_dis_bak_n;
        // **`_vehicleInfo` 는 건드리지 않는다.** 그 칸은 규칙이 아니라 *무엇이
        // 나오는가* 를 정한다 - 말의 18 을 씌웠더니 드래곤 자리에서 말이
        // 나왔다(사용자 실측 2026-09-15 17:23). 바꿀 것은 휠 칸뿐이다.
        //
        // 아래 둘은 **장소 검사를 느슨하게만** 하므로 월드 등장 여부와 무관하게
        // 한 번 걸어 두고 끌 때 되돌린다.
        //
        // **호출 지형 요구를 푼다.** 여기가 "호출할 수 없는 위치입니다" 의 진짜
        // 출처다 - A.T.A.G. 는 요구가 0개라 그대로 됐고(사용자 실측 2026-09-15
        // 17:46), 드래곤은 1개(복셀 3)만 허용해 거부됐다. 개수만 0 으로 둔다.
        const std::uint32_t vx = rd32(reader, rec + kCiSpawnVoxelCount);
        if (spawn_voxel_gated(vx) && g_dis_xbak_n < kDisguiseMax) {
            const std::uint32_t none = 0;
            if (mem::safe_write_bytes(rec + kCiSpawnVoxelCount, &none,
                                      sizeof none)) {
                g_dis_xbak[g_dis_xbak_n] = VoxBak{rec, vx};
                ++g_dis_xbak_n;
                log::infof("휠 칸 바꾸기: 종행 {} 호출 지형 요구 {}개 -> 0",
                           p.row[i], vx);
            }
        }
        // 자기 탈것 규칙에서 **장소에 관한 두 칸만** 기증자와 맞춘다. 규칙 행
        // 자체는 안 바꾼다(그건 무엇이 나오는지를 정한다).
        if (veh_recs == 0 || static_cast<int>(ov) >= veh_n) continue;
        const std::uintptr_t vrec = static_cast<std::uintptr_t>(
            rd64(reader, veh_recs + static_cast<std::uintptr_t>(ov) * 8));
        if (vrec < 0x10000) continue;
        // (1) 지면 거리 검사. 드래곤 30 · 와이번 0.
        const float d = rdf(reader, vrec + kViGroundDist);
        if (vehicle_place_gated(d) && g_dis_vbak_n < kDisguiseMax) {
            const float zero = 0.0f;
            if (mem::safe_write_bytes(vrec + kViGroundDist, &zero,
                                      sizeof zero)) {
                g_dis_vbak[g_dis_vbak_n] = VehBak{vrec, d};
                ++g_dis_vbak_n;
                log::infof("휠 칸 바꾸기: 탈것규칙 {} 지면 거리 {} -> 0", ov, d);
            }
        }
        // (2) **탈출로 그룹.** 되는 둘과 안 되는 하나가 여기서 갈렸다 - 실측
        // 2026-09-15: A.T.A.G.(행 2) 5 · 와이번(행 4) 5 · **드래곤(행 3) 2**.
        // 지형 요구와 지면 거리를 다 풀어도 드래곤만 "호출할 수 없는 위치" 가
        // 남았고, 그때 남아 있던 유일한 깨끗한 차이가 이 칸이다. 기증자 값으로
        // 맞춘다 - 번호를 박지 않는다.
        if (donor_vrec == 0 || g_dis_rbak_n >= kDisguiseMax) continue;
        const std::uint8_t want = rd8(reader, donor_vrec + kViEscapeRoad);
        const std::uint8_t mine = rd8(reader, vrec + kViEscapeRoad);
        if (!escape_road_differs(mine, want)) continue;
        if (mem::safe_write_bytes(vrec + kViEscapeRoad, &want, sizeof want)) {
            g_dis_rbak[g_dis_rbak_n] = RoadBak{vrec, mine};
            ++g_dis_rbak_n;
            log::infof("휠 칸 바꾸기: 탈것규칙 {} 탈출로 그룹 {} -> {}", ov, mine,
                       want);
        }
    }
    if (g_dis_bak_n == 0) {
        disguise_teardown();
        return false;
    }
    // **휠 칸 등록 채우기에 대상 종행을 알려 준다.** 종을 못 박지 않으면 빈
    // 말 칸까지 채워 유령 동반자가 생긴다(가진 말을 다 올려놓진 않는다).
    {
        std::uint16_t rows[16];
        int n = 0;
        for (int i = 0; i < g_dis_bak_n && n < 16; ++i) {
            if (g_dis_bak[i].rec != 0) rows[n++] = g_dis_bak[i].row;
        }
        wheel_fill_set_rows(rows, n);
    }
    disguise_tick(reader);   // 지금 상태에 맞춰 타입을 정한다
    return true;
}

// 얹기가 동반자 칸까지 옮길지. 기본은 켬(예전 동작 그대로).
bool g_dis_swap_slot = true;

void disguise_set_swap_slot(bool on) { g_dis_swap_slot = on; }

bool disguise_swap_slot() { return g_dis_swap_slot; }

// 월드에 나와 있는 동안은 **제 타입으로 돌려 놓는다.**
//
// 휠에서 부를 때만 특수 탑승물 타입이 필요하다. 나온 뒤에도 바꿔 둔 채로 있으면
// 다른 계통이 이 종을 특수 탑승물로 본다 - A.T.A.G. 개조 UI 자리에 말 UI 가
// 떴다(사용자 실측 2026-09-15). 그래서 등장/퇴장에 맞춰 오간다.
//
// 값이 바뀔 때만 쓴다. 호출자가 몇 초에 한 번씩 부른다.
void disguise_tick(const mem::Reader& reader) {
    if (g_dis_bak_n == 0) return;
    const mem::Rtti* rtti = clan_rtti();
    if (rtti == nullptr) return;
    std::uintptr_t clan = 0;
    if (!clan_component_fast(reader, *rtti, false, &clan)) return;
    std::vector<ClanEntry> list;
    if (!read_clan_roster(reader, clan, &list)) return;
    for (int i = 0; i < g_dis_bak_n; ++i) {
        DisguiseBak& b = g_dis_bak[i];
        if (b.rec == 0) continue;
        // 같은 종이 명부에 여럿일 수 있다(블랙스타가 둘이다). 하나라도 월드에
        // 나와 있으면 나와 있는 것으로 친다.
        int out = 0;
        for (const auto& e : list) {
            if (e.row == b.row && e.handle != 0) ++out;
        }
        // 칸 옮기기를 끄면 장소 제한만 풀고 타입은 제자리에 둔다. 등록을
        // 채운 뒤에는 제 칸에서 그대로 불리므로 옮길 이유가 없다.
        const bool want = g_dis_swap_slot && disguise_wants_swap(out);
        if (want == b.applied) continue;
        const std::uint16_t v = want ? b.donor : b.merc;
        if (!wr16(b.rec + kCiMercInfo, v)) continue;
        b.applied = want;
        log::infof("휠 칸 바꾸기: 종행 {} {} - 타입 {} (월드에 {}개)", b.row,
                   want ? "휠용으로 바꿈" : "제 타입으로 되돌림", v, out);
    }
}

void disguise_teardown() {
    wheel_fill_set_rows(nullptr, 0);   // 대상이 없으면 아무것도 안 채운다
    if (g_dis_bak_n == 0 && g_dis_vbak_n == 0 && g_dis_xbak_n == 0 &&
        g_dis_rbak_n == 0) {
        return;
    }
    int back = 0;
    for (int i = 0; i < g_dis_bak_n; ++i) {
        const DisguiseBak& b = g_dis_bak[i];
        if (b.rec == 0) continue;
        // `_vehicleInfo` 는 애초에 안 썼으니 되돌릴 것도 없다.
        if (wr16(b.rec + kCiMercInfo, b.merc)) ++back;
    }
    int xback = 0;
    for (int i = 0; i < g_dis_xbak_n; ++i) {
        const VoxBak& b = g_dis_xbak[i];
        if (b.rec == 0) continue;
        if (mem::safe_write_bytes(b.rec + kCiSpawnVoxelCount, &b.count,
                                  sizeof b.count)) {
            ++xback;
        }
    }
    int vback = 0;
    for (int i = 0; i < g_dis_vbak_n; ++i) {
        const VehBak& b = g_dis_vbak[i];
        if (b.rec == 0) continue;
        if (mem::safe_write_bytes(b.rec + kViGroundDist, &b.dist,
                                  sizeof b.dist)) {
            ++vback;
        }
    }
    int rback = 0;
    for (int i = 0; i < g_dis_rbak_n; ++i) {
        const RoadBak& b = g_dis_rbak[i];
        if (b.rec == 0) continue;
        if (mem::safe_write_bytes(b.rec + kViEscapeRoad, &b.group,
                                  sizeof b.group)) {
            ++rback;
        }
    }
    log::infof("휠 칸 바꾸기: {}개 되돌렸다 (지형 요구 {} · 지면 거리 {} ·"
               " 탈출로 {})",
               back, xback, vback, rback);
    g_dis_bak_n = 0;
    g_dis_vbak_n = 0;
    g_dis_xbak_n = 0;
    g_dis_rbak_n = 0;
}

void call_place_teardown() {
    if (g_veh_bak_n == 0) return;
    int back = 0;
    for (int i = 0; i < g_veh_bak_n; ++i) {
        const VehBak& b = g_veh_bak[i];
        if (b.rec == 0) continue;
        if (mem::safe_write_bytes(b.rec + kViGroundDist, &b.dist, sizeof b.dist)) {
            ++back;
        }
    }
    log::infof("호출 장소 제한: {}개 되돌렸다", back);
    g_veh_bak_n = 0;
}

void mount_timer_teardown() {
    if (g_mount_bak_n == 0) return;
    int back = 0;
    for (int i = 0; i < g_mount_bak_n; ++i) {
        const MountBak& b = g_mount_bak[i];
        if (b.rec == 0) continue;
        if (wr64(b.rec + kCiCoolTime, b.cool)) ++back;
        if (b.dur > 0) wr64(b.rec + kCiSpawnDuration, b.dur);
    }
    log::infof("탈것 타이머: {}개 되돌렸다", back);
    g_mount_bak_n = 0;
}

}  // namespace cdtb::game

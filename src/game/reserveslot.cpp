#include "game/reserveslot.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/log.h"
#include "game/knowledge.h"
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

inline constexpr std::uintptr_t kCondNameTableRva = 0x0584FA10;   // char* x466
inline constexpr int kCondNameCount = 466;
inline constexpr std::uintptr_t kEmptyStrRva = 0x0692E4C0;
inline constexpr std::uintptr_t kGpvMgrGlobalRva = 0x06C32880;
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
        const Mgr kn = read_mgr(reader, 0x06C2E2D8, "지식 매니저");
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
    const Mgr kn = read_mgr(reader, 0x06C2E2D8, "지식 매니저", true);
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

bool wheel_restore() {
    if (!g_wheel_bak.held) return true;
    const WheelBackup b = g_wheel_bak;
    // **개수를 먼저 줄인다.** 포인터를 먼저 되돌리면 그 사이에 게임이 옛 배열을
    // 늘어난 개수로 읽어 배열 밖을 본다.
    bool ok = wr32(b.info + kRsMercCount, b.count);
    if (b.repointed) {
        ok = wr64(b.info + kRsMercList, b.ptr) && ok;
        ok = wr32(b.info + kRsMercCap, b.cap) && ok;
    }
    g_wheel_bak = WheelBackup{};
    log::infof("탈것 휠: 되돌렸다 (개수 {}{})", b.count,
               b.repointed ? ", 포인터도" : "");
    return ok;
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
    if (!on) return wheel_restore();
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
        wheel_restore();
        log::warnf("탈것 휠: 쓰기에 실패해 되돌렸다");
        return false;
    }
    log::infof("탈것 휠: 메인 슬롯 허용 {}개 -> {}개 ({})", s.main_slot.count,
               s.want_count, g_wheel_bak.repointed ? "새 배열" : "제자리");
    return true;
}

void wheel_teardown() { wheel_restore(); }

}  // namespace cdtb::game

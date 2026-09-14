#include "game/reserveslot.h"

#include <cstring>
#include <string>
#include <vector>

#include "core/log.h"

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

Mgr read_mgr(const mem::Reader& r, std::uintptr_t rva, const char* what) {
    Mgr m;
    const std::uintptr_t base = r.module_base();
    if (base == 0 || rva + 8 > r.module_size()) return m;
    const std::uintptr_t obj = static_cast<std::uintptr_t>(rd64(r, base + rva));
    if (obj < 0x10000) {
        log::warnf("  {} 전역이 비었다(0x{:X})", what, obj);
        return m;
    }
    const int cnt = static_cast<int>(rd32(r, obj + kMgrCount));
    const std::uintptr_t arr =
        static_cast<std::uintptr_t>(rd64(r, obj + kMgrArray));
    log::infof("  {} 0x{:X} 개수 {} 배열 0x{:X}", what, obj, cnt, arr);
    if (cnt <= 0 || cnt > kSlotMaxCount || arr < 0x10000) {
        log::warnf("  {} 가 말이 안 된다 - 안 따라간다", what);
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
    log::infof("  {} info 0x{:X}: key {} · blocked {} · type {} · usingType {}",
               label, info, rd32(r, info + kRsKey), rd8(r, info + kRsBlocked),
               rd8(r, info + kRsType), rd8(r, info + kRsUsingType));
    log::infof("    flags +0xAC..AF: {} {} {} {}", rd8(r, info + kRsFlags),
               rd8(r, info + kRsFlags + 1), rd8(r, info + kRsFlags + 2),
               rd8(r, info + kRsFlags + 3));
    log::infof("    fill 0x{:X} x{} · target 0x{:X} x{}",
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
    if (slot.object != 0) {
        dump_slot_info(reader, cond, info_at(reader, slot, elem), "원소");
        // 대조군 - 채워져 있을 법한 슬롯 하나를 나란히 본다.
        dump_slot_info(reader, cond, info_at(reader, slot, veh), "탈것(대조군)");
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

}  // namespace cdtb::game

#include "game/mountslot.h"

#include <cstdio>

#include "core/log.h"
#include "core/write_log.h"
#include "game/roster.h"
#include "game/wheelfill.h"
#include "mem/safe_read.h"

namespace cdtb::game {
namespace {

// 걸어 볼 카테고리. 실측으로 이 여섯에 항목이 있었다(2026-09-18).
// 모르는 번호가 나와도 표가 알려 주므로 넉넉히 훑는다.
constexpr std::uint32_t kCats[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};

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
std::int32_t rdi32(const mem::Reader& r, std::uintptr_t a) {
    std::int32_t v = 0;
    return r.read_value(a, &v) ? v : 0;
}

bool vp(std::uintptr_t p) { return p >= 0x10000; }

// 플레이어에서 등록 표의 주인까지. 훅이 인자로 받던 그 객체다.
std::uintptr_t slot_owner(const mem::Reader& r, std::uintptr_t ch) {
    if (!vp(ch)) return 0;
    const auto holder = static_cast<std::uintptr_t>(rd64(r, ch + 0x68));
    if (!vp(holder)) return 0;
    const auto owner =
        static_cast<std::uintptr_t>(rd64(r, holder + kSlotOwnerOff));
    return vp(owner) ? owner : 0;
}

// 카테고리 하나의 항목 배열. `dragondiag::find_category_list` 와 같은 걷기다.
bool category_list(const mem::Reader& r, std::uintptr_t owner,
                   std::uint32_t cat, std::uintptr_t* out_arr,
                   std::uint32_t* out_cnt) {
    const std::uintptr_t map = owner + kSlotMapOff;
    const std::uint32_t buckets = rd32(r, map + 0x30);
    const std::uint32_t live = rd32(r, map + 0x34);
    if (buckets == 0 || buckets > 4096 || live == 0) return false;
    const auto table = static_cast<std::uintptr_t>(rd64(r, map + 0x40));
    const auto recs = static_cast<std::uintptr_t>(rd64(r, map + 0x48));
    if (!vp(table) || !vp(recs)) return false;

    const std::uintptr_t bucket = table + (cat % buckets) * 0x100;
    const std::uint32_t n = rd32(r, bucket);
    for (std::uint32_t i = 0; i < n && i < 30; ++i) {
        if (rd32(r, bucket + i * 8 + 8) != cat) continue;
        const std::uint32_t idx = rd32(r, bucket + i * 8 + 12);
        const auto rec = static_cast<std::uintptr_t>(rd64(r, recs + idx * 8));
        if (!vp(rec)) return false;
        const auto arr = static_cast<std::uintptr_t>(rd64(r, rec + 8));
        const std::uint32_t cnt = rd32(r, rec + 0x10);
        if (!vp(arr) || cnt == 0 || cnt > kSlotMaxEntries) return false;
        *out_arr = arr;
        *out_cnt = cnt;
        return true;
    }
    return false;
}

std::uintptr_t entry_at(const mem::Reader& r, std::uintptr_t arr, int nth) {
    const auto e = static_cast<std::uintptr_t>(
        rd64(r, arr + static_cast<std::uintptr_t>(nth) * 8));
    return vp(e) ? e : 0;
}

}  // namespace

// --------------------------------------------------------- 순수 부분

bool slot_registered(std::uint16_t slot) { return slot != kSlotNone; }

std::string slot_category_name(std::uint32_t cat) {
    switch (cat) {
        case 1: return "말";
        case 2: return "드래곤";
        case 3: return "A.T.A.G.";
        case 5: return "특수 탑승물";
        case 7: return "마차";
        case 9: return "동물";
        default: break;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "카테고리 %u", cat);
    return buf;
}

// --------------------------------------------------------- 읽기

SlotTable mount_slots(const mem::Reader& reader,
                      std::uintptr_t player_char) {
    SlotTable t;
    const std::uintptr_t owner = slot_owner(reader, player_char);
    if (owner == 0) {
        std::snprintf(t.note, sizeof t.note, "%s",
                      "등록 표를 아직 못 잡았습니다 (월드 밖입니까?)");
        return t;
    }
    t.owner = owner;
    for (const std::uint32_t cat : kCats) {
        std::uintptr_t arr = 0;
        std::uint32_t cnt = 0;
        if (!category_list(reader, owner, cat, &arr, &cnt)) continue;
        SlotCategory c;
        c.cat = cat;
        for (std::uint32_t k = 0; k < cnt; ++k) {
            const std::uintptr_t e = entry_at(reader, arr, static_cast<int>(k));
            if (e == 0) break;
            SlotEntry se;
            se.nth = static_cast<int>(k);
            se.species = rd16(reader, e + kSlotEntrySpecies);
            se.slot = rd16(reader, e + kSlotEntrySlot);
            se.hp = rdi32(reader, e + kSlotEntryHp);
            se.grow = rdi32(reader, e + kSlotEntryGrow);
            if (const RosterEntry* re = character_by_row(se.species)) {
                se.name = re->display();
            }
            c.entries.push_back(se);
        }
        if (!c.entries.empty()) t.cats.push_back(c);
    }
    t.ready = !t.cats.empty();
    if (!t.ready) {
        std::snprintf(t.note, sizeof t.note, "%s",
                      "주인은 잡았는데 카테고리가 하나도 안 걸립니다");
    }
    return t;
}

// --------------------------------------------------------- 쓰기

bool mount_slot_set(const mem::Reader& reader, std::uintptr_t player_char,
                    std::uint32_t cat, int nth, std::uint16_t slot) {
    // **주소를 들고 오지 않는다** - 여기서 표를 다시 걸어 지금 주소를 얻는다.
    const std::uintptr_t owner = slot_owner(reader, player_char);
    if (owner == 0) return false;
    std::uintptr_t arr = 0;
    std::uint32_t cnt = 0;
    if (!category_list(reader, owner, cat, &arr, &cnt)) return false;
    if (nth < 0 || static_cast<std::uint32_t>(nth) >= cnt) return false;
    const std::uintptr_t e = entry_at(reader, arr, nth);
    if (e == 0) return false;

    const std::uint16_t before = rd16(reader, e + kSlotEntrySlot);
    if (before == slot) return true;   // 이미 그 상태다
    if (!mem::safe_write_bytes(e + kSlotEntrySlot, &slot, sizeof slot)) {
        log::warnf("휠 등록: 카테고리 {} [{}] 에 못 썼다", cat, nth);
        return false;
    }
    char from[16], to[16];
    std::snprintf(from, sizeof from, "%u", before);
    std::snprintf(to, sizeof to, "%u", slot);
    log_write("휠 등록", e + kSlotEntrySlot, from, to);
    log::infof("휠 등록: {} [{}] 칸 {} -> {}", slot_category_name(cat), nth,
               before, slot);
    return true;
}

int mount_slot_autofix(const mem::Reader& reader,
                       std::uintptr_t player_char, std::uint32_t cat) {
    const SlotTable t = mount_slots(reader, player_char);
    if (!t.ready) return 0;
    const SlotCategory* c = nullptr;
    for (const SlotCategory& x : t.cats) {
        if (x.cat == cat) c = &x;
    }
    if (c == nullptr) return 0;

    int changed = 0;
    // 종마다 한 벌씩 본다. 같은 종을 두 번 처리하지 않게 앞에서 본 종은 건너뛴다.
    for (std::size_t i = 0; i < c->entries.size(); ++i) {
        const std::uint16_t sp = c->entries[i].species;
        bool seen = false;
        for (std::size_t j = 0; j < i; ++j) {
            if (c->entries[j].species == sp) seen = true;
        }
        if (seen) continue;

        // 그 종이 지금 쓰고 있는 칸. 아무도 안 올라가 있으면 **건드리지 않는다** -
        // 어느 칸에 올려야 하는지 모른다.
        std::uint16_t want = kSlotNone;
        for (const SlotEntry& e : c->entries) {
            if (e.species == sp && slot_registered(e.slot)) want = e.slot;
        }
        if (!slot_registered(want)) continue;

        // 제일 나은 항목 하나를 고른다(판정은 wheelfill 의 것을 그대로 쓴다).
        const SlotEntry* best = nullptr;
        for (const SlotEntry& e : c->entries) {
            if (e.species != sp) continue;
            if (best == nullptr ||
                wheel_fill_better(e.hp, e.grow, best->hp, best->grow)) {
                best = &e;
            }
        }
        if (best == nullptr) continue;

        for (const SlotEntry& e : c->entries) {
            if (e.species != sp) continue;
            const std::uint16_t target = (e.nth == best->nth) ? want : kSlotNone;
            if (e.slot == target) continue;
            if (mount_slot_set(reader, player_char, cat, e.nth, target)) ++changed;
        }
    }
    if (changed > 0) {
        log::infof("휠 등록 정리: {} 에서 {}개를 바꿨다",
                   slot_category_name(cat), changed);
    }
    return changed;
}

}  // namespace cdtb::game

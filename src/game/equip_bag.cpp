#include "game/equip_bag.h"

#include "game/inventory.h"
#include "game/items.h"   // kSocketLocked

namespace cdtb::game {
namespace {

bool vp(std::uintptr_t p) {
    return p > 0x100000000ULL && p < 0x7FFFFFFFFFFFULL;
}

}  // namespace

void read_level_and_sockets(const mem::Reader& reader, std::uintptr_t record,
                            WornPiece* w) {
    if (w == nullptr) return;
    std::uint16_t temper = 0;
    std::uint16_t sharpness = 0;
    reader.read_value(record + 0x0A, &temper);
    reader.read_value(record + 0x58, &sharpness);
    w->temper = temper;
    w->sharpness = sharpness;
    w->unlocked = 0;
    for (auto& s : w->sockets) s = WornSocket{};
    std::uint64_t sp = 0;
    if (!reader.read_value(record + 0x60, &sp) || !vp(static_cast<std::uintptr_t>(sp))) {
        return;
    }
    for (int k = 0; k < 5; ++k) {
        const std::uintptr_t r =
            static_cast<std::uintptr_t>(sp) + static_cast<std::uintptr_t>(k) * 6;
        WornSocket s;
        if (!reader.read_value(r + 0, &s.gem) || !reader.read_value(r + 2, &s.marker) ||
            !reader.read_value(r + 4, &s.index)) {
            return;
        }
        w->sockets[k] = s;
        if (s.index != kSocketLocked && s.index == k) ++w->unlocked;
    }
}

std::unordered_map<std::uint64_t, std::uintptr_t> bag_index(
    const mem::Reader& reader, std::uintptr_t comp) {
    std::unordered_map<std::uint64_t, std::uintptr_t> out;
    if (comp == 0) return out;
    std::vector<InventoryContainer> conts;
    if (!read_inventory_containers(reader, comp, &conts)) return out;
    std::vector<InventoryRecord> recs;
    for (const auto& c : conts) {
        if (!read_inventory_records(reader, c, &recs)) continue;
        for (const auto& r : recs) out.emplace(r.instance_id, r.address);
    }
    return out;
}

void apply_bag_truth(const mem::Reader& reader,
                     const std::unordered_map<std::uint64_t, std::uintptr_t>& bag,
                     std::vector<WornPiece>* pieces) {
    if (pieces == nullptr || bag.empty()) return;
    for (auto& w : *pieces) {
        const auto it = bag.find(w.instance);
        if (it == bag.end()) continue;
        read_level_and_sockets(reader, it->second, &w);
        w.in_bag = true;
    }
}

}  // namespace cdtb::game

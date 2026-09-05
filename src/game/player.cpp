#include "game/player.h"

namespace cdtb::game {
namespace {

constexpr const char* kEquipClass = "EquipSlotActorComponent";

bool vp(std::uintptr_t p) {
    return p > 0x100000000ULL && p < 0x7FFFFFFFFFFFULL;
}

std::int32_t ri32(const mem::Reader& r, std::uintptr_t a) {
    std::int32_t v = 0;
    return r.read_value(a, &v) ? v : 0;
}

std::uintptr_t rptr(const mem::Reader& r, std::uintptr_t a) {
    std::uint64_t v = 0;
    return r.read_value(a, &v) ? static_cast<std::uintptr_t>(v) : 0;
}

}  // namespace

bool stat_pair_ok(const mem::Reader& reader, std::uintptr_t r, std::size_t cur,
                  std::size_t max) {
    if (!vp(r)) return false;
    std::int32_t mx = 0;
    if (!reader.read_value(r + max, &mx)) return false;
    if (mx <= 0 || mx > 50000000) return false;
    std::int32_t cu = 0;
    if (!reader.read_value(r + cur, &cu)) return false;
    return cu >= 0 && cu <= mx;
}

// 자원 풀은 0x90 그리드에 늘어선다(entry N: cur +0x08+N*0x90, max +0x18+N*0x90).
// 체력=entry0, 스태미나=entry12(0x6C8), 정신력=entry13(0x758). 진짜 상태
// 블록은 이 그리드에 유효한 풀이 여러 개 연달아 있다. 잡음 구조는 두엇뿐.
int stat_grid_run(const mem::Reader& reader, std::uintptr_t r) {
    int n = 0;
    for (int i = 0; i < 16; ++i) {
        const std::size_t cur = 0x08 + static_cast<std::size_t>(i) * 0x90;
        const std::size_t max = 0x18 + static_cast<std::size_t>(i) * 0x90;
        if (stat_pair_ok(reader, r, cur, max)) ++n;
    }
    return n;
}

bool looks_like_stat_block(const mem::Reader& reader, std::uintptr_t r) {
    // 신뢰도 높은 스태미나+정신력 쌍(CT 확정)으로 1차 판별. HP 오프셋은
    // 빌드별로 갈릴 수 있어 여기선 요구하지 않는다(probe 로 확인 중).
    return stat_pair_ok(reader, r, kStaCur, kStaMax) &&
           stat_pair_ok(reader, r, kSpiCur, kSpiMax);
}

std::uintptr_t stat_block_in_actor(const mem::Reader& reader,
                                   std::uintptr_t actor) {
    if (!vp(actor)) return 0;
    // 액터가 직접 가리키는 포인터를 훑는다(한 단계). 스탯 블록은
    // 액터의 컴포넌트 중 하나다.
    for (std::size_t off = 0; off <= 0x800; off += 8) {
        const std::uintptr_t p = rptr(reader, actor + off);
        if (!vp(p)) continue;
        if (looks_like_stat_block(reader, p)) return p;
    }
    return 0;
}

bool read_stat_block(const mem::Reader& reader, StatBlock* b) {
    if (b == nullptr || !vp(b->addr)) return false;
    b->health.cur = ri32(reader, b->addr + kHpCur);
    b->health.max = ri32(reader, b->addr + kHpMax);
    b->stamina.cur = ri32(reader, b->addr + kStaCur);
    b->stamina.max = ri32(reader, b->addr + kStaMax);
    b->spirit.cur = ri32(reader, b->addr + kSpiCur);
    b->spirit.max = ri32(reader, b->addr + kSpiMax);
    return true;
}

int find_stat_blocks(const mem::Rtti& rtti, const mem::Reader& reader,
                     std::vector<StatBlock>* out, int max_hits) {
    if (out == nullptr) return 0;
    out->clear();
    const auto objs = rtti.find_objects(kEquipClass, 128);
    std::vector<std::uintptr_t> seen_actors;
    std::vector<std::uintptr_t> seen_blocks;
    for (const auto& o : objs) {
        const std::uintptr_t actor = rptr(reader, o.address + 0x08);
        if (!vp(actor)) continue;
        bool dup = false;
        for (const auto a : seen_actors) {
            if (a == actor) {
                dup = true;
                break;
            }
        }
        if (dup) continue;
        seen_actors.push_back(actor);

        const std::uintptr_t blk = stat_block_in_actor(reader, actor);
        if (blk == 0) continue;
        bool bdup = false;
        for (const auto x : seen_blocks) {
            if (x == blk) {
                bdup = true;
                break;
            }
        }
        if (bdup) continue;
        seen_blocks.push_back(blk);

        StatBlock b;
        b.addr = blk;
        b.actor = actor;
        read_stat_block(reader, &b);
        out->push_back(b);
        if (static_cast<int>(out->size()) >= max_hits) break;
    }
    return static_cast<int>(out->size());
}

}  // namespace cdtb::game

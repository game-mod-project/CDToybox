#include "game/spec_heal.h"

#include <windows.h>

#include <cstdint>
#include <vector>

#include "core/log.h"
#include "game/inventory.h"

namespace cdtb::game {
namespace {

// {순번(아이템표 인덱스), 게이지 최대(표+0x2C8), 최대 내구도(표+0x400)}.
// 빌드 2.00.01(2658) 아이템표 덤프에서 추출한 특수기능(+0x58≠0) 32종.
struct SpecItem {
    std::uint32_t seq;
    std::uint32_t gauge;
    std::uint16_t maxend;
};
constexpr SpecItem kSpec[] = {
    {3658, 10000, 65535},   {3659, 1800000, 65535}, {3660, 60000, 65535},
    {3661, 1800000, 65535}, {3662, 1000, 65535},    {3663, 0, 65535},
    {3713, 3000, 65535},    {5715, 0, 65535},       {5741, 60000, 65535},
    {5742, 10000, 65535},   {5743, 0, 65535},       {5745, 0, 65535},
    {5746, 1000, 65535},    {5749, 1000, 65535},    {5753, 10000, 100},
    {5754, 1000, 65535},    {5778, 0, 65535},       {5821, 10000, 100},
    {5823, 1000, 100},      {5824, 30000, 100},     {5828, 0, 65535},
    {5829, 0, 65535},       {5830, 10000, 65535},   {5831, 0, 65535},
    {6197, 10000, 65535},   {6201, 10000, 65535},   {6202, 15000, 65535},
    {6203, 0, 65535},       {6206, 10000, 65535},   {6207, 10000, 65535},
    {6211, 1800000, 65535}, {6597, 600000, 65535},
};

// 레코드 오프셋(inventory.cpp 실측). +0x2C 는 "특수 상태 채워짐" 표시로 쓴다
// (저장/로드본·힐 후 =1, 갓 지급 =0).
constexpr std::size_t kRecFlag = 0x2C;      // u32
constexpr std::size_t kRecGauge = 0x30;     // u32 게이지 최대(분모)
constexpr std::size_t kRecEndur = 0x40;     // u16 현재 내구도

bool wr32(std::uintptr_t a, std::uint32_t v) {
    __try {
        *reinterpret_cast<volatile std::uint32_t*>(a) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool wr16(std::uintptr_t a, std::uint16_t v) {
    __try {
        *reinterpret_cast<volatile std::uint16_t*>(a) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

const SpecItem* find_spec(std::uint32_t seq) {
    for (const auto& s : kSpec) {
        if (s.seq == seq) return &s;
    }
    return nullptr;
}

}  // namespace

void heal_special_items(const mem::Reader& reader) {
    if (!inventory_ready()) return;
    const std::uintptr_t comp = inventory_component();
    std::vector<InventoryContainer> conts;
    if (!read_inventory_containers(reader, comp, &conts)) return;

    int healed = 0;
    for (const auto& c : conts) {
        std::vector<InventoryRecord> recs;
        if (!read_inventory_records(reader, c, &recs)) continue;
        for (const auto& rec : recs) {
            const SpecItem* s = find_spec(rec.index);
            if (s == nullptr) continue;
            // 이미 채워졌으면(저장/로드본 또는 이전 힐) 건너뛴다.
            std::uint32_t flag = 0;
            if (reader.read_value(rec.address + kRecFlag, &flag) && flag != 0) {
                continue;
            }
            // 아이템표 기준값으로 채운다. 게이지 없는 아이템(gauge==0)은
            // +0x30 을 건드리지 않는다(원래 0 이 정상).
            wr32(rec.address + kRecFlag, 1);
            if (s->gauge != 0) wr32(rec.address + kRecGauge, s->gauge);
            wr16(rec.address + kRecEndur, s->maxend);
            ++healed;
        }
    }
    if (healed > 0) {
        log::infof("특수아이템 표시 보정: {}개", healed);
    }
}

}  // namespace cdtb::game

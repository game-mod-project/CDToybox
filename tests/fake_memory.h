#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "mem/reader.h"

namespace cdtb::tests {

// 가짜 메모리. 주소 공간 두 덩어리(모듈 이미지, 힙)를 흉내낸다.
//
// mem::Reader 의 Range::begin 은 역참조 가능한 포인터가 아니라 원격
// 주소를 담는 자리다(RemoteReader 가 그렇게 쓴다). 여기서도 같게 둔다.
class FakeMemory : public cdtb::mem::Reader {
public:
    static constexpr std::uintptr_t kModuleBase = 0x140000000ull;
    static constexpr std::uintptr_t kHeapBase = 0x7FF00000000ull;

    std::vector<std::uint8_t> image;
    std::vector<std::uint8_t> heap;

    bool read(std::uintptr_t addr, void* out, std::size_t n) const override {
        if (addr >= kModuleBase && addr + n <= kModuleBase + image.size()) {
            std::memcpy(out, image.data() + (addr - kModuleBase), n);
            return true;
        }
        if (addr >= kHeapBase && addr + n <= kHeapBase + heap.size()) {
            std::memcpy(out, heap.data() + (addr - kHeapBase), n);
            return true;
        }
        return false;
    }
    std::uintptr_t module_base() const override { return kModuleBase; }
    std::size_t module_size() const override { return image.size(); }
    std::vector<cdtb::mem::Range> heap_regions() const override {
        return {cdtb::mem::Range{
            reinterpret_cast<const std::uint8_t*>(kHeapBase), heap.size()}};
    }

    // --- 힙 쓰기 도우미 (오프셋은 kHeapBase 기준) ---
    std::uintptr_t heap_addr(std::size_t off) const { return kHeapBase + off; }
    void put_u8(std::size_t off, std::uint8_t v) { heap.at(off) = v; }
    void put_u32(std::size_t off, std::uint32_t v) {
        std::memcpy(heap.data() + off, &v, 4);
    }
    void put_u64(std::size_t off, std::uint64_t v) {
        std::memcpy(heap.data() + off, &v, 8);
    }
    void put_str(std::size_t off, const char* s) {
        std::memcpy(heap.data() + off, s, std::strlen(s) + 1);
    }
};

}  // namespace cdtb::tests

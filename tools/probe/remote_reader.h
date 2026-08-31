#pragma once

#include "mem/reader.h"
#include "remote.h"

namespace cdtb::probe {

// Remote 를 mem::Reader 로 감싼다.
//
// 이렇게 하면 RTTI 탐색 로직(src/mem/rtti)을 모드와 probe가 같이
// 쓴다. 둘은 메모리 접근 방법만 다르다 - 모드는 자기 프로세스를
// 직접 읽고, probe는 ReadProcessMemory로 게임을 읽는다.
class RemoteReader : public mem::Reader {
public:
    explicit RemoteReader(const Remote& r) : r_(r) {}

    bool read(std::uintptr_t addr, void* out, std::size_t n) const override {
        return r_.read(addr, out, n);
    }
    std::uintptr_t module_base() const override { return r_.module_base(); }
    std::size_t module_size() const override { return r_.module_size(); }

    std::vector<mem::Range> heap_regions() const override {
        std::vector<mem::Range> out;
        for (const auto& reg : r_.regions()) {
            if (!reg.writable || reg.is_image) continue;
            out.push_back(mem::Range{
                reinterpret_cast<const std::uint8_t*>(reg.base), reg.size});
        }
        return out;
    }

private:
    const Remote& r_;
};

}  // namespace cdtb::probe

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "mem/scanner.h"

namespace cdtb::mem {

// 메모리 접근 추상화.
//
// 같은 RTTI 탐색 로직을 두 곳에서 쓴다. 모드는 자기 프로세스를
// 직접 읽고, 외부 분석 도구(cdtb_probe)는 ReadProcessMemory로
// 게임을 읽는다. 접근 방법만 다르고 나머지는 같으므로 여기서
// 갈라 놓는다.
class Reader {
public:
    virtual ~Reader() = default;

    // 실패하면 false. 부분 읽기는 실패로 친다.
    virtual bool read(std::uintptr_t addr, void* out,
                      std::size_t n) const = 0;

    // 주 실행 모듈(게임 exe).
    virtual std::uintptr_t module_base() const = 0;
    virtual std::size_t module_size() const = 0;

    // 객체가 놓이는 영역. 모듈 이미지는 제외한다.
    virtual std::vector<Range> heap_regions() const = 0;

    template <class T>
    bool read_value(std::uintptr_t addr, T* out) const {
        return read(addr, out, sizeof(T));
    }
};

// 현재 프로세스를 읽는 구현. 모드 안에서 쓴다.
class LocalReader : public Reader {
public:
    bool read(std::uintptr_t addr, void* out, std::size_t n) const override;
    std::uintptr_t module_base() const override;
    std::size_t module_size() const override;
    std::vector<Range> heap_regions() const override;
};

}  // namespace cdtb::mem

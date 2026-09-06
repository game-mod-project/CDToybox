#pragma once

#include <cstddef>
#include <cstdint>

namespace cdtb::mem {

// 아래 함수들은 전부 SEH로 감싸여 있어, 잘못된 주소를 받아도
// 크래시하지 않고 false를 반환한다.
//
// MSVC는 소멸자를 가진 C++ 객체가 있는 함수에서 __try를 허용하지
// 않으므로(C2712), 이 파일의 구현에는 그런 객체를 두지 않는다.
// SEH를 쓰는 함수를 여기 모아 격리하면 나머지 코드가 자유로워진다.

bool safe_read_float(std::uintptr_t addr, float* out);
bool safe_read_bytes(std::uintptr_t addr, void* out, std::size_t n);
bool safe_write_float(std::uintptr_t addr, float value);
bool safe_write_bytes(std::uintptr_t addr, const void* src, std::size_t n);

// [base, base+size) 를 4바이트 정렬로 훑어 |v - target| <= eps 인
// 주소를 out에 채운다. cap에 도달하면 멈춘다.
// 영역이 도중에 해제되면 false를 반환한다(그때까지의 결과는 유효).
bool scan_region_floats(const std::uint8_t* base, std::size_t size,
                        float target, float eps, std::uintptr_t* out,
                        std::size_t cap, std::size_t* count);

}  // namespace cdtb::mem

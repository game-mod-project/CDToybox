#include "mem/safe_read.h"

#include <windows.h>

#include <cmath>
#include <cstring>

namespace cdtb::mem {

bool safe_read_float(std::uintptr_t addr, float* out) {
    if (addr == 0 || out == nullptr) return false;
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(addr), sizeof(float));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool safe_read_bytes(std::uintptr_t addr, void* out, std::size_t n) {
    if (addr == 0 || out == nullptr || n == 0) return false;
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(addr), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool safe_write_float(std::uintptr_t addr, float value) {
    if (addr == 0) return false;
    __try {
        std::memcpy(reinterpret_cast<void*>(addr), &value, sizeof(float));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool scan_region_floats(const std::uint8_t* base, std::size_t size,
                        float target, float eps, std::uintptr_t* out,
                        std::size_t cap, std::size_t* count) {
    if (base == nullptr || out == nullptr || count == nullptr) return false;
    __try {
        // 4바이트 정렬만 본다. 카메라 좌표와 FOV는 정렬된 float이고,
        // 정렬을 가정하면 후보 수와 스캔 시간이 모두 1/4로 준다.
        for (std::size_t i = 0; i + sizeof(float) <= size; i += 4) {
            float v;
            std::memcpy(&v, base + i, sizeof(float));
            if (std::fabs(v - target) <= eps) {
                if (*count >= cap) return true;
                out[(*count)++] = reinterpret_cast<std::uintptr_t>(base + i);
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace cdtb::mem

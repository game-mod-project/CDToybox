#include "game/camera.h"

#include "mem/safe_read.h"

namespace cdtb::game {
namespace {

std::uintptr_t g_base = 0;
CameraOffsets g_off;

bool read_at(int off, float* out) {
    if (off < 0 || g_base == 0) return false;
    return mem::safe_read_float(g_base + static_cast<std::uintptr_t>(off),
                                out);
}

}  // namespace

void set_base(std::uintptr_t addr) { g_base = addr; }
std::uintptr_t base() { return g_base; }

void set_offsets(const CameraOffsets& o) { g_off = o; }
CameraOffsets offsets() { return g_off; }

bool read_view(CameraView* out) {
    if (out == nullptr || g_base == 0) return false;
    read_at(g_off.pos_x, &out->pos[0]);
    read_at(g_off.pos_y, &out->pos[1]);
    read_at(g_off.pos_z, &out->pos[2]);
    read_at(g_off.rot_pitch, &out->rot[0]);
    read_at(g_off.rot_yaw, &out->rot[1]);
    read_at(g_off.rot_roll, &out->rot[2]);
    read_at(g_off.fov, &out->fov);
    return true;
}

const char* fov_write_blocker() {
    if (g_base == 0) {
        return "베이스 주소가 설정되지 않았습니다. "
               "메모리 스캔에서 후보를 고정한 뒤 '고정 주소 사용'을 "
               "누르거나, 주소를 직접 입력하세요.";
    }
    if (g_off.fov < 0) {
        return "fov 오프셋이 -1(미확정)입니다. "
               "베이스가 곧 FOV 주소라면 0을 넣으세요.";
    }
    return nullptr;
}

bool write_fov(float value) {
    if (fov_write_blocker() != nullptr) return false;
    return mem::safe_write_float(
        g_base + static_cast<std::uintptr_t>(g_off.fov), value);
}

}  // namespace cdtb::game

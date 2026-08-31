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

bool write_fov(float value) {
    if (g_base == 0 || g_off.fov < 0) return false;
    return mem::safe_write_float(
        g_base + static_cast<std::uintptr_t>(g_off.fov), value);
}

}  // namespace cdtb::game

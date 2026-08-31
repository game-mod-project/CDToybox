#pragma once

#include <cstdint>

namespace cdtb::game {

// 바이트 오프셋. -1은 아직 확정되지 않았다는 뜻이다.
//
// 값을 런타임에 바꿀 수 있게 두는 것이 핵심이다. 역공학은
// 추측과 확인의 반복인데, 오프셋이 코드에 박혀 있으면 추측마다
// 빌드·배포·게임 재시작이 필요하다.
struct CameraOffsets {
    int pos_x = -1;
    int pos_y = -1;
    int pos_z = -1;
    int rot_pitch = -1;
    int rot_yaw = -1;
    int rot_roll = -1;
    int fov = -1;
};

struct CameraView {
    float pos[3]{};
    float rot[3]{};
    float fov = 0.0f;
};

void set_base(std::uintptr_t addr);
std::uintptr_t base();

void set_offsets(const CameraOffsets& o);
CameraOffsets offsets();

// 확정된 오프셋의 필드만 채운다. 베이스가 0이면 false.
bool read_view(CameraView* out);

// 쓰기를 막고 있는 이유. 준비됐으면 nullptr.
//
// 실패를 bool로만 돌려주면 사용자가 무엇을 고쳐야 할지 알 수 없다.
// 전제 조건이 안 갖춰졌으면 그것을 화면에 말해야 한다.
const char* fov_write_blocker();

bool write_fov(float value);

}  // namespace cdtb::game

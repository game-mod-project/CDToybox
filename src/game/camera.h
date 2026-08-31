#pragma once

#include <cstdint>
#include <string>

namespace cdtb::game {

// 카메라 객체의 필드 오프셋.
//
// 2026-08-31 인게임 실측(문서: 2026-08-31-camera-offsets.md).
// FreeCamCamera 와 활성 카메라가 같은 레이아웃을 쓴다.
namespace camera_offset {
constexpr int kName = 0x48;        // 이름 객체 포인터
constexpr int kScale = 0x50;       // float[3]
constexpr int kRotation = 0x5C;    // 쿼터니언 float[4]
constexpr int kPosition = 0x6C;    // float[3]
constexpr int kViewportW = 0x7C;   // int32
constexpr int kViewportH = 0x80;   // int32
constexpr int kFov = 0x9C;         // float, 도 단위
constexpr int kNearClip = 0xB4;    // float
constexpr int kFarClip = 0xB8;     // float
}  // namespace camera_offset

// 런타임에 찾아낸 카메라들. 주소는 실행마다 바뀌므로 매번 탐색한다.
struct CameraSet {
    std::uintptr_t manager = 0;      // CameraManager
    std::uintptr_t free_cam = 0;     // FreeCamCamera (비활성 상태로 존재)
    std::uintptr_t photo_cam = 0;    // PhotoCamera
    std::uintptr_t active = 0;       // 현재 렌더에 쓰이는 카메라
    std::uintptr_t player_component = 0;   // PlayerCameraComponent

    bool complete() const {
        return manager != 0 && free_cam != 0 && active != 0;
    }
};

// RTTI로 카메라 객체를 찾는다. 수 초가 걸리므로 워커 스레드에서 부른다.
// 진행 상황은 로그에 남는다.
bool discover(CameraSet* out);

// 마지막 탐색 결과.
const CameraSet& cameras();
bool discovered();

// 필드 접근. 주소가 0이면 false.
bool read_fov(std::uintptr_t camera, float* out);
bool read_position(std::uintptr_t camera, float out[3]);
bool read_rotation(std::uintptr_t camera, float out[4]);
bool read_name(std::uintptr_t camera, std::string* out);

}  // namespace cdtb::game

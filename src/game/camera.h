#pragma once

#include <cstdint>
#include <string>

#include "mem/reader.h"
#include "mem/rtti.h"

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

// 탐색 본체. Reader 를 받으므로 모드(자기 프로세스)와 외부 분석
// 도구(cdtb_probe)가 같은 로직을 돌린다. 배포하기 전에 probe로
// 결과를 확인할 수 있어야 한다.
bool discover_with(const mem::Rtti& rtti, const mem::Reader& reader,
                   CameraSet* out);

// 카메라가 +0x48 에 들고 있는 이름을 읽는다.
// 이름 객체는 +0x18 에 짧은 문자열을 인라인으로 담는다(SSO).
bool camera_name(const mem::Reader& reader, std::uintptr_t camera,
                 std::string* out);

// 백그라운드에서 스스로 분석한다. 사용자는 게임만 하면 된다.
//
// 월드에 진입할 때까지 주기적으로 탐색을 재시도하고, 카메라를 찾으면
// 하드웨어 브레이크포인트로 갱신 코드를 추적해 결과를 로그에 남긴다.
// 버튼을 눌러 달라고 하지 않는다 - 그러면 모드를 만든 의미가 없다.
void start_auto_analysis();
void stop_auto_analysis();

// 마지막 탐색 결과.
const CameraSet& cameras();
bool discovered();

// 필드 접근. 주소가 0이면 false.
bool read_fov(std::uintptr_t camera, float* out);
bool read_position(std::uintptr_t camera, float out[3]);
bool read_rotation(std::uintptr_t camera, float out[4]);
bool read_name(std::uintptr_t camera, std::string* out);

}  // namespace cdtb::game

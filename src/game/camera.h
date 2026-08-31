#pragma once

#include <cstdint>
#include <string>

#include "mem/reader.h"
#include "mem/rtti.h"

namespace cdtb::game {

// 카메라 객체의 필드 오프셋. 2026-08-31 실행 중인 게임에서 실측.
// 문서: 2026-08-31-camera-write-paths.md
//
// 주의 - 지역 변환(kLocalScale/kLocalRotation/kLocalPosition)은 활성
// 카메라에서 항상 (1,1,1) / 항등 쿼터니언 / (0,0,0) 이다. 이미지
// 전수 조사에서도 그 칸에 쓰는 코드가 0곳이었다. 카메라의 월드
// 좌표는 카메라 객체가 아니라 PlayerCameraComponent 에 있다
// (component_offset::kWorldPosition). 큰 오픈월드가 흔히 쓰는
// 카메라 상대 렌더링으로 보인다.
namespace camera_offset {
constexpr int kName = 0x48;           // 이름 객체 포인터(SSO, +0x18 에 인라인)
constexpr int kLocalScale = 0x50;     // float[3] - 활성 카메라에서 (1,1,1)
constexpr int kLocalRotation = 0x5C;  // 쿼터니언 float[4] - 항등
constexpr int kLocalPosition = 0x6C;  // float[3] - (0,0,0)
constexpr int kViewportW = 0x7C;      // int32, 실측 1920
constexpr int kViewportH = 0x80;      // int32, 실측 1080
constexpr int kFov = 0x9C;            // float, 도 단위. 활성 45, 프리캠 60
constexpr int kNearClip = 0xB4;       // float, 실측 0.2
constexpr int kFarClip = 0xB8;        // float, 실측 100000
constexpr int kActiveFlags = 0xBC;    // 바이트 플래그. 활성 0x0101, 프리캠 0
constexpr int kRenderView = 0xE0;     // 렌더 뷰 객체 포인터. 프리캠은 0
constexpr int kOwner = 0x10;          // 소유 컴포넌트. 프리캠은 0
}  // namespace camera_offset

// PlayerCameraComponent 의 오프셋.
namespace component_offset {
// 활성 카메라의 내부 포인터(카메라 + 0x28)를 담는다. 카메라 객체를
// 얻으려면 0x28 을 빼야 한다. 게임의 파라미터 복사 루틴도 이 슬롯을
// 거쳐 카메라에 쓴다(0x140A59230).
constexpr int kActiveCamera = 0x88;

// 카메라 월드 좌표 float[3] + 패딩. 매 프레임 변하는 것을 실측했다.
// 이것이 프리카메라가 제어해야 할 진짜 값이다.
constexpr int kWorldPosition = 0x360;
}  // namespace component_offset

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
bool read_name(std::uintptr_t camera, std::string* out);

// 카메라 월드 좌표. 카메라가 아니라 컴포넌트에서 읽는다.
bool read_world_position(std::uintptr_t component, float out[3]);

}  // namespace cdtb::game

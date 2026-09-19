#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "mem/reader.h"
#include "mem/rtti.h"

namespace cdtb::game {

// 탈것(드래곤·A.T.A.G.·와이번 등)의 체력·스태미나 읽기 · 바꾸기 · 고정.
//
// **플레이어와 같은 사슬이다**(2026-09-18 실측, exe 1.0.0.2944). `player.h` 의
// 게이지 배열 체인이 탈것 액터에도 그대로 먹는다:
//
//     char +0x68 -> actor +0x20 -> marker +0x18 -> root +0x58 = 게이지 배열
//     항목 k = 배열 + k*0x90 : +0x00 i32 타입 · +0x08 i64 현재 · +0x18 i64 최대
//
// 값은 **1000배 척도**다. 블랙스타의 최대 체력 2,500,000 은 명부 레코드의
// "생명 2500"(`+0xA0`)과 정확히 맞는다.
//
// 실측값(참고):
//
//     블랙스타(드래곤)  체력 2,500,000  스태미나 300,000
//     와이번            체력   600,000  스태미나 150,000
//     무역 마차         체력 5,000,000  스태미나 —
//
// ---- ⚠ 이 배열은 **거울**이다 (2026-09-18 추적, 사용자 신고로 다시 팜)
//
// 처음에는 "체력은 약 2초마다 덮이고 스태미나는 안 덮인다" 고 적었는데, **둘 다
// 틀렸다.** 사용자가 "드래곤·A.T.A.G. 는 바꾸면 바로 복구된다" 고 해 다시 쟀다.
//
// 항목 한 칸(0x90)을 통째로 떠 보면 같은 값이 두 벌씩 있다:
//
//     +0x08 현재   +0x10 회복   +0x18 최대      <- 갱신될 때마다 덮인다
//     +0x58 기준최대   +0x80 기준회복          <- 우리 쓰기가 남지만 아무 효과 없다
//     +0x38 시계(초당 2000)  +0x48 틱 수
//
// 최대를 5,000,000 으로, 회복을 20,000 으로 써 두고 지켜보니 **+0x18 과 +0x10 만
// 원래 값으로 돌아오고 +0x58·+0x80 은 내 값 그대로 남았다.** 그리고 현재치는
// 틱마다 "내가 쓴 값" 이 아니라 **직전 값 + 회복량**으로 올라갔다 - 엔진이 우리
// 쓰기를 아예 못 본다는 뜻이다.
//
// 힙에서 서명(최대 2,500,000 · 회복 200 · +0x28 250,000)으로 훑으니 **똑같은
// 배열이 하나 더** 나왔다. 현재치·틱까지 한 치도 안 다른데 기준칸만 원래 값이었다.
// 거슬러 올라가니 클래스가 갈렸다:
//
//     우리가 쓰던 쪽 : ClientChildOnlyInGameActor -> ClientStatusActorComponent
//     진짜 값을 든 쪽: ServerChildOnlyInGameActor -> ServerStatusActorComponent
//
// 즉 **탈것도 플레이어와 똑같이 클라/서버 두 realm 이 있고**(`player.cpp` 가
// "사망 판정은 서버 게이지가 권위" 라고 적어 둔 그것), 우리는 클라 거울에만 썼다.
// 체력은 회복 틱이 1~1.5초마다 돌아 곧바로 덮이고("바로 복구"), 스태미나는 안 쓰는
// 동안 틱이 안 돌아 한동안 남아 있다가 타면 그때 덮인다. 사용자가 본 그대로다.
//
// ---- 권위 사본으로 가는 길
//
// 클라 사슬 어디에도 서버 쪽 포인터가 없다(액터·홀더 0x800, 컴포넌트·root 0x400 을
// 다 훑었다). 그래서 **RTTI 로 `ServerStatusActorComponent` 를 훑어** 핸들로 짝짓는다:
//
//     컴포넌트 +0x08 -> 액터 · 액터 +0x60 -> 핸들(클라와 **같은 값**)
//     컴포넌트 +0x18 -> root · root +0x58 -> 권위 게이지 배열
//
// 핸들이 두 realm 에서 같다는 것이 열쇠다(A.T.A.G. 는 양쪽 다 0xB0100003 이었다).
// 힙 전수라 값싸지 않으므로 **부탁할 때만** 훑고(`mount_authority_discover`),
// 쓸 때마다 컴포넌트에서 주소를 다시 얻으며 핸들을 재확인한다.
//
// ---- 대상은 **핸들**로 들고 있는다
//
// 액터 주소는 휘발성이다(clan-roster-volatile-writes: 예전 주소에 써서 게임을
// 팅긴 적이 있다). 고정 대상은 핸들로 기억하고, 매 틱 살아있는 액터 목록에서
// 그 핸들을 **다시 찾아** 주소를 얻는다. 못 찾으면 그 틱은 조용히 건너뛴다.
//
// ---- 안 건드리는 것 (근거를 정확히 적는다)
//
// 체력·스태미나 **둘만** 쓴다. 나머지 게이지는 읽지도 쓰지도 않는다.
//
// `player.h` 에 "위험 타입(핀 금지): 17/18(발열·자연발화), 48(탈것 화염)" 이라는
// 줄이 있는데, **그것을 근거로 삼지 않는다.** 출처를 따라가 보면:
//
//   - 그 줄을 넣은 것은 커밋 6d26f6c(2026-09-05)이고, 커밋 메시지에도 결정으로만
//     적혀 있다. 측정 기록이 없다.
//   - 참고한 CT 문서(`specs/2026-09-05-ct-asi-feature-review.md`)에 17·18·48
//     이야기가 **없다.** 외부 출처가 아니다.
//   - `player.cpp` 는 그 타입들을 **아예 언급하지 않는다.** 고정 오프셋 셋만
//     쓰기 때문에 닿지 않을 뿐, 금지를 강제하는 코드는 없다.
//   - "발열·자연발화·탈것 화염" 이라는 **이름조차 확인된 적이 없다.**
//
// 그래서 여기서 그 셋을 안 건드리는 진짜 이유는 "위험하다고 측정됐다" 가 아니라
// **"뜻을 모르는 칸을 고정해서 얻을 것이 없다"** 이다. 드래곤에서 실제로 읽힌
// 값은 타입 18 이 60,306/100,000 · 타입 17 이 65,781/100,000 이었다.

// 게이지 배열 배치
inline constexpr std::size_t kGaugeStride = 0x90;
inline constexpr std::size_t kGaugeType = 0x00;   // i32
inline constexpr std::size_t kGaugeCur = 0x08;    // i64
inline constexpr std::size_t kGaugeMax = 0x18;    // i64
// 최대치의 **기준값**. `+0x18` 은 여기서 유도되는 것으로 보인다(거울 쪽에서
// `+0x18` 만 되돌아오고 `+0x58` 은 남았다). 최대를 쓸 때 같이 쓴다 - 한쪽만
// 쓰면 언젠가 재계산이 돌 때 되돌아간다.
inline constexpr std::size_t kGaugeBaseMax = 0x58;   // i64
inline constexpr int kGaugeMaxEntries = 24;

// 액터에서 게이지 배열까지. 가운데 것은 `StatusActorComponent` 다(클래스 확인
// 2026-09-18: vtable -> RTTI 이름). 예전 주석의 "마커" 가 이것이다.
inline constexpr std::size_t kMvActorSub = 0x68;
inline constexpr std::size_t kMvMarker = 0x20;    // 홀더 -> 상태 컴포넌트
inline constexpr std::size_t kMvRoot = 0x18;      // 컴포넌트 -> root
inline constexpr std::size_t kMvArray = 0x58;     // root -> 게이지 배열

// 권위 사본(서버 realm)
inline constexpr const char* kMvServerStatusClass =
    ".?AVServerStatusActorComponent@pa@@";
inline constexpr std::size_t kMvStatusActor = 0x08;   // 컴포넌트 -> 액터
inline constexpr std::size_t kMvActorHandle = 0x60;   // 액터 안의 핸들(u32)
// 한 번 훑을 때 받아 올 인스턴스 상한. 실측에서 서버 액터가 200~900개였다.
inline constexpr std::size_t kMvAuthorityMax = 4096;

// 게이지 타입
inline constexpr std::uint32_t kTypeHealth = 0;
inline constexpr std::uint32_t kTypeStamina = 22;

// 값 상한. 표에서 본 제일 큰 것이 마차의 5,000,000 이라 넉넉히 잡는다.
// 상한을 두는 이유는 화면 입력이 엔진 칸을 넘겨 음수로 돌지 않게 하려는 것이다.
inline constexpr std::int64_t kMountVitalMax = 100'000'000;

struct MountVital {
    std::uintptr_t actor = 0;
    std::uint32_t handle = 0;
    std::string name;                 // 표시명(없으면 내부 이름)
    std::uint16_t merc_row = 0xFFFF;  // 동반자 타입 행
    std::uintptr_t gauges = 0;        // 클라 거울 배열(0 이면 못 잡음)
    std::uintptr_t authority = 0;     // 권위 배열(0 이면 아직 못 찾음)
    bool ok = false;                  // 항목[0].타입 == 0 게이트를 통과했나
    int hp_idx = -1;
    int sta_idx = -1;
    std::int64_t hp_cur = 0, hp_max = 0;
    std::int64_t sta_cur = 0, sta_max = 0;
};

// 월드에 나와 있는 **동반자** 중 게이지 배열이 잡히는 것만.
// 살아있는 액터 목록(`actors.h`)의 캐시를 쓴다 - 힙을 다시 안 훑는다.
std::vector<MountVital> mount_vitals(const mem::Reader& reader);

// ------------------------------------------------------------- 권위 사본
//
// **힙 전수다.** 분석 스레드에서만 부른다(렌더 프레임에서 부르면 게임이 멈춘다).
// 찾은 {핸들 -> 상태 컴포넌트} 를 캐시하고 개수를 돌려준다.
int mount_authority_discover(const mem::Rtti& rtti, const mem::Reader& reader);

// 캐시된 컴포넌트에서 **지금** 권위 배열 주소를 얻는다. 주소를 들고 다니지
// 않는다 - 컴포넌트를 거쳐 매번 다시 내려가고, 그 액터의 핸들이 아직 같은지
// 확인한다(탈것을 돌려보내면 객체가 재사용된다). 못 찾으면 0.
std::uintptr_t mount_authority_gauges(const mem::Reader& reader,
                                      std::uint32_t handle);

// 어디까지 왔나. **화면이 이걸 보고 버튼을 잠근다.**
//
// 준비가 덜 된 채로 쓰면 거울에만 가고, 서 있는 동안은 그대로 보이다가 타는
// 순간 되돌아간다. 예전에는 경고만 띄우고 버튼은 열어 뒀는데, 사용자가 그
// 경고를 못 보고 눌렀다가 "탑승하면 초기화된다" 로 읽었다(2026-09-19).
// 이제 **찾기 전에는 아예 못 누른다** - 기다리면 저절로 열린다.
enum class MountAuthPhase {
    Idle,       // 아직 부탁한 적 없다
    Waiting,    // 부탁했고 분석 스레드가 집어 가기를 기다린다
    Scanning,   // 훑는 중이다 (힙 전수)
    Done,       // 한 번 끝났다. 짝지은 수는 mount_authority_count()
};
MountAuthPhase mount_authority_phase();

// 지금 훑기가 시작된 뒤 몇 초나 지났나. 안 돌고 있으면 0.
// "1분쯤 걸린다" 는 안내만으로는 멈춘 것인지 도는 것인지 못 가른다.
double mount_authority_elapsed_sec();

// 화면 표시용.
std::size_t mount_authority_count();
void mount_authority_request_refresh();
bool mount_authority_take_refresh();

// 고정 설정. `-1` 은 "그 칸은 안 건드린다" 는 뜻이다.
struct MountPin {
    std::uint32_t handle = 0;   // 대상. 0 이면 꺼진 것이다
    std::int64_t hp_cur = -1;
    std::int64_t hp_max = -1;
    std::int64_t sta_cur = -1;
    std::int64_t sta_max = -1;
};

// 한 번만 쓴다(고정 없이). 성공하면 참.
bool mount_vital_write(const mem::Reader& reader, std::uint32_t handle,
                       const MountPin& what);

void mount_pin_set(const MountPin& pin);
MountPin mount_pin_get();
void mount_pin_clear();

// 매 틱 적용. **모드(주입 DLL)에서만.** 대상이 없으면 조용히 돌아간다.
void mount_pin_tick(const mem::Reader& reader);

// --------------------------------------------------------- 순수 부분(시험용)

// 그 게이지 타입을 건드려도 되나. 17·18·48 은 위험 타입이라 거짓.
bool mount_type_safe(std::uint32_t type);

// 항목 k 의 배열 안 오프셋.
std::size_t gauge_offset(int index);

// 화면에서 들어온 값을 칸에 맞게 자른다. 음수는 0, 상한 초과는 상한.
std::int64_t mount_clamp(std::int64_t value);

// 이 칸을 쓸 것인가(-1 은 안 건드림).
bool mount_pin_wants(std::int64_t field);

// 액터 핸들처럼 생겼나. 사용자 0x9010 · 일반 0xB010 · 소유 0xA010 네임스페이스다
// (`actors.h`). 서버 컴포넌트 후보에서 다중 상속 서브객체를 걸러내는 데 쓴다.
bool mount_handle_plausible(std::uint32_t handle);

// 고정이 켜져 있나(대상이 있고 쓸 칸이 하나라도 있나).
bool mount_pin_active(const MountPin& pin);

}  // namespace cdtb::game

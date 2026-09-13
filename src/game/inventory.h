#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "mem/reader.h"
#include "mem/rtti.h"

namespace cdtb::game {

// 서버 인벤토리 컴포넌트(`pa::ServerInventoryActorComponent`)를 읽는다.
//
// 실측 구조 (docs/superpowers/specs/2026-09-02-inventory.md):
//
//   컴포넌트 +0x18  ptr 컨테이너 포인터 배열
//            +0x20  u32 개수 / +0x24 u32 용량   (실측 18 / 18)
//   컨테이너 +0x00  ptr 레코드 배열
//            +0x08  u32 배열 칸 수 (실측 1460)
//            +0x0C  u32 같은 값
//            +0x10  u16 종류 / +0x12 u16 사용 / +0x14 u16 용량
//   레코드   0xC8 간격
//            +0x00  u64 인스턴스 ID   (빈 칸은 전부 0xFF)
//            +0x08  u16 순번 / +0x0A u16 담금질
//            +0x10  i64 개수
//            +0x60  ptr 소켓 배열 / +0x68 u32 5 / u32 5
//
// 컴포넌트 앞부분을 훑어 `{포인터, 사용, 용량}` 꼴을 찾는 방식은
// 쓰지 않는다. 그렇게 하면 엉뚱한 컨테이너가 걸리고 정작 플레이어
// 가방은 못 잡는다 - 가방은 `+0x18` 의 포인터 배열 너머에 있다.
//
// 컨테이너의 `용량`은 화면에 나오는 값과 같다. 실측에서 종류 1 이
// 97/130, 종류 4 가 144/300 이었고 화면 표시와 일치했다.

struct InventoryContainer {
    std::uintptr_t address = 0;   // 컨테이너 자신
    std::uintptr_t records = 0;   // 레코드 배열
    std::uint32_t slots = 0;      // 배열 칸 수
    std::uint16_t kind = 0;       // 종류 (실측 0~4)
    std::uint16_t used = 0;       // 사용 개수 (믿지 않는다, 아래 참고)
    std::uint16_t capacity = 0;   // 화면에 나오는 용량
};

struct InventoryRecord {
    std::uintptr_t address = 0;      // 레코드 주소
    std::uint32_t slot = 0;          // 배열에서의 칸 번호
    std::uint64_t instance_id = 0;
    std::uint32_t index = 0;         // 아이템 표에서의 순번
    std::uint32_t temper = 0;        // 담금질

    // 장비 연마 (레코드 +0x58). 게임 툴팁의 "장비 연마 100/100" 이다.
    // 상한은 아이템 표의 `_SharpnessData`(+0x2E8).
    std::uint32_t sharpness = 0;

    // 현재 내구도 (레코드 +0x40). **0xFFFF 면 내구도가 없는
    // 아이템**이다 - 아이템 표의 `_maxEndurance` 와 같은 표기다.
    // 갓 지급한 것은 게임이 저장을 한 바퀴 돌기 전까지 0 이다.
    std::uint32_t endurance = 0;
    std::int64_t count = 0;
    std::uintptr_t sockets = 0;      // 소켓 배열 (레코드 +0x60)

    // 배열의 크기 (레코드 +0x68). **아이템의 소켓 칸 수가 아니다** -
    // 게임이 늘 5칸을 잡아 실측이 예외 없이 5다. 용량(+0x6C)도 5다.
    std::uint32_t socket_count = 0;

    // **열린 소켓 칸 수** (레코드 +0x70, u8). 이것이 사람이 보는
    // "이 장비의 소켓 개수" 다. 잠긴 칸은 배열에 남아 있되
    // `raw[4] == 0xFF` 다. 아이템 표의 상한은 `max_sockets`(+0x238).
    std::uint8_t open_sockets = 0;
};

// 소켓 한 칸. 실측 6바이트다.
//
//   24 0D FF FF 00 §§    보석이 박힌 칸 (칸 0)
//   FF FF 00 00 01 §§    열려 있는 빈 칸 (칸 1)
//   FF FF 00 00 FF ??    잠긴 칸 (게임이 [5] 를 안 건드려 쓰레기값)
//
//   +0x00  u16  박힌 것의 아이템 표 순번. 0xFFFF 면 빈 칸
//   +0x02  u16  채움 표시. 보석 있으면 0xFFFF, 비면 0x0000
//   +0x04  u8   **칸 번호(=열림) / 0xFF(=잠김, items.h 의 kSocketLocked)**
//   +0x05  u8   뜻 모름(§§). **고정 상수가 아니다** - 한 판 안에서는
//               열린 칸이 전부 같은데 판이 바뀌면 달라질 수 있다(실측
//               다섯 판: 04 · 05 · 03 · 02 · 02). 로드할 때 게임이 다시
//               매기므로 우리가 쓴 값은 남지 않는다.
//
// 근거: specs/2026-09-07-socket-grant-unlock-research.md 2.2 절.
inline constexpr std::size_t kSocketSize = 6;

struct InventorySocket {
    std::uint32_t slot = 0;
    std::uint16_t index = 0xFFFF;
    std::uint8_t raw[kSocketSize]{};

    bool empty() const { return index == 0xFFFF; }
};

// 소켓 칸이 이보다 많으면 레코드를 잘못 집은 것으로 본다. 실측 5칸이다.
inline constexpr std::uint32_t kMaxSockets = 64;

// 레코드의 소켓 배열을 읽는다. 배열이 없으면 false 다.
bool read_inventory_sockets(const mem::Reader& reader,
                            const InventoryRecord& record,
                            std::vector<InventorySocket>* out);

// 배열 칸 수가 이보다 크면 컨테이너를 잘못 집은 것으로 본다.
// 실측 1,460칸이다.
inline constexpr std::uint32_t kMaxInventorySlots = 1u << 15;

// 컨테이너가 이보다 많으면 컴포넌트를 잘못 집은 것으로 본다.
// 실측 18개다.
inline constexpr std::uint32_t kMaxInventoryContainers = 256;

// `+0x18` 의 포인터 배열을 따라 컨테이너를 모은다. 널 슬롯과 읽기
// 실패한 것은 건너뛴다.
bool read_inventory_containers(const mem::Reader& reader,
                               std::uintptr_t component,
                               std::vector<InventoryContainer>* out);

// 레코드 배열을 통째로 읽어 빈 칸을 뺀 것을 모은다.
//
// `used` 에서 멈추지 않고 칸 전체를 본다. 실측에서 144 라고 하는데
// 실제 레코드는 143개였다 - 중간에 빈 칸이 섞여 있어 그 값으로
// 자르면 뒤가 잘린다.
bool read_inventory_records(const mem::Reader& reader,
                            const InventoryContainer& container,
                            std::vector<InventoryRecord>* out);


// --------------------------------------------------- 모드용 배경 탐색

// 서버 인벤토리 컴포넌트를 RTTI 로 찾아 캐시한다. 찾으면 그다음부터
// 즉시 참을 돌려주므로 재시도 루프에서 부르면 된다.
//
// **내용이 있는 첫 컴포넌트**를 고른다. 빈 것이 여럿 살아 있어서
// (NPC · 상자 등) 그냥 첫 번째를 잡으면 늘 비어 보인다. probe 가
// 쓰는 기준과 같다.
bool discover_inventory(const mem::Rtti& rtti, const mem::Reader& reader);
bool inventory_ready();
std::uintptr_t inventory_component();

// 캐시된 **클라 realm** 인벤토리 컴포넌트. 없으면 0. 가방 확장이 both-realms
// 로 쓰려고 둔다(화면 숫자의 출처가 어느 쪽인지 확정되지 않아 둘 다 쓴다).
std::uintptr_t inventory_component_client();
// 두 realm 을 **다** 잡았는가. 배경 루프의 탐색 종료 조건에 쓴다 - 서버만 보고
// 멈추면 클라를 영영 못 찾아 가방 확장이 한쪽 realm 에만 간다(리뷰 지적 4).
bool inventory_both_ready();

// ------------------------------------------------------ 가방·보관함 확장
//
// **2026-09-05 사고의 원인은 "임시 버퍼" 가 아니었다.** 되돌림 커밋은 "등록
// 컨테이너 전부에 써서 4개 평행 사본 중 임시 버퍼 2개를 건드렸다" 고 적었지만,
// 되돌린 코드는 컨테이너 객체의 +0x14~+0x1A **네 칸만** 썼고 레코드 배열은
// 건드린 적이 없다(참고 모드가 말하는 4개 사본은 **레코드 배열**이다).
// 사고는 다음 둘로 설명된다(2026-09-12 재조사·라이브 재검증). 확정된 것은
// "산술이 틀렸다"·"목표 999·상한 없음"·"레코드 배열은 안 건드렸다" 이고,
// **크래시의 직접 기전 자체는 미확정**이다:
//
//   1. **기본 슬롯 유도가 틀렸다.** 옛 코드는 `기본 = (+0x14) - (+0x1A)` 였는데,
//      가방은 확장을 +0x18 에 담아(+0x1A = 0) 240 이 나왔다(정답 50). 보관함은
//      반대로 +0x1A 에 담는다. 한 칸만 보면 반드시 틀린다.
//      실측: 가방 +0x14=240 +0x16=190 +0x18=190 +0x1A=0
//            보관함 +0x14=440 +0x16=200 +0x18=0  +0x1A=200
//   2. **목표 기본값이 999 였고 하드 상한이 없었다**(60000 까지 허용).
//      게다가 같은 값을 +0x16·+0x18·+0x1A 세 칸에 흩뿌려 배증 위험까지 있었다.
//
// 그래서 지금은 `기본 = (+0x14) - (+0x16)` 으로 유도하고, **두 갈래 중 고른 한
// 칸만** 바꾼다(반대 칸은 그대로 둔다). 엔진이 +0x16 을 합으로 재계산하든 최대로
// 재계산하든 결과가 목표 이하가 되어(최대면 오히려 작아진다) 미확정인 부분을
// 안전한 쪽으로 비켜 간다.
//
// 상한: 참고 모드 둘이 독립적으로 **732 초과는 엔진이 깨진다**고 적는다(CT 소스
// `HARD_MAX = 732`, ASI "Above 732 the game breaks ... a save made in that state
// can crash on load"). 그 아래 **700** 이 그들이 쓰는 실전값이다("the tested safe
// value", 널리 쓰이는 인벤토리 확장 모드와 같은 값). 732 의 기전은 상류도 우리도
// 못 짚었다 - 물리 배열 칸 수는 1460 이고 언모드 천장은 240 이라 둘 중 어느 것도
// 아니다. 실패 비용이 세이브 손상이라 그 위는 시험하지 않는다.
inline constexpr int kBagTargetMax = 700;    // 화면 슬라이더의 상한(가장 큰 종류 기준)
inline constexpr int kBagEngineMax = 732;    // 엔진이 깨지는 선 - 절대 넘기지 않는다

// -------------------------------------------------------- 종류별 규칙 한 표
//
// **700 은 가방에서 얻은 값이다.** CT 의 `HARD_MAX = 732` 도, ASI 의 `Target=700` 도
// 둘 다 그 모드의 **[Bag] 항목**에 있다. 보관함류의 엔진 한계를 말한 상류 문서는
// 아직 없다(2026-09-13 조사 중). 그래서 상한을 종류마다 따로 둘 수 있게 표로 뺀다.
//
// 거르개와 상한을 **한 표에** 둔다. 예전에는 "어느 종류를 건드리나" 가 함수 안에
// 흩어져 있었는데, 상한까지 따로 두면 둘이 어긋나 "건드리는데 상한이 없는 종류" 가
// 생긴다.
struct BagKindRule {
    std::uint16_t kind;
    int cap;             // 이 종류의 용량 상한
    bool storage_only;   // 참이면 "보관함도 함께" 를 켰을 때만 건드린다
    const char* name;    // 화면에 내는 이름
};

// 우리가 건드리는 종류 전부. 여기 없는 종류는 어느 쪽이든 건드리지 않는다 -
// 용량 5·10·20·50 짜리 작은 칸까지 부풀린 것이 2026-09-05 "리로드 후 지급 손상" 의
// 유력한 원인이다. **종류 4 는 뺀다**: 혼자 +0x20 에 8칸짜리 보조 배열을 다는데
// (실측 2026-09-13) 그 정체를 모른다(리뷰 지적 11).
std::span<const BagKindRule> bag_kind_rules();

// 이 종류의 상한. 표에 없으면 0(= 안 건드린다).
int bag_kind_cap(std::uint16_t kind);

// 컨테이너 하나를 어떻게 바꿀지 계산한 결과. **순수 계산**이라 시험할 수 있다.
// 확장을 어느 칸에 쓸 것인가. **2026-09-13 실측: 세이브·로드를 하면 우리가 넣은
// 확장이 사라진다.** 가방이 원래 갖고 있던 정당한 확장 190 은 +0x18 에 있었고
// (보관함은 반대로 +0x1A 에 200), 우리는 비어 있던 +0x1A 에 60 을 넣었다. 엔진에
// VaryExpandedInventorySlotAck 와 VaryExpandedNoSaveInventorySlotAck 가 따로 있으니
// (2026-09-12 스파이크), 두 칸이 각각 "저장되는 확장" 과 "저장 안 되는 확장" 일 수
// 있다. 배포를 거듭하지 않고 가리려고 **화면에서 고를 수 있게** 둔다.
//
// 저장에 남든 안 남든 화면에서는 유지되게 하는 것은 이것과 별개다 - 게임은 로드할
// 때 인벤토리 컴포넌트를 **통째로 새로 만들기** 때문에, 자동 재적용(bag_auto_set)
// 쪽이 그 절반을 맡는다.
enum BagBranch : int {
    kBagBranchA = 0,   // +0x18 - 가방의 기존 확장이 여기 있다(기본)
    kBagBranchB = 1,   // +0x1A - 처음 쓴 칸, 리로드에서 사라졌다
};

struct BagPlan {
    bool apply = false;        // 거짓이면 건너뛴다(모르는 모양이거나 바꿀 게 없다)
    const char* skip = "";     // apply 가 거짓인 이유(화면·로그에 그대로 나간다)
    bool same = false;         // 건너뛴 이유가 "이미 그 값이다" 인가.
                               // **판정을 문구에서 뗀다** - skip 문자열을 비교해
                               // 쓰면 문구를 다듬는 순간 판정이 조용히 멈춘다.
    // 모양 검사를 통과했는가(= 우리가 아는 컨테이너다). 통과한 뒤의 건너뜀은
    // "안 건드리기로 한 결정" 이고, 통과 못 한 건너뜀은 "아직 못 알아봤다" 다.
    // 리로드 직후의 전이 상태가 후자라, 자동 재적용이 다시 해 볼 근거가 된다.
    bool understood = false;
    int base = 0;              // 유도한 기본 슬롯
    int branch = kBagBranchA;  // 어느 칸을 쓸 것인가
    int expand = 0;            // 그 칸에 쓸 값
    int other = 0;             // 반대 칸의 현재 값(그대로 둔다)
    int sum = 0;               // +0x16 에 쓸 값 = expand + other
    int capacity = 0;          // +0x14 에 쓸 값 = base + sum
};

// cap/sum/a/b 는 각각 컨테이너의 +0x14/+0x16/+0x18/+0x1A, slots 는 +0x08 이다.
// target 이 0 이면 "원래대로"(복원)가 아니라 **바꿀 것 없음**이다 - 복원은 저장해
// 둔 원본을 그대로 쓰는 별도 경로다(옛 restore 는 확장을 0 으로 써서 가방의 190 을
// 날렸다. 그건 복원이 아니었다).
// limit 은 **이 컨테이너 종류의 상한**이다(bag_kind_cap). 0 이나 음수면 상한이
// 없는 것으로 보지 않고 kBagTargetMax 를 쓴다 - 상한 없는 길을 만들지 않는다.
BagPlan plan_bag_expand(int cap, int sum, int a, int b, int slots, int target,
                        int branch = kBagBranchA,
                        int limit = kBagTargetMax);

// 이 종류를 건드릴 것인가(bag_kind_rules 를 읽는다).
// 가방(1)은 언제나, 보관함류(7·9·11)는 storage 가 참일 때만,
// 작은 칸(용량 5·10·20·50)은 **절대** 건드리지 않는다 - 그것까지 부풀린 것이
// 2026-09-05 "리로드 후 지급 손상" 의 유력한 원인이다. 종류 4 는 혼자 +0x20 에
// 8칸짜리 보조 배열을 달아(실측) 정체를 모르므로 뺀다(리뷰 지적 11).
// 헤더로 올린 이유: 시험이 이 거르개를 덮을 수 있어야 한다(리뷰 지적 8).
bool bag_kind_selected(std::uint16_t kind, bool storage);

struct BagResult {
    int changed = 0;   // 실제로 쓴 컨테이너 수(realm 합산)
    int skip = 0;      // 건너뛴 수(아래 unknown 을 포함한다)
    // 그중 **모양 검사를 통과 못 해** 건너뛴 수(BagPlan::understood 가 거짓).
     // 목표보다 이미 크거나 이미 그 값인 것은 모양을 알아본 것이라 여기 안 든다.
    // 리로드 직후에는 컨테이너가 아직 채워지는 중이라 cap < sum 같은 모양으로
    // 보인다(실측 2026-09-13: 재탐색 0.001초 뒤에 자동 재적용이 들어가 서버
    // realm 4개가 전부 이 이유로 밀렸다). 그 상태는 "끝" 이 아니라 "잠시 뒤
    // 다시 해야 할 일" 이므로 자동 재적용이 세대를 소모하지 않는 근거가 된다.
    int unknown = 0;
    int fail = 0;      // 쓰기나 되읽기가 실패한 수
    int realms = 0;    // 실제로 쓴 realm 수(1 이면 한쪽만 - 화면이 안 바뀔 수 있다)
    const char* last_skip = "";   // 마지막으로 건너뛴 이유(화면·로그에 낸다)
};

// 가방(종류 1)을, storage 가 참이면 보관함류(종류 7·9·11 - 4 는 다른 구조를 달아
// 뺐다, bag_kind_selected 참고)도 함께 target 슬롯으로
// 맞춘다. 클라·서버 두 realm 에 같이 쓴다. **모드(주입 DLL)에서만** 부른다.
BagResult bag_expand(const mem::Reader& reader, int target, bool storage,
                     int branch = kBagBranchA);

// 마지막 확장 전의 원래 값으로 되돌린다. 기억해 둔 것이 없으면 아무것도 안 한다.
BagResult bag_restore(const mem::Reader& reader);
bool bag_has_backup();
// 되돌릴 기록이 **버려진** 적이 있는가. "확장한 적이 없습니다" 와 "기록이
// 사라졌습니다" 는 사용자에게 전혀 다른 말이다(리뷰 경미 4).
bool bag_backup_dropped();

// ------------------------------------------------- 되돌리기 기록(순수 부분)

// 확장 전 원본. 열쇠는 **(realm, 종류)** 하나뿐이다 - 인벤토리가 새로 생기면 주소는
// 바뀌지만 realm 과 종류는 그대로다. 주소로 열쇠를 잡으면 자동 재적용이 "이미 확장된
// 상태" 를 원본으로 삼아, 되돌리기가 240 이 아니라 300 으로 간다(2026-09-13).
//
// **한 컴포넌트 안에서 종류가 유일하다는 데 기댄다**(실측 18개, 종류 0~19 가 한 번씩).
// 그 가정이 깨지면 기록 하나가 같은 종류의 다른 컨테이너를 가리킬 수 있는데, 그때
// 쓰기를 막는 것은 열쇠가 아니라 bag_restore_blocked 의 혈통 검사다 - 값이 우리가 써
// 놓은 것과 다르면 쓰지 않는다. 주소로 한 번 더 거르지 **않는** 이유는, 게임이 힙에서
// 같은 자리를 돌려주면 주소가 남의 컨테이너를 가리켜 오히려 기록을 잃기 때문이다
// (재검토 중대 2).
struct BagBackup {
    int realm = 0;                   // 0 = 서버, 1 = 클라
    std::uint16_t kind = 0;
    std::uintptr_t address = 0;      // 지금 주소(새로 잡힐 때마다 갱신)
    std::uint16_t cap = 0, sum = 0, a = 0, b = 0;   // **최초** 원본, 덮지 않는다
    // 우리가 마지막으로 만들어 놓은 값. 두 곳에서 쓴다.
    //   1) 되돌리기 직전에 지금 값과 대조한다 - 다르면 그 사이 **우리가 아닌
    //      누군가가** 바꾼 것이므로 되돌리지 않는다(리뷰 치명 3).
    //   2) 다음 적용 때 손대기 전 값이 이것과 다르면 **원본을 다시 잡는다**.
    //      이것이 없으면 자동 재적용이 want 만 새로 덮어써서 1)의 관문이 리로드 한
    //      번으로 무력해지고, 되돌리기가 사용자가 돈 주고 산 칸을 지운다
    //      (재검토 치명 1).
    std::uint16_t want_cap = 0, want_sum = 0;
};

// 한 컨테이너를 보고 기록에 반영할 내용.
struct BagSeen {
    int realm = 0;
    std::uint16_t kind = 0;
    std::uintptr_t address = 0;
    std::uint16_t cap = 0, sum = 0, a = 0, b = 0;   // 손대기 **전** 지금 값
    bool changed = false;   // 우리가 방금 썼는가 - 새 기록을 만들 자격이다
    // 이 컨테이너의 모양을 우리가 이해했는가(계획이 쓰기로 갔거나 "이미 그 값" 이다).
    // 모르는 모양의 값을 원본으로 채택하면, 전이 상태를 한 번 본 것만으로 멀쩡한
    // 원본이 그 값으로 갈려 되돌리기가 엉뚱한 이유로 막힌다(게이트 경미 3).
    bool understood = false;
    bool known = false;     // 지금 컨테이너 값이 "우리가 만든 값" 이라고 말할 수 있나
    std::uint16_t want_cap = 0, want_sum = 0;      // known 일 때의 그 값
};

// 기록을 갱신한다. 규칙 셋:
//   * **바꾸지 않았어도 주소는 갱신한다** - "이미 그 값이다" 는 우리가 쓴 값이 세이브에
//     남아 그대로인 경우의 판정이라, 그때 주소를 안 고치면 이 기능이 **성공했을 때만**
//     되돌리기가 잠긴다(리뷰 치명 1).
//   * 손대기 전 값이 우리가 써 놓은 값(want)과 **다르면 그것이 새 원본이다** - 그
//     사이 누가 바꿨다는 뜻이고, 되돌아갈 자리는 옛 원본이 아니라 지금 그 값이다
//     (재검토 치명 1).
//   * 새 기록은 changed 일 때만 만든다 - 우리가 바꾼 적 없는 컨테이너의 되돌리기
//     기록은 있을 이유가 없다.
void bag_backup_upsert(std::vector<BagBackup>& v, const BagSeen& seen);

// 지금 이 컨테이너에 이 기록을 되돌려도 되는가. 되면 nullptr, 안 되면 그 이유.
// cap/sum/used 는 지금 컨테이너의 +0x14/+0x16/+0x12 다.
const char* bag_restore_blocked(const BagBackup& s, int cap, int sum, int used);

// 지금 값이 저장해 둔 원본 그대로인가. 그렇다면 되돌릴 것이 없으므로 막힘이 아니라
// **완료**로 쳐서 기록을 지운다. 안 그러면 - 자동 재적용을 끈 사용자가 리로드할
// 때마다 - 버튼이 켜진 채 "적용한 뒤 값이 바뀌었습니다" 만 낸다(게이트 경미 5).
bool bag_restore_already_original(const BagBackup& s, int cap, int sum, int a,
                                  int b);

// 지금 자동 재적용을 걸어야 하는가(순수).
bool should_auto_reapply(bool on, unsigned gen, unsigned auto_gen,
                         bool both_ready);

// --------------------------------------------- 리로드에서 살아남게 하는 두 축

// **로드 후 자동 다시 적용.** 게임은 세이브를 불러올 때 인벤토리 컴포넌트를 통째로
// 새로 만든다(실측 2026-09-13: 옛 주소의 컨테이너 배열이 0xFFFF/0 이 된다). 그래서
// 메모리에 쓴 용량은 저장에 남든 안 남든 **화면에서 사라진다**. 사용자가 이번 실행에
// 한 번 적용했다면 그 설정을 기억해 두었다가, 인벤토리가 새로 잡힐 때 한 번 더 건다.
//
// 사용자가 직접 누른 것을 그대로 되풀이할 뿐이다 - 스스로 값을 정하지 않는다.
void bag_auto_set(int target, bool storage, int branch);
void bag_auto_clear();      // 되돌리기와 화면 체크 해제가 부른다
bool bag_auto_on();
// 분석 루프가 매 바퀴 부른다. 무장돼 있고 인벤토리가 **새 세대로** 잡혔을 때만 쓴다
// (주소가 아니라 세대다 - 힙이 같은 자리를 돌려주면 주소 비교는 조용히 실패한다).
void bag_auto_tick(const mem::Reader& reader);

// 캐시해 둔 컴포넌트가 아직 살아 있는지 본다. 죽었으면 버리고 다시 찾게 한다.
// 예전에는 이것이 없어, 로드 뒤 캐시가 죽은 포인터를 든 채로 영원히 남았다 -
// 패널은 낡은 값을 보이고 가방 확장은 조용히 아무것도 안 했다(2026-09-13).
// 로딩 화면에서 잠깐 안 읽히는 것과 가르려고 연속 실패를 세고 나서 버린다.
void inventory_check_alive(const mem::Reader& reader);

// 캐시를 버린다. 게임이 인벤토리를 새로 만들면(재접속 등) 옛 주소가
// 남으므로 화면에서 다시 찾을 수 있어야 한다. 버리면 곧 다시 찾도록
// 요청까지 남긴다 - 한쪽만 부르는 자리를 만들지 않는다.
void forget_inventory();


// "지금 다시 찾아라". RTTI 인스턴스 탐색은 힙 전수라 값싸지 않아
// 배경 루프가 10초에 한 번만 돌린다. 화면에서 다시 찾기를 누른
// 사람에게 그 10초는 "눌렀는데 아무 일도 안 난다" 로 보인다.
// 요청을 남기면 루프가 다음 2초 안에 집어간다.
//
// 탐색에 필요한 `Rtti` 는 배경 루프의 것이다(350MB 이미지를 들고
// 있다). 화면 스레드가 직접 부르지 않고 요청만 남기는 이유다.
void request_inventory_rescan();

// 요청을 집어간다. 남아 있었으면 참이고, 그 즉시 지워진다.
bool take_inventory_rescan();

// --------------------------------------------------------- 표시용 변환

// 인벤토리 한 줄의 표시용 문자열. 규칙을 화면 코드에서 떼어 낸다.
struct InventoryRowText {
    std::string endurance;   // 비면 내구도가 없는 아이템이다
    std::string sharpness;   // 비면 0
    std::string sockets;     // 비면 박힌 것이 없다
};

// 내구도가 없는 아이템은 레코드에 0xFFFF 가 들어 있다. 그대로 내면
// 65535 로 보이므로 빈칸이어야 한다.
inline constexpr std::uint32_t kNoEndurance = 0xFFFF;

InventoryRowText format_inventory_row(std::uint32_t endurance,
                                      std::uint32_t sharpness,
                                      const std::vector<std::string>& gems);


// 이 모듈이 힙에서 찾는 RTTI 클래스(통과 단위 미리 훑기용, mem/rtti.h prefetch_instances).
std::vector<std::string> inventory_scan_classes();

}  // namespace cdtb::game

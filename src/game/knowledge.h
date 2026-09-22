#pragma once

// @build 1.0.0.2949  지식 등록 함수(0x2B72C90)와 서버 컴포넌트 vtable(0x5B2B880)을
//   재도출했고, 매니저 전역(0x6D69AB8)은 지식 정보 조회 함수 0x433650 이 읽는 전역으로
//   그대로임을 확인했다(2026-09-22). 근거: specs/2026-09-22-game-update-2949.md §5.
//   (2944 때 매니저 전역은 라이브 프로브로 확정했다 - 2944 스펙 §6. 낡으면
//   know_mgr_sane 이 걸러 "표를 아직 못 잡았습니다" 로 끝난다.)
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "mem/reader.h"
#include "mem/rtti.h"

namespace cdtb::game {

// 지식(스킬 트리 노드) 표.
//
// 화면의 `[깨달음] 필요` / `[원소: …] 중 하나` 는 **둘 다 선행 지식**이다. 문자열 키가
// `UI_Skill_RequiredKnowledgeNotLearned` / `UI_Skill_RequiredAnyKnowledgeNotLearned` 한 쌍이고,
// 둘을 고르는 코드가 같은 목록·같은 개수를 쓰고 메시지만 갈아 끼운다(2026-09-13 실측).
//
// 판정은 `CheckLearnOrLevelUp`(RVA 0x0208B070) 안의 루프 0x0208BB7D~0x0208BCD9 이고,
// 하는 일은 **한 줄**이다(RVA 0x0208BC20 `cmp dword [r10+rcx*8], eax`):
//
//     배운지식표[선행지식번호].레벨 >= 필요레벨
//
// 그 루프는 **쓰기가 하나도 없다** - 툴팁 목록은 별개 루프(아이템·재화, 0x0208B669~)가
// 만든다. 그래서 "배운 것으로 만들기" 만으로 조건이 그대로 풀린다.
//
// 그리고 지식을 배우면 휠도 열린다(정적 표 사슬):
//     KnowledgeInfo._learnApplySkillInfo(+0x104) -> SkillInfo._reserveSlotInfoList(+0xE8)
// 판정만 우회하는 코드 패치로는 휠이 안 열린다 - 그래서 데이터 쓰기가 정공법이다.

// ------------------------------------------------------- 실측 오프셋 (2026-09-13)

// 매니저 전역. 조회 함수 RVA 0x003C1C70 이 그대로 보여 준다:
//   rbx = *(전역);  if (번호 >= [rbx+8]) 실패;  info = *(u64*)([rbx+0x58] + 번호*8)
// **RVA 를 박아 두는 것이라 게임이 갱신되면 밀린다**(game-update-rva-drift).
// 그래서 읽은 값이 말이 되는지, 그리고 컴포넌트의 레코드 수와 같은지 반드시 대조한다
// (`Initialize` 가 매니저의 개수로 표를 잡으므로 두 값은 같아야 한다).
inline constexpr std::uintptr_t kKnowMgrGlobalRva = 0x06D69AB8;   // 2850 까지 0x06C2E2D8
inline constexpr std::size_t kKnowMgrCount = 0x08;   // i32 전체 지식 수
inline constexpr std::size_t kKnowMgrArray = 0x58;   // KnowledgeInfo* 배열(stride 8)

// KnowledgeActorComponent 의 배운 지식 표. `Initialize`(RVA 0x0E09C4F0)가
// 전체 지식 수만큼 **한 번에** 잡는 밀집 배열이라, 끼워 넣을 것이 없다.
inline constexpr std::size_t kKnowCompData = 0x18;
inline constexpr std::size_t kKnowCompCount = 0x20;
inline constexpr std::size_t kKnowCompCap = 0x24;
inline constexpr std::size_t kKnowRecStride = 24;
inline constexpr std::size_t kKnowRecLevel = 0x00;   // i32, 0 = 미습득
inline constexpr std::size_t kKnowRecObj = 0x08;     // 지연 생성 객체 - 건드리지 않는다
inline constexpr std::size_t kKnowRecFlag = 0x10;    // u8, 게임도 1 을 쓴다

// KnowledgeInfo / KnowledgeLevelData / learnFrom
inline constexpr std::size_t kInfoLevels = 0x88;        // KnowledgeLevelData 배열
inline constexpr std::size_t kInfoLevelCount = 0x90;    // i32
inline constexpr std::size_t kLevelStride = 0xE8;
// `_knowledgeName` 은 단일 키가 아니라 **{포인터, u64 키, u32 분류}** 구조체다.
// 필드는 +0xA8 에서 시작하지만 **키는 그 안쪽 +8**, 즉 절대 +0xB0 이다.
// 처음에 +0xA8(포인터)을 키로 읽어 이름이 하나도 안 풀렸다 - 실측으로 잡았다
// (2026-09-14, 지식 0번: +0xB0 키 0xB1F27E3200000490 -> "생명", 분류 9).
inline constexpr std::size_t kLevelName = 0xB0;   // u64 현지화 키
inline constexpr std::size_t kLevelDesc = 0xD0;   // 설명(같은 모양)
inline constexpr std::size_t kLevelLearnFrom = 0x78;
inline constexpr std::size_t kLevelLearnFromCount = 0x80;
inline constexpr std::size_t kLearnFromStride = 0x58;
inline constexpr std::size_t kLearnFromNeed = 0x20;       // 선행 지식 목록
inline constexpr std::size_t kLearnFromNeedCount = 0x28;
inline constexpr std::size_t kLearnFromTag = 0x44;        // u8, 3 = 스킬 트리 경로
inline constexpr std::size_t kLearnFromMode = 0x45;       // u8, 0 = 전부, 1 = 중 하나
inline constexpr std::size_t kNeedStride = 8;             // {u16 번호 @0, i32 레벨 @4}
inline constexpr std::uint8_t kTagSkillTree = 3;
inline constexpr std::uint8_t kModeAnyOf = 1;

// 훑기 한도. 표가 말이 안 되면 거기서 멈춘다 - 쓰레기 포인터를 따라가지 않는다.
inline constexpr int kKnowMaxCount = 20000;
inline constexpr int kKnowMaxLevels = 64;
inline constexpr int kKnowMaxLearnFrom = 32;
inline constexpr int kKnowMaxNeeds = 64;
// `fresh` 목록의 화면 상한. 넘으면 자르고 개수만 알린다.
inline constexpr int kKnowFreshMax = 300;

// ------------------------------------------------------------------ 자료형

struct KnowMgr {
    std::uintptr_t object = 0;
    std::uintptr_t array = 0;
    int count = 0;
};

struct KnowTable {
    std::uintptr_t comp = 0;
    std::uintptr_t data = 0;
    int count = 0;
    int cap = 0;
};

// 어딘가의 선행 조건으로 요구되는 지식 하나.
struct KnowNeed {
    int number = 0;        // 지식 짧은 번호(표의 색인)
    int need_level = 0;    // 요구 레벨 중 가장 높은 것
    int have_level = 0;    // 지금 레벨
    int wanted_by = 0;     // 이것을 요구하는 (지식, 레벨) 자리의 수
    bool any_of = false;   // "중 하나" 목록에 든 적이 있나
    // 이 지식에 **붙는 스킬**(KnowledgeInfo._learnApplySkillInfo, +0x104).
    // 0xFFFF 면 없다 - 그러면 등록 함수를 불러도 게임 자신이 건너뛴다
    // (RVA 0x02AA643A `cmp word [rax+0x104], 0xFFFF; je`). 선행 조건 노드는
    // "문을 여는 열쇠" 일 뿐 스킬을 안 다는 경우가 많다.
    int skill_key = 0;
    std::string name;      // 현지화 이름(못 풀면 빈 문자열)
};

struct KnowScan {
    bool ok = false;
    const char* skip = "";
    int realm = 0;
    int knowledge = 0;   // 매니저가 든 지식 수
    int learned = 0;     // 레벨 >= 1 인 것
    int walked = 0;      // 실제로 훑은 지식 수
    std::vector<KnowNeed> needs;   // **모자란 것만**, 요구 많은 순
    // **스킬이 붙었는데 아직 안 배운 지식**(선행 조건인지와 무관하게 전수).
    // 선행 조건 목록으로는 목적지에 못 간다는 것이 실측으로 드러나 넣었다
    // (2026-09-14: 레벨 0 인 줄에는 붙는 스킬이 없고, 스킬이 붙은 줄은 이미
    // 배운 것뿐이라 등록해도 맵이 18 -> 18 이었다).
    std::vector<KnowNeed> fresh;
    int fresh_total = 0;   // 잘리기 전 실제 개수
};

// ------------------------------------------------------------ 순수 부분(시험용)

std::uintptr_t know_record(std::uintptr_t data, int n);
// 매니저가 말이 되는 값인가. 게임 갱신으로 RVA 가 밀리면 여기서 걸린다.
bool know_mgr_sane(int count, std::uintptr_t array);
// 같은 번호가 여러 곳에서 요구되면 **가장 높은 요구 레벨**로 합치고 수를 센다.
void know_need_add(std::vector<KnowNeed>* v, int number, int need, bool any_of);
// 요구 많은 순 -> 번호 순. 화면이 흔들리지 않게 완전 순서를 준다.
void know_need_sort(std::vector<KnowNeed>* v);
// 레벨이 충분한 것을 뺀다. 남은 것이 "아직 못 배운 선행 조건" 이다.
void know_need_drop_satisfied(std::vector<KnowNeed>* v);

// ------------------------------------------------------------------ 읽기

bool know_manager(const mem::Reader& reader, KnowMgr* out);
// realm 0 = 서버, 1 = 클라. skillpoint 의 탐색 결과를 그대로 쓴다.
bool know_table(const mem::Reader& reader, int realm, KnowTable* out);
int know_level(const mem::Reader& reader, const KnowTable& t, int n);

// 정적 표를 통째로 훑어 "선행 조건으로 요구되는데 내 레벨이 모자란 지식" 을 모은다.
// rtti 가 널이 아니면 이름도 푼다(현지화). **쓰기 없음.**
KnowScan know_scan(const mem::Rtti* rtti, const mem::Reader& reader, int realm);

// ------------------------------------------------------------------ 쓰기

struct KnowWrite {
    int changed = 0;   // 실제로 쓴 realm 수
    int skip = 0;
    int fail = 0;
    const char* last_skip = "";
};

// 지식 하나를 그 레벨로 만든다(두 realm). 게임이 지식을 지급할 때 하는 쓰기와 같다
// (`ServerKnowledgeActorComponent::Initialize` RVA 0x02AA620C/0x02AA6224):
// 레코드 `+0x00` 에 레벨, `+0x10` 에 1. **`+0x08` 은 건드리지 않는다** - 게임이
// 필요할 때 만들고, 읽는 쪽에 널 가드가 있다.
KnowWrite know_learn(const mem::Reader& reader, int number, int level);

// **되돌리기.** 레벨 0 · 플래그 0 · 습득 시각 0 으로 되돌린다(두 realm).
// `know_learn` 은 레벨 1 미만을 거부하므로 짝이 따로 필요하다 - 세이브에 남는
// 쓰기를 시험하면서 되돌릴 길이 없으면 안 된다.
KnowWrite know_forget(const mem::Reader& reader, int number);

// ------------------------------------------ 스킬 등록 (게임 함수 호출)
//
// **레벨 표만 쓰면 "배운 것처럼 보이기만" 한다.** 실제 기능은 서버 컴포넌트의 별도
// 해시맵이 정한다(실측 2026-09-14: 화면은 활성화로 보이는데 스킬을 못 쓰고 휠도 안 켜진다).
//
//   comp+0xE8 버킷 수 · +0xF4 원소 수 · +0xF8 버킷 배열 · +0x100 값 배열
//   키 = 지식 id, 값 = { u32 SkillKey, i32 레벨 }
//
// 사슬: 진짜 습득이 `0x02AA55D0` 으로 이 맵에 넣고(KnowledgeInfo._learnApplySkillInfo
// `+0x104` 에서 SkillKey 를 꺼낸다), `ServerSkillActorComponent` vtable 슬롯 19
// (RVA 0x02B26DE0)가 그 맵을 훑어 스킬 컴포넌트에 등록한다 - 그때 휠이 켜진다.
//
// 해시맵이라 **손으로 못 쓴다.** 게임 함수를 부르는 수밖에 없는데, 그것은 이 저장소가
// 처음 하는 일이다(현지화조차 게임의 조회 함수를 일부러 안 부르고 직접 걷는다 -
// 그 함수가 읽기인 줄 알고 부르면 표에 항목을 만들기 때문이다). 그래서 관문을 여럿 둔다:
// 서버 vtable 확인 · 표 개수 교차 검증 · 붙을 스킬 유무 확인 · 호출을 SEH 로 감싸기 ·
// 호출 뒤 원소 수가 실제로 늘었는지 확인.
//
// 함수 모양은 프롤로그를 직접 읽어 확인했다(인자 4개, 스택 인자 없음):
//   0x02AA55D0  mov [rsp+0x18], r8d / mov [rsp+0x10], dx / mov [rsp+8], rcx
//   -> (rcx = 서버 컴포넌트, dx = u16 지식키, r8d = i32 레벨, r9b = u8 조용히)
//
// 1.0.0.2949(2026-09-22): **지식 컴포넌트 코드가 통째로 옮겨졌다**(옛 자리는 명령 중간).
// 게임 자신의 호출부로 되짚었다 - 0x2B73B10 의 명령 배열이 2944 호출부(0x02AA6450)와
// 하나하나 같다(`mov ecx,0xffff; cmp word [rax+0x104],cx; je; mov r9b,1; mov r8d,edi;
// movzx edx,..; mov rcx,r13; call`). 같은 함수의 둘째 호출부 0x2B73BB8 과의 간격도
// 2944(0x02AA6450 / 0x02AA64F8)와 같은 0xA8 이다. 대상 0x2B72C90 의 첫 15바이트가 위
// 프롤로그 그대로다. vtable 은 RTTI 로 0x5B2B880 이고, 2949 실행 로그도
// `지식 컴포넌트 … vtable 0x145B2B880` 을 찍었다.
inline constexpr std::uintptr_t kKnowRegisterRva = 0x02B72C90;       // 2944 0x02AA55D0
inline constexpr std::uintptr_t kServerCompVtableRva = 0x05B2B880;   // 2944 0x05A13200

// 등록 함수의 첫 15바이트(위 세 명령). **부르기 전에 이것을 대조한다.** 이 호출은
// 게임 함수를 직접 부르므로, 갱신으로 자리가 낡으면 명령 중간으로 뛰어들어 죽는다 -
// 2949 에서 실제로 옛 자리가 명령 중간이었다. 그때 vtable 대조가 먼저 막아 주었지만
// (vtable 도 옮겼다) 둘이 따로 낡을 수 있으니 자리 자체를 따로 확인한다.
inline constexpr std::uint8_t kKnowRegisterPrologue[15] = {
    0x44, 0x89, 0x44, 0x24, 0x18,    // mov [rsp+0x18], r8d
    0x66, 0x89, 0x54, 0x24, 0x10,    // mov [rsp+0x10], dx
    0x48, 0x89, 0x4C, 0x24, 0x08};   // mov [rsp+8], rcx

// `p` 는 등록 함수 자리에서 읽은 바이트. 모자라거나 다르면 false.
inline bool know_register_prologue_ok(const std::uint8_t* p, std::size_t n) {
    if (p == nullptr || n < sizeof(kKnowRegisterPrologue)) return false;
    for (std::size_t i = 0; i < sizeof(kKnowRegisterPrologue); ++i) {
        if (p[i] != kKnowRegisterPrologue[i]) return false;
    }
    return true;
}
inline constexpr std::size_t kKnowMapCount = 0xF4;      // u32 맵 원소 수
inline constexpr std::size_t kInfoApplySkill = 0x104;   // u16, 0xFFFF = 붙을 스킬 없음
inline constexpr std::uint16_t kNoApplySkill = 0xFFFF;
// `KnowledgeInfo +0x08` = 내부 이름 문자열 객체 포인터("Knowledge_Hp" …).
// 원소 넷을 이름으로 찾을 때 확인한 자리다(STATUS §1.20).
inline constexpr std::size_t kInfoInternalName = 0x08;

struct KnowRegister {
    bool ok = false;
    const char* skip = "";
    int before = 0;        // 맵 원소 수(호출 전)
    int after = 0;         // 호출 뒤
    int skill_key = 0;     // 붙은 스킬 키(진단용)
};

// **서버 컴포넌트에만** 부른다. 성공 판정은 "예외 없이 돌아왔고 원소 수가 안 줄었다" 다.
//
// **한 번 예외가 나면 그 실행 내내 다시 안 부른다.** 해시맵 삽입 도중에 튕기면 맵이
// 반쯤 바뀐 채 남을 수 있고, 그 위에 또 넣는 것이 가장 나쁜 수다. 실측 2026-09-14 에
// 실제로 예외가 났다(4859번, 맵 원소 18에서 호출 직후) - 관문 다섯을 다 지나 호출까지
// 갔으므로 **부르는 방식 쪽에 아직 모르는 전제가 있다.**
KnowRegister know_register_skill(const mem::Reader& reader, int number, int level);

// 예외로 잠겼나. 잠기면 화면이 버튼을 안 그린다.
bool know_register_locked();

// ------------------------------------------ 게임 스레드에 걸어 두기
//
// **등록 함수를 렌더 스레드에서 부르면 안 된다.** 게임 함수 안쪽이 TLS
// (`gs:[0x58] -> [0] -> [+0x250]`)를 따라가는데 렌더 스레드에는 그 블록이 없다.
// 2026-09-14 에 ImGui 버튼에서 바로 불러 **네 번 다 접근 위반**이 났다 - 인자도
// 관문도 다 맞았는데도. 이 저장소가 이미 겪고 적어 둔 함정이다
// (TROUBLESHOOTING 1.8, 그리고 그 재발인 1.13).
//
// 그래서 화면은 **요청만 걸고**, 소환 치트가 쓰는 그 자리 - 액터 조회 디투어의
// `깊이 == 1` + `thread_ready_for_spawn()` - 에서 게임 스레드가 집어 간다.
struct KnowQueue {
    bool pending = false;    // 걸린 요청이 있나
    int number = 0;
    int level = 0;
    bool has_result = false; // 집어 가서 끝났나
    KnowRegister result;
};

// **원자 하나만 읽는다.** 게임 디투어가 매 호출마다 부르는 자리라, 걸린 것이
// 없을 때 뮤텍스도 TLS 검사도 건드리지 않게 하려는 것이다.
bool knowledge_has_pending();

// 화면이 부른다. 이미 걸린 것이 있으면 거짓.
bool knowledge_queue_register(int number, int level);
// **게임 스레드에서만** 부른다(TLS 가 선 자리). 걸린 것이 없으면 즉시 반환.
void knowledge_run_pending();
KnowQueue knowledge_queue_state();
void knowledge_queue_clear_result();

// 컴포넌트 `+0x08` = 소유 액터. **등록 함수가 초입에서 이것을 역참조한다**:
//   0x02AA5421 mov rax,[rcx+8] / 0x02AA5425 lea rdx,[rax+8]
//   0x02AA542C cmove rdx,0     / 0x02AA5433 mov rdx,[rdx]   <- 0 이면 여기서 죽는다
// 첫 시도에서 예외가 난 자리로 가장 유력하다(실측 2026-09-14).
inline constexpr std::size_t kKnowCompOwner = 0x08;

// **읽기만 하는 진단.** 무엇이 어긋났는지 로그로 남긴다 - 추측으로 또 부르지 않기 위해서다.
void know_diagnose(const mem::Reader& reader);

// 이름이 안 풀릴 때 쓰는 진단. 지역화 시스템 상태와, 레벨 데이터의 이름 후보
// 자리들을 **날바이트로** 찍는다. `_knowledgeName +0xA8` 이 u64 현지화 키라는 것은
// 아직 **가정**이고, 2026-09-14 에 이름이 하나도 안 나왔다.
void know_diagnose_names(const mem::Rtti* rtti, const mem::Reader& reader,
                         int number);

// ------------------------------------- 스킬 맵 읽기 · 호출 지식 (2026-09-16)
//
// **왜 이것이 있나.** 드래곤 호출 모션은 **스킬**이고 그 스킬은 **지식**이 준다
// (게임 데이터 실측, `docs/superpowers/specs/2026-09-16-dragon-external-research.md`):
//
//   Knowledge_CallVehicle -> Skill_CallVehicle  "탈승물 호출"   <- 말. 잘 된다
//   Knowledge_CallDragon  -> Skill_CallDragon   "용을 호출한다" <- 드래곤. 안 된다
//
// 정본이 남긴 마지막 벽이 "7시 슬롯 경로에 호출 모션이 아예 없다" 인데, 모션이
// 스킬이라면 **그 스킬이 안 붙어서**일 수 있다. 여기 있는 것은 그것을 **쓰지 않고
// 확인하는** 조회다 - 세이브에 남는 쓰기를 헛되이 쓰지 않으려는 것이다.
//
// 읽는 방법에서 **버킷·해시를 가정하지 않는다.** 값 배열은 원소 수(+0xF4)와
// 배열 포인터(+0x100)만으로 조밀하게 읽히므로 추측이 하나 적다. 버킷 배치
// (+0xE8/+0xF8)는 다른 엔진 맵에서 본 모양일 뿐 **이 맵에서 확인한 적이 없다.**
//
// **값 배열은 포인터 배열이다**(실측 2026-09-16, 탐침). STATUS §1.19 가
// "값 = {u32 SkillKey, i32 레벨}" 이라 적은 것은 **한 겹 얕았다** - 그 쌍은
// 배열에 바로 있지 않고 포인터가 가리키는 객체 안에 있다. 첫 판을 그렇게 읽어
// 대조군까지 "없음" 으로 나왔고, 가드가 그것을 잡았다.
//
// 항목 하나(실측으로 확정 - 지식 키 1000000 `Knowledge_Wrestle` ->
// 스킬 키 10202 `Skill_Wrestle` 레벨 5 가 게임 데이터와 정확히 맞는다):
//
//   +0x00 u32 ?        0 또는 31 - 아직 모른다
//   +0x04 u32 **지식 키**  KnowledgeInfo._key. **행 번호가 아니다**
//   +0x08 u32 **스킬 키**  SkillInfo._key
//   +0x0C i32 **레벨**
//   +0x10 · +0x18 u64  시각으로 보이는 값 둘
inline constexpr std::size_t kKnowMapValues = 0x100;   // 항목 **포인터** 배열
inline constexpr std::size_t kKnowMapValueStride = 8;  // 포인터 하나
inline constexpr int kKnowMapMaxCount = 1 << 16;       // 말이 안 되는 개수를 거른다
inline constexpr std::size_t kSkillEntryKnowKey = 0x04;
inline constexpr std::size_t kSkillEntrySkillKey = 0x08;
inline constexpr std::size_t kSkillEntryLevel = 0x0C;
inline constexpr std::size_t kInfoDataKey = 0x00;      // KnowledgeInfo._key

struct KnowSkillSlot {
    int know_key = 0;
    int skill_key = 0;
    int level = 0;
};

// 순수 - 시험한다. 맵 머리가 말이 되는가.
bool know_map_sane(int count, std::uintptr_t values);
// 순수 - 시험한다. 값 하나의 주소. 말이 안 되면 0.
std::uintptr_t know_map_value(std::uintptr_t values, int index);

// 값 배열을 통째로 읽는다. **쓰기 없음.** 컴포넌트를 직접 받는 쪽이 본체이고
// 시험 대상이다 - realm 판은 전역 게터만 씌운 껍데기다.
//
// **못 읽는 칸에서 멈춘다.** 개수가 말하는 만큼 다 못 읽어도 0 으로 채우지
// 않는다 - 그러면 "스킬 0 이 등록돼 있다" 를 답으로 내게 된다.
bool know_skill_slots_at(const mem::Reader& reader, std::uintptr_t comp,
                         std::vector<KnowSkillSlot>* out);
bool know_skill_slots(const mem::Reader& reader, int realm,
                      std::vector<KnowSkillSlot>* out);

// `KnowledgeInfo._learnApplySkillInfo`(+0x104, u16). 0xFFFF 면 붙을 스킬이 없다.
// 못 읽으면 -1. **둘을 갈라 쓴다** - 첫 판은 로그에서 둘 다 -1 로 찍어 어느
// 쪽인지 알 수 없었다.
int know_apply_skill(const mem::Reader& reader, const KnowMgr& mgr, int number);

// `KnowledgeInfo._key`(+0x00). 맵은 행 번호가 아니라 이 키로 색인한다.
// 못 읽으면 0.
int know_data_key(const mem::Reader& reader, const KnowMgr& mgr, int number);

// 내부 이름(`KnowledgeInfo +0x08`)으로 찾는다. 못 찾으면 -1.
// **6천 개를 도는 일이다** - 프레임마다 부르지 않는다.
int know_find_by_name(const mem::Reader& reader, const KnowMgr& mgr,
                      const char* internal);

// 호출 지식과 **대조군 둘**.
//
//   [0] Knowledge_Wrestle     맵-양성 대조군 - 이 맵에 실제로 들어 있다.
//                             여기서 "없음" 이 나오면 **조회가 틀린 것**이다
//   [1] Knowledge_CallVehicle 기능-양성 대조군 - 말이 잘 불린다
//   [2] Knowledge_CallDragon  대상
//
// 대조군을 둘 둔 이유: 첫 판에서 "대조군이 맵에 없다" 가 *조회가 틀렸다* 는
// 뜻인지 *호출 스킬은 이 맵을 안 탄다* 는 뜻인지 가릴 수가 없었다. 맵에 확실히
// 있는 것을 하나 끼워 두면 그 둘이 갈린다.
//
// **번호를 코드에 안 박는다** - 게임이 갱신되면 밀리므로 이름으로 찾는다
// (원소 넷에서 번호를 잘못 골라 여러 번 헛돌았다, STATUS §1.20).
inline constexpr int kCallKnowCount = 3;
// 화면이 **쓰기를 그리는 칸.** 대조군 둘에는 손대지 않는다.
inline constexpr int kCallKnowDragon = 2;

struct CallKnow {
    const char* label = "";
    const char* internal = "";
    int number = -1;        // 지식 번호(표의 행). -1 이면 못 찾았다
    int data_key = 0;       // KnowledgeInfo._key. 맵이 쓰는 것은 이쪽이다
    int level = 0;          // 레벨 표의 지금 레벨
    int apply_skill = -1;   // 붙을 스킬 키. 0xFFFF = 없음, -1 = 못 읽음
    bool in_skill_map = false;  // 서버 스킬 맵에 그 지식이 있는가
    int map_skill = 0;          // 있으면 거기 적힌 스킬 키
    int map_level = 0;          // 있으면 거기 적힌 레벨
};

// 둘을 이름으로 찾아 레벨·붙을스킬·맵 등록 여부까지 채운다. **읽기만 한다.**
bool call_knowledge(const mem::Reader& reader, CallKnow out[kCallKnowCount]);

// 위를 로그로 찍는다(맵 머리와 표본 포함). **읽기만 한다.**
void know_diagnose_call(const mem::Reader& reader);

// ------------------------------------------------------------ 자동 재적용
//
// **지식 레벨 쓰기는 저장을 넘는다 - 게임에서 저장하면.** (실측 2026-09-15:
// 적용 -> 저장 안 하고 종료 -> 적용 -> **저장하고** 종료 -> 재시작 -> **남아 있다.**)
// 쓴 값은 살아 있는 컴포넌트 표에 들어가고, 게임이 저장할 때 그 표를 그대로 내려
// 적는다. 그러니 저장을 안 하고 끄면 안 남는 것이 당연하다.
//
// 2026-09-14 에 "저장을 못 넘는다" 고 적었던 것은 **틀렸다** - 그때 건 것들(5089,
// 4712)은 저장을 안 하고 껐던 것이다. 한 번 더 재기 전에는 "안 남는다" 라고 적을 수
// 없었다(TROUBLESHOOTING 3.8).
//
// 그래도 재적용은 남긴다: 저장을 잊고 끄는 일이 있고, 목록은 ini(`knowledge_keep`)에
// 남으므로 다음 실행이 알아서 다시 건다. 사람이 다시 누를 일을 없앤다.
//
// 가방과 다른 점 하나: 여기서는 **세대 카운터가 필요 없다.** 레벨은 단조 증가로만 쓰고
// (모자랄 때만, 낮추는 일이 없다) 검사가 값싸므로, 매 바퀴 "모자란가" 만 보면 된다.
// 그래서 리로드든 재탐색이든 주소가 어떻게 바뀌든 저절로 따라간다.
struct KnowWant {
    int number = 0;
    int level = 0;
};

// 같은 번호면 **높은 레벨**로 합친다. 순수 - 시험한다.
void know_auto_upsert(std::vector<KnowWant>* v, int number, int level);

void know_auto_remember(int number, int level);
void know_auto_forget();

// 목록이 바뀔 때 부를 것. 화면 층이 ini 에 적어 두려고 건다 - 이 파일이 설정
// 층을 직접 알면 층이 뒤집힌다. **자물쇠 밖에서** 부르므로 훅 안에서
// `know_auto_list()` 를 불러도 된다.
using KnowAutoPersist = void (*)();
void know_auto_persist_hook(KnowAutoPersist fn);
std::vector<KnowWant> know_auto_list();

// 분석 스레드에서 부른다. 기억해 둔 것이 모자라면 다시 건다 - 충분하면 읽기만 하고
// 아무것도 안 쓴다(평소에는 로그도 안 남는다).
void know_auto_tick(const mem::Reader& reader);

}  // namespace cdtb::game

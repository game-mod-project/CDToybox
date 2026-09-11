# 오버레이 UI/UX 개선 — 설계 (2026-09-10)

워크스트림 C. 창 8개를 코드 리뷰 2건(opus, 112건)과 실제 화면(창 8개 · 로스터 7탭 ·
팝업 2개, 게임 실행 중 PrintWindow/PostMessage 로 캡처)으로 본 뒤 42건을 골라 세 단계로
고친다. 소견 원문은 세션 스크래치패드 `uiux/findings-merged.md`(T1~T5 표) 에 있고, 이
문서는 **무엇을 어떻게 바꾸는지**만 적는다.

## 0. 진단 요약

- **확정 결함(T1, 13건)**: 단축키 안내 두 개가 틀림(End→실제 F10, F9 는 보류 기능) ·
  버전 하드코딩 · **장비 창 연마 숫자 칸 폭 0(값이 안 보이고 못 친다)** · 진단 오류
  분기가 창 8개를 전부 숨김 · 로스터/장비/플레이어에 기본 위치가 없어 첫 실행에 본창
  위에 포개짐 · 인벤 바닥 안내가 '소켓 5칸'과 모순 · 체크박스 라벨≠제목 · 지급 '떨구기'
  가 `spawn_ready()` 를 안 봐 "2초" 거짓 진단 · 실패 메시지가 초록 · 비활성 '획득' 툴팁이
  절대 안 뜸 · 라벨이 곧 ID 인 위젯 둘 · 카메라 창 정렬/설명/어미 · "(실험)"·"사이트
  재추출" 문구.
- **쓰기 안전장치(T2, 7건)**: 종 고르기 줄 클릭이 곧 게임 메모리 쓰기(확인 없음) ·
  '소켓 5칸'·'전부 연마/소켓' 확인 없음 · 장비/플레이어/소켓 쓰기에 로그 없음 · 쓰기
  버튼 툴팁 규격 없음 · '같은 타입만' 매번 초기화 · guard 문구가 아무것도 안 막음.
- **보관함 저장 경로(T3, 6건)**: ★·'보관함에' 가 보관함을 바꾸는데 저장은 보관함 창
  버튼뿐(자동 저장 없음) → 조용히 유실 · 일괄 지급 큐가 창을 닫으면 멈추고 세션이 없으면
  통째로 버려짐 · `g_open_set` 낡은 인덱스 · 세트 지우기 확인 없음 · 즐겨찾기가 세트를
  스크롤 밖으로 밀어냄 · 로그 없음.
- **레이아웃·빈 상태·일관성(T4, 16건)**: 빈 결과 문구 없음 · 로딩 게이트가 창마다 다름 ·
  장비 창 한 줄이 5줄 높이 · 보석 팝업에 대상 없음 · 인벤 이름 칸 좁음 · 창 하한이
  아이템 목록에만 · 결과 줄이 영원히 남음 · 색 3종/2종 · 용어 7가지 · RE-EQUIP/realm
  노출 · 로스터 탭 순서 · 공유 검색창 · 매 프레임 선형 탐색.

## 1. 목표 · 비목표

**목표.** 창 8개가 같은 색·같은 결과 줄·같은 확인 방식·같은 로딩/빈 상태 표시·한 배치
표를 쓰게 하고, 위 42건을 없앤다. 게임 메모리에 쓰는 모든 동작은 (1) 확인 한 단계를
거치고 (2) 로그 한 줄을 남긴다.

**비목표.** 창 틀(Window 기반 클래스) 도입, 패널 파일 분해(워크스트림 D), 새 기능,
`freecam`·명령 파일·캡처 하위시스템 변경. T5(Low 25건)는 손대지 않는다 — 지나는 길에
같은 줄을 고치게 될 때만 함께 한다.

**결정 사항(사용자).** 단계 순서 1) T1+T2 → 2) T4 → 3) T3. 구조는 "공용 조각 + 창별
적용". 확인 방식은 **2단 버튼**(버튼) + **선택→적용**(고르기 팝업). 보관함은 **자동
저장**. 로스터 탭은 자주 쓰는 것을 앞으로.

## 2. 공용 조각

새 파일은 `CMakeLists.txt` 목록에 직접 넣는다(GLOB 없음). ImGui 를 쓰지 않는 순수
로직은 따로 두어 `cdtb_tests` 에도 넣는다(워크스트림 A 의 `game/item_view` 방식).
문구는 전부 "-습니다" 체.

### 2.1 `render/colors.h` — 상태 색 4개

```cpp
namespace cdtb::render::col {
inline constexpr ImVec4 kOk{0.40f, 0.85f, 0.40f, 1.0f};    // 성공 · 활성 · 걸려 있음
inline constexpr ImVec4 kWarn{0.90f, 0.60f, 0.30f, 1.0f};  // 막힘 · 경고 · 미저장 · 한쪽만
inline constexpr ImVec4 kBad{0.95f, 0.35f, 0.35f, 1.0f};   // 실패 · 치명 · 죽은 세션
inline constexpr ImVec4 kBusy{1.00f, 0.90f, 0.40f, 1.0f};  // 로딩 · 진행 중
}
```

창 8개의 `ImVec4(...)` 리터럴을 전부 이것으로 바꾼다. 회색은 `TextDisabled` 그대로.

### 2.2 `render/notice_state.h` + `render/notice.{h,cpp}` — 결과 줄

```cpp
// notice_state.h (ImGui 없음)
enum class NoticeLevel { Ok, Warn, Bad, Info };
struct Notice { NoticeLevel level = NoticeLevel::Info; char text[200] = ""; double at = -1.0; };
enum class NoticeAge { Fresh, Faded, Gone };
NoticeAge notice_age(const Notice& n, double now, double fade_after = 10.0, double hide_after = 60.0);
void notice_clear(Notice* n);

// notice.h
template <class... A>
void notice_set(Notice* n, NoticeLevel lv, std::format_string<A...> f, A&&... a);  // at = ImGui::GetTime()
void notice_draw(const Notice& n);   // Fresh = 등급 색, Faded = TextDisabled, Gone = 안 그림
```

창마다 `Notice g_notice` 하나가 `g_msg`(장비) · `g_species_msg`(로스터 팝업) ·
`g_near_busy_why`/`g_item_busy_why`(로스터 탭) · 인벤 `g_status` 의 오류 겸용 · 지급
결과 분기(`grant_panel.cpp:535–561`)를 대신한다. 실패는 반드시 `Bad`, 밀림/한쪽만은
`Warn`. `at < 0` 이면 Gone.

### 2.3 `render/confirm_state.h` + `render/confirm.{h,cpp}` — 2단 버튼

```cpp
// confirm_state.h (ImGui 없음)
struct ConfirmState { unsigned id = 0; double armed_at = -1.0; };
enum class ConfirmStep { Idle, Armed, Fired };
// clicked: 이 프레임에 그 버튼이 눌렸는가. window 초가 지나면 스스로 풀린다.
// 다른 id 가 무장하면 앞의 것은 풀린다(한 번에 하나만).
ConfirmStep confirm_step(ConfirmState* s, unsigned id, bool clicked, double now, double window = 3.0);
double confirm_left(const ConfirmState& s, double now, double window = 3.0);   // 남은 초, 0 이면 풀림

// confirm.h — 상태는 confirm.cpp 의 전역 하나
bool confirm_button(const char* label, ImVec2 size = ImVec2(0, 0));
bool confirm_small_button(const char* label);
```

동작: 첫 클릭에 라벨이 `정말? (3초)` → `(2초)` … 로 바뀌고(같은 ID 유지 — `###` 사용),
그 안에 다시 누르면 true 를 한 번 내고 풀린다. `guard::is_safe_to_modify()` 가 false 면
비활성 + 툴팁 "쓰기 기능이 잠겨 있습니다". 적용처는 3절·4절·5절에 적는다.

고르기 팝업(종 바꾸기 · 보석)은 확인 버튼 대신 **선택→적용**: 줄 클릭은 선택(강조)만,
표 아래 `선택: <이름> (행 N)` + `[적용]`(선택 없으면 비활성). 적용 뒤에도 팝업은 열려
있고 결과는 팝업 안 Notice 로 낸다.

### 2.4 `render/gates.{h,cpp}` — 로딩 · 빈 상태

```cpp
// ready 가 false 면 점 애니메이션 + kBusy "<what> 읽는 중..." (+ "(done / total)") +
// 고정 문단 "월드 진입 후 자동으로 채워집니다 (보통 5~10초). 이 표시가 사라지지 않고
// 계속 남아 있으면 로드 실패입니다." 를 그리고 false 를 낸다(부른 쪽이 End 한다).
bool loading_gate(bool ready, const char* what, std::size_t done = 0, std::size_t total = 0);
// 표 안에 열을 가로지르는 한 줄. action 이 있으면 SmallButton 을 붙이고 눌리면 true.
bool table_empty_row(int cols, const char* text, const char* action = nullptr);
```

`item_panel.cpp:153–172` 의 지금 구현을 그대로 옮긴 것이다(아이템 표 → 이름 두 단계).

### 2.5 `render/layout_table.{h,cpp}` + `render/layout.h` — 창 배치 표

```cpp
// layout_table.h (ImGui 없음)
enum class Win { Main, Items, Grant, Stash, Inventory, Roster, Equip, Player, Camera, Count };
struct WindowSpec {
    Win id; const char* title; const char* label;   // label = 본창 체크박스 글자
    bool default_open; float x, y, w, h, min_w, min_h;
};
const WindowSpec& window_spec(Win w);
std::span<const WindowSpec> window_specs();
bool specs_overlap(const WindowSpec& a, const WindowSpec& b);

// layout.h
// SetNextWindowPos/Size(FirstUseEver) + SetNextWindowSizeConstraints + Begin(title, open).
bool begin_window(Win w, bool* open);
```

| Win | title | label | 기본 | 위치 | 크기 | 하한 |
|---|---|---|---|---|---|---|
| Main | CDToybox | — | 열림 | 60,60 | 420×260 | 420×200 |
| Items | 아이템 목록 | 아이템 목록 | 열림 | **480**,60 | 760×520 | 430×240 |
| Grant | 아이템 지급 | 아이템 지급 | 열림 | 1240,60 | 440×260 | 420×260 |
| Stash | 보관함 | 보관함 | 열림 | 1240,340 | 420×400 | 420×300 |
| Inventory | 인벤토리 | 인벤토리 | 열림 | 480,600 | 760×420 | 720×300 |
| Roster | 탈것 · 용병 · 캐릭터 | 탈것·용병·캐릭터 | 닫힘 | 60,340 | 560×520 | 560×360 |
| Equip | 장비 소켓 · 연마 · 염색 | 장비 소켓·연마·염색 | 닫힘 | 640,340 | 560×420 | 560×300 |
| Player | 플레이어 치트 | 플레이어 치트 | 닫힘 | 1240,760 | 320×220 | 320×220 |
| Camera | 카메라 분석 | 카메라 분석 (진단) | 닫힘 | 60,870 | 560×200 | 400×160 |

불변식(테스트): 기본 열림 4창 + 본창은 서로 안 겹친다 · 닫힘 4창은 본창을 안 덮고 서로
안 겹친다 · 전부 1920×1080 안 · title 은 유일하고 본창을 뺀 창의 label 이 비지 않는다.
label 도 본창을 뺀 창끼리 유일하다.

`overlay.cpp` 의 `WindowFlags g_show` 는 `bool g_show[Win::Count]` 가 되고 초기값은
`default_open`. `overlay::show_window(Win)` 을 노출한다(아이템 목록이 지급 창을 열 때).

### 2.6 `core/write_log.h` — 쓰기 로그 규칙

```cpp
// "쓰기 <what>: 0x<target> <before> -> <after>" 한 줄. 게임 메모리를 바꾸는 함수 안에서 부른다.
void log_write(std::string_view what, std::uintptr_t target, std::string_view before, std::string_view after);
```

적용처: `game::eq_write_socket/eq_write_refine/eq_write_dye/eq_unlock_sockets`,
`socket_unlock_record`, `player_set_godmode/inf_stamina/inf_spirit`, `nofall_set`.
`apply_species`(로스터)·`socket_cap_apply`(아이템표)는 이미 남긴다 — 형식만 맞춘다.
창이 아니라 `game::` 안에 두어 명령 파일로 조작해도 남는다.

### 2.7 헬퍼

- `core/vk_name.{h,cpp}`: `const char* vk_name(int vk, char* buf, std::size_t n)` — Insert,
  Delete, Home, End, PgUp, PgDn, F1~F24, 숫자·영문, 화살표; 나머지는 `buf` 에 `0x%02X`.
- `core/file_version.{h,cpp}`: `bool file_version_string(const std::wstring& path, std::string* out)`
  — `GetFileVersionInfoW` + `VS_FIXEDFILEINFO` → `"1.0.0.2760"`. `cdtb_core` 에 `version.lib`
  링크. 오버레이는 게임 exe(`GetModuleFileNameW(nullptr)`) 로 한 번 읽어 둔다.
- `game::item_by_key(std::uint32_t) -> const ItemCatalogEntry*` (`items.h`): 키→엔트리
  해시맵. 카탈로그 판(`item_catalog().data()`)이 바뀌면 재구성. 2단계에서 만든다.
- `scripts/overlay-check.ps1`: 게임을 포그라운드로 가져오지 않고 오버레이를 캡처·클릭하는
  검증 도구(PrintWindow + PostMessage `[MOUSEMOVE, WM_CHAR(1), LBUTTONDOWN, LBUTTONUP]`).
  스크래치패드의 `cdclick.ps1` 을 옮기고 사용법 주석을 단다. 1단계에서 넣는다.

## 3. 1단계 — 확정 결함(T1) + 쓰기 안전장치(T2)

브랜치 `feat/uiux-overlay`(워크트리 `E:/CDToybox-sdd`). 2.1~2.7 을 만들고 창 8개에
적용한다. `item_by_key` 만 2단계로 미룬다.

**본창 (`overlay.cpp`)**
- 체크박스 8개를 배치 표의 `label` 로, 2열 격자(`SameLine(200)`). 순서: 아이템 목록 ·
  아이템 지급 / 보관함 · 인벤토리 / 탈것·용병·캐릭터 · 장비 소켓·연마·염색 / 플레이어
  치트 · 카메라 분석 (진단).
- 단축키 줄: `vk_name(toggle_key)` + " 토글 · " + `vk_name(unload_key)` + " 비활성화".
  프리카메라 항목 삭제.
- 버전 줄: `"Crimson Desert " + file_version_string(게임 exe) + " / %.1f FPS"`, 실패면
  `"Crimson Desert (버전 확인 불가) / %.1f FPS"`.
- 진단 오류 분기: `ImGui::End(); draw_windows(); return;`. `모듈`·`스캐너 진단` 헤더는
  기본 접힘(`DefaultOpen` 제거).
- "쓰기 기능이 잠겨 있습니다" 줄은 남기고(`col::kWarn`), 실제 잠금은 `confirm_button`
  이 한다. `guard.h` 주석을 현재 동작("지금은 항상 true, 멀티플레이가 오면 여기 한 곳")
  으로 고친다.

**아이템 지급 (`grant_panel.cpp`)**
- `blocked` 판정에 추가: `!game::give_ready()` → "지급 경로(후킹)가 아직 준비되지
  않았습니다". 떨구기 전용 `blocked_spawn`: `!game::spawn_ready()` → "바닥 스폰 경로가
  아직 준비되지 않았습니다". `BeginDisabled` 는 각자의 이유 하나로.
- 결과 분기(535–561)를 `Notice` 로: 성공 Ok, 액터 없음 Warn, 게임 안 예외 Bad,
  밀림 "연달아 누르면 잠시 막힙니다 (2초)" 는 Warn 이고 `spawn_ready` 가 참일 때만.
- 머리줄: 등급이 없으면 가운데를 빼고 "화살 · 탄환".

**인벤토리 (`inventory_panel.cpp`)**
- 바닥 안내: "개수·담금질·연마는 게임이 되쓰므로 지급 칸으로 옮겨 새로 지급하세요." /
  "소켓 열기만은 제자리로 되고 저장까지 남습니다."
- `소켓 5칸` → `confirm_small_button`. 툴팁 끝에 "게임 세이브에 남고 되돌릴 수
  없습니다." 한 줄.
- `g_status`(가방 N개, 아이템 M개)와 `Notice g_notice`(마지막 동작 결과) 분리.
- "소켓 상한 (실험)" → "소켓 상한".

**탈것 · 용병 · 캐릭터 (`roster_panel.cpp`)**
- 종 바꾸기 팝업: `int g_species_pick = -1`(hits 색인이 아니라 **행 번호**), 줄
  `Selectable(selected = row == pick)`. 표 아래 `선택: <이름> (행 N)` + `[바꾸기 적용]`
  → `apply_species`. `g_species_msg` → `Notice`(RTTI 전·자리 없음·쓰기 실패·확인 어긋남 =
  Bad, 바꿨습니다 = Ok). `바꾸기` 버튼이 `g_species_same_type` 을 초기화하지 않는다;
  "타입을 넘는 교체도 됩니다 - 펫→특수 탑승물 확인됨" 은 `TextDisabled`.
- `바꾸기` 툴팁: "명부 레코드의 종을 그 자리에서 고쳐 씁니다.\n저장·리로드에 남습니다.\n
  되돌리려면 원래 종으로 다시 바꾸세요."
- 비활성 `획득`: `IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)`; 이유별 툴팁 —
  소유("이미 임자가 있어 등록되지 않습니다…" 기존 문구) / 동반자 아님("동반자로 등록할
  수 있는 종이 아닙니다") / 핸들 없음("액터 핸들이 아직 없습니다 - 잠시 뒤 다시") /
  준비 전("획득 경로가 아직 준비되지 않았습니다").
- `g_near_busy_why`·`g_item_busy_why` → 탭별 `Notice`(성공 Ok, 세션 없음·밀림 Warn).
- 체크박스 ID 고정: `"용병대원 (%zu)###clan_people"`, `"탈것·펫 (%zu)###clan_mount"`,
  `"시스템 (%zu)###clan_system"`. `g_tab` → `enum class RosterTab` (주석 갱신).

**장비 소켓 · 연마 · 염색 (`equip_panel.cpp`)**
- 연마 칸: `SetNextItemWidth(100)`, `연마` 열 100 → 150.
- `g_msg` → `Notice`. `전부 연마 10`·`전부 소켓 5칸` → `confirm_button`; `done+part == 0`
  이면 Warn "쓴 것이 없습니다 (대상 없음)" 이고 재장착 안내를 붙이지 않는다; `part > 0`
  이면 Warn "N개는 한쪽만 적용됐습니다 - 다시 시도하세요".
- `열기`·`비우기` → `confirm_small_button`.
- 문구: "RE-EQUIP 하면 보입니다" → "벗었다 다시 착용하면 화면에 반영됩니다";
  "(%d realm)"/"both-realms" → "클라·서버 모두".
- 보석 팝업: `GemPickerOpts.title` 을 `"<장비명> 소켓 k"` 로, `selected_key` 에 현재 보석
  키. `gem_picker` 를 선택→적용으로 바꾼다(`GemPicker` 에 `int pick = -1`, 표 아래
  `선택: <이름>` + `[적용]`; 지급 창도 같은 동작).

**플레이어 치트 (`player_panel.cpp`)**
- "이 게임 빌드 미지원 (사이트 재추출 필요)" → "이 게임 빌드에서 훅 지점을 못 찾았습니다
  (모드 업데이트가 필요합니다)". "(한 번 낙하해야 학습)" → "(첫 낙하 한 번은 피해를
  받습니다 - 그때 대상을 학습)". "무적 (Godmode)" → "무적", "낙사 방지 (No Fall Damage)"
  → "낙사 방지".
- `v.ok` 가 아니면 "게이지를 읽지 못했습니다 (지역 이동·캐릭터 전환 중일 수 있습니다)".

**카메라 분석 (`scan_panel.cpp`)**
- 첫 줄 `TextDisabled("개발 진단용입니다 - 평소에는 열 필요 없습니다.")`. 주소 5줄은
  2열 표(`SizingFixedFit`). "분석 중..." 에 경과 초(`"분석 중... %.0f초"`). "프리카메라:
  보류 - 렌더가 읽는 값을 아직 못 찾았다" → "…아직 못 찾았습니다". `(F9)` 삭제.

**쓰기 로그** — 2.6 의 적용처 전부.

**테스트(`cdtb_tests`)** — `notice_age` 경계(10/60초, at<0) · `confirm_step`(무장→발화,
만료, 다른 id 가 무장하면 풀림, Idle 에서 clicked 없음) · `vk_name` 표(0x2D, 0x79,
0x23, 0x41, 0x70, 미지 0xE7) · 배치 표 불변식 4개 · `file_version_string`
(`kernel32.dll` 로 `\d+\.\d+\.\d+\.\d+`, 없는 파일이면 false).

## 4. 2단계 — 레이아웃 · 빈 상태 · 일관성(T4)

브랜치 `feat/uiux-p2`(develop 에서 새로). 새 조각은 둘.

- **`game::item_by_key`**(2.7): `inventory_panel.cpp:109–129` 의 `key2ent` 를 `items.cpp`
  로 옮긴다. 지급 창 `selected_item()`(71–77)·소켓 칸(137)·보관함 `find_item`(108)·
  `max_stack_of`(131)·보석 팝업이 쓴다.
- **로스터 뷰 캐시**: `struct RosterViewKey { RosterTab tab; std::string query; bool wild_only,
  hirable_only, same_type; std::size_t generation; }` — 키가 바뀔 때만 `hits` 재구성.
  종 팝업의 `hits` 도 같은 방식.

창별:
- **빈 상태**: 아이템 목록·인벤·로스터 표(동반자·근처·탈것·용병 타입·캐릭터·내 동반자)에
  `table_empty_row` — 질의가 있으면 "검색어 때문에 비어 있습니다" + `[지우기]`(질의를
  비운다), 없으면 "조건에 맞는 항목이 없습니다".
- **로딩 게이트 통일**: 지급(세션 0개)·보관함(`items_named` 전)·동반자 아이템 탭이
  `loading_gate` 를 쓴다. 지급 창은 월드 밖에서도 고른 아이템·키·개수·담금질·소켓 칸은
  그리고, 그 아래(막힌 이유·버튼·고급)는 게이트 문구가 대신한다(구현 판정 — 죽은 버튼과
  게이트가 중복). 동반자 아이템 탭에 `draw_companion_drive_gate(DriveLane::Item)` —
  지급 버튼은 Item 레인이라 Companion 레인을 보면 반대로 오해한다(최종 리뷰 정정).
- **장비 창 표**: 열 = 부위(카탈로그 `category_name`) · 장비 · 연마 · 소켓 · 염색, `Resizable`,
  장비명 툴팁. 소켓은 칸마다 `SmallButton` 하나(`"0 심연의 광휘"` / `"3 비어 있음"` /
  `"4 잠김"`)를 `flow_same_line` 으로 흘린다. 누르면 **칸 팝업** `"소켓##equip_socket"`:
  잠김이면 `confirm_button("이 칸 열기")`; 열려 있으면 보석 목록(선택→적용) + 박혀 있으면
  `confirm_small_button("비우기")`. 이를 위해 `gem_picker.cpp` 를 목록
  (`gem_list_draw(GemPicker*, const GemPickerOpts&, GemChoice*)` — 검색·표·선택·적용 버튼)
  과 팝업 껍데기(`gem_picker_draw`)로 나눈다. 보석 이름은 `grade_color`.
- **인벤토리 표**: 버튼 라벨 `지급`·`보관`·`소켓 5칸`(뜻은 툴팁), 버튼 열 190 → 160
  (120 은 세 버튼이 안 들어간다 — 판정), `이름` 열 stretch 가중치 2.0. `보관` 은 라벨을
  고정하고 목적지 세트 이름을 툴팁 `보관함의 '<세트>' 에 담습니다` 로(판정); 담은 뒤
  `Notice` "‘<이름>’ 을 <세트> 에 담았습니다". `걸기` 는 규칙 0개면 비활성. 소켓 상한
  안내에 "이미 가진 아이템은 표 아래 줄의 `소켓 5칸` 으로 레코드도 함께 열어야 합니다."
- **지급 창**: 버튼 줄에 `flow_same_line`. `고급` 세션 표 `ScrollX`, `short_class` 가
  `@` 앞에서 자른다. `request_give`/`request_spawn` 로그에 `key={} count={}`.
  `SpawnOutcome` 에 `std::uint32_t serial`(요청마다 증가, `last_request_serial()`), 지급
  창은 자기 serial 의 결과만 Notice 로.
- **아이템 목록**: `★` 머리글 툴팁 "보관함 즐겨찾기 - 보관함 창에 모입니다"; 줄 클릭 시
  지급 창이 닫혀 있으면 `overlay::show_window(Win::Grant)`; 정렬 해제(`SpecsCount == 0`)
  는 `ItemSort::Key` 오름차순으로 되돌린다; `SmallButton(fav ? "★##fav" : "☆##fav")`.
- **로스터**: 탭 순서·이름 = `내 동반자` · `근처` · `동반자 아이템` · `포획 가능 종`(구
  '동반자') · `탈것` · `캐릭터` · `용병 타입`. 머리 문구에 범례 "키=캐릭터 키 · 행=표 행 ·
  번호=명부 번호 · 핸들=액터". 탭을 바꾸면 검색어를 비운다. `새로고침` 옆 `TextDisabled
  ("자동 2초")`. 종 팝업 표 폭 = `min(620, 부모 창 폭 - 40)`. `"%zu / %zu 명"` → `"개체"`.
- **색·문구**: 남은 리터럴 색 전부 `col::`. 어미 통일. 장비 창 `(순번 %u)` → `(카탈로그
  순번 %u)`.
- **본창**: 쓰기 창(로스터·장비·플레이어·인벤토리 — 소켓 열기·상한도 쓰기다) 체크박스 옆
  `*` 표식 + 툴팁 "이 창은 게임 메모리를 바꿉니다" + 격자 아래 범례 한 줄. 처음의
  `(쓰기)` 는 2열 격자(200px)를 넘어 옆 열과 겹쳤다(최종 리뷰 정정).
- **1단계 최종 리뷰 이월**: 인벤 소켓 상한의 `g_cap_note` → Notice; 근처 탭 획득 성공을
  Ok 로; `apply_species` 를 `game::` 로 옮겨 명령 파일 경로도 같은 로그; 로스터 데이터
  표(탈것·용병 타입·캐릭터) 안내 어미 통일; `stash_panel` 의 리터럴 색 2곳; `Notice` 의
  UTF-8 경계 절단·시계 되감김; confirm 라벨 79바이트 초과 시 ID 불변식(`PushID` 방식으로).

- **모든 표에 머리글 정렬(사용자 요청 2026-09-11)**: 아이템 목록·인벤토리처럼 나머지 표 9개 —
  로스터 6개(동반자 · 근처 · 내 동반자 · 종 바꾸기 팝업 · 동반자 아이템 · 탈것/용병 타입/캐릭터 목록) ·
  장비 창 착용 장비 · 지급 창 세션 표 · 인벤 소켓 상한 표 — 에 `Sortable | SortTristate` 를 걸고
  `TableGetSortSpecs()` 로 뷰를 정렬한다. 공용 헬퍼 `render/table_sort.h`(ImGui 없음): 열 번호·방향과
  열별 키 함수로 벡터를 정렬, 해제(`SpecsCount == 0`)면 원래 순서. 로스터는 위 뷰 캐시의 키에 정렬
  열·방향을 넣어 매 프레임 재정렬하지 않는다. 보관함(줄 목록)은 대상이 아니다.

**테스트** — `item_by_key` 조회·판 교체 · 뷰 캐시 키 비교 · `short_class` 자르기 ·
`SpecsCount == 0` 복귀 · serial 일치 판정 · `table_sort` 열/방향/해제.

## 5. 3단계 — 보관함 저장 경로(T3)

브랜치 `feat/uiux-p3`.

- **자동 저장**: `g_dirty_at`(마지막 변경 시각). `stash_tick()` 이 `dirty && now - dirty_at
  >= 1.0` 이면 `save()`. `저장` 버튼 제거, 바닥에 `TextDisabled("cdtoybox_stash.txt 에 자동
  저장 · 마지막 HH:MM:SS")`. 실패는 `Notice` Bad "저장 실패 - 파일이 잠겼는지 보세요".
  성공 로그 "보관함 저장: 즐겨찾기 N개, 세트 M개".
- **큐를 창 밖으로**: `stash_queue.{h,cpp}`(ImGui 없음) —
  `struct StashQueue { std::vector<game::StashEntry> items; std::size_t at = 0; double next_at = 0; }`,
  `enum class QueueStep { Idle, Wait, Send, Done, NoSession }`,
  `QueueStep stash_queue_step(StashQueue*, bool have_session, double now, double interval = 2.0)`.
  `stash_tick()` 을 `overlay::draw_windows()` 가 매 프레임 부른다. 보관함 창이 닫혀 있으면
  본창에 `"보관함 지급 중 N / M"` + `중단`. `NoSession` 이면 큐를 **남기고** Warn "세션이
  없어 N개 남았습니다 - 월드에 들어가면 이어집니다"(+`log::warnf`). `Done` 이면 Ok
  "N개 지급 완료". 시작·중단 로그.
- **담기 목적지**: `g_open_set`(int) → `std::string g_open_set_name`; `stash_open_set()` 은
  이름으로 색인을 찾아 돌려준다(없으면 -1). 창이 그려지지 않은 프레임엔 `stash_tick()` 이
  비운다.
- **즐겨찾기 영역**: `BeginChild("favs", ImVec2(0, min(내용, 창 높이 * 0.45)))`. 머리글
  `즐겨찾기 (N)` / `세트 (M)`.
- **세트 줄**: `[빼기][지급] 아이콘 이름(잘리면 `…` + 툴팁)  …  [개수 90] / N` — 개수 칸은
  `SameLine(avail - 140)` 으로 오른쪽 고정. `세트 지우기` → `confirm_small_button`.
  `다시 읽기` 툴팁 "파일로 되돌립니다 (아직 저장되지 않은 1초 안의 변경은 버립니다)".
  `세트 만들기` 는 이름이 비면 `BeginDisabled`. 세트 항목 `지급` 도 `set_grant_item_key`.
- **로그**: ★ 토글·세트 추가/삭제·큐 시작/중단 각 한 줄.

**테스트** — `stash_queue_step`(간격·세션 없음에 큐 유지·완료), 자동 저장 판정
(`autosave_due(dirty_at, now)`), 이름 기반 목적지 해석(삭제 뒤 -1).

## 6. 검증

- 단계마다: `cdtb_tests` 전부 통과(지금 387) → 워크트리 빌드 → develop 머지 →
  `scripts/deploy.ps1`(게임 종료 후) → 게임에서 화면 확인.
- 화면 확인은 `scripts/overlay-check.ps1` 로 내가 한다(게임을 포그라운드로 가져오지 않고
  PrintWindow 캡처 + PostMessage 클릭). **게임 메모리에 쓰는 버튼과 고르기 팝업의 [적용]은
  누르지 않는다**; 확인 버튼은 첫 클릭(무장)까지만 본다. 쓰기 흔적은 로그로 확인한다.
- 1단계 확인 목록: 본창 라벨·단축키·버전 줄 / 장비 연마 숫자 / 종 팝업 선택→적용 흐름 /
  카메라 창 표 / 비활성 획득 툴팁(마우스 위치를 PostMessage 로 올려 캡처).
- 2단계: 장비 창 한 줄 높이·칸 팝업 / 빈 상태 문구(검색어 `zzz`) / 인벤 이름 칸 / 지급 창
  440 폭 버튼 줄 / 로스터 탭 순서.
- 3단계: 즐겨찾기 25개 상태에서 세트 머리글 / 창 닫고 큐 진행 줄(본창) / 자동 저장 시각.
- 마지막에 외부 리뷰(Codex)를 받는다(워크스트림 A 와 같은 절차).

## 7. 기록

각 단계의 플랜은 `docs/superpowers/plans/2026-09-10-uiux-p{1,2,3}.md`. 끝나면 이 문서에
"결과" 절을 붙이고(커밋·테스트 수·화면 확인), `docs/STATUS.md` §1 에 창 구조·공용 조각
목록을 갱신한다.

## 8. 결과 — 1단계 (2026-09-11)

브랜치 `feat/uiux-overlay` 20커밋 → `develop@4c0d636`(--no-ff). 45파일 +3976/−331, 테스트 **387 → 408** 전부 통과.
태스크 13개 전부 opus 구현 + opus 태스크 리뷰, 수정 라운드 4회(Task 4·7·11·12), 최종 전체 리뷰 1회 + 최종 수정 1회.
원장·리뷰 보고는 `.superpowers/sdd/2026-09-10-uiux-p1/`(git 무시).

**화면 확인(게임 실행, ini 백업 뒤 첫 실행, `scripts/overlay-check.ps1`).** 기본 배치 4창 무겹침·본창 노출 ·
2열 라벨 전부 보임(진단 펼쳐 스크롤바가 떠도) · 단축키 "Insert 토글 · F10 비활성화" · 버전 "Crimson Desert
1.0.0.2760" · 진단 헤더 접힘 · 지급 머리줄 "(이름 없음) · 탄환" · 인벤 "소켓 상한"·바닥 두 줄 · 필요할 때 여는
4창이 표 위치에 뜸 · 장비 창 연마 숫자 보임 · 종 바꾸기 팝업 줄 클릭 = 선택("선택: 칠흑발톱 곰 (행 3393)" +
[바꾸기 적용]) · 카메라 창 2열 표 + "오버레이 켜진 뒤 N초" · 확인 버튼 "정말? (3초)" 무장 뒤 4초에 해제 ·
쓰기 로그 새 형식(시작 시 자동 소켓 상한). 게임 메모리 쓰기는 하나도 실행하지 않았다.

**2단계에 넘긴 관찰.** 플레이어 창 기본 폭 320 에서 "(내부 수치입니다…)" 줄이 잘림(TextWrapped 또는 폭 360) ·
로스터 기본 폭 560 에서 열 이름이 잘림 · 인벤 버튼 열에서 "소켓 5칸" 이 "소켓" 으로 잘림(4절 인벤 항목) · 지급
440 폭에서 체크박스 잘림(4절 지급 항목) · 검색창에 한글 입력이 되는지 실제 IME 로 확인(게임 창이 ANSI 창이라
`WM_CHAR` 의 wParam 을 백엔드가 1바이트로 변환한다 — PostMessage 로 넣으면 깨졌다).

**최종 리뷰가 남긴 이월(4절에 반영됨).** `g_cap_note` → Notice, 근처 탭 성공 Ok, `apply_species` 의 `game::`
이동, 로스터 데이터 표 어미, stash 리터럴 색, Notice UTF-8 절단·시계 되감김, confirm 라벨 79바이트 초과.

## 9. 결과 — 2단계 (2026-09-11)

브랜치 `feat/uiux-p2`(develop@a4c9a77 에서) — 태스크 9개 + 수정 라운드 3회(T2 표 높이, T5 팝업
ID, 최종 wave) + 플랜 정정 3건, 커밋 17개. 태스크마다 opus 구현자·리뷰어, 마지막에 브랜치 전체
최종 리뷰(With fixes → 수정 wave 1회 → 재리뷰 전부 해결). 테스트 **408 → 424**(전부 통과).

### 공용 조각(2단계 신규)
- `render/table_sort.h` — `SortSpec`·`cmp3`·`sort_view`(안정 정렬, 뷰에만). `render/table_sort_imgui.h` —
  `table_sort_pull`. `render/view_cache.h` — `ViewKey`(tab·query·type·flags·sort·generation·count·stamp)
  + `CachedView<T>`. `game::ItemKeyIndex`/`item_by_key`(판 교체 감지, 뮤텍스). `game::item_sort_from_specs`.
- 정렬이 붙은 표 11개: 아이템 목록(해제 → 키 오름차순 복귀)·인벤·소켓 상한·세션·장비·로스터 6개.
  tristate 표는 `DefaultSort` 열이 없으면 첫 프레임 정렬 없음(원래 순서)이다.

### 판정 목록 (장부의 `Ruling:` 전부, 순서대로 — 틀렸으면 되돌릴 비용을 함께)
1. T1 기대 테스트 수 13→12 는 플랜 덧셈 착오 — 기준선 420 으로 정정. (문서)
2. 인벤 버튼 열 190→**160**, `보관` 라벨 고정 + 세트 이름은 툴팁·결과 줄. (숫자 하나)
3. 본창 쓰기 표시에 **인벤토리 포함**(소켓 열기·상한이 쓰기). (조건 하나)
4. Notice 시계 되감김은 **Gone**(옛 시계의 결과). (한 줄)
5. `flow_same_line` 을 content-region 기준으로 재작성(표 칸 안에서도 흘림). (창에서는 같은 값)
6. 기본 정렬 열: 근처 표 액터·핸들 NoSort + 이름 DefaultSort, 세션 표 횟수 내림차순, 나머지는 없음.
7. `apply_species` 를 `game::` 으로 옮기되 명령 파일 연결은 하지 않음(호출자가 로스터뿐).
8. T2: `ScrollX` 표는 자식 창이라 세션 표 높이를 머리글+8줄로 고정 + `ScrollY`. (한 줄)
9. T2 리뷰 권장(대기 중 "끝났습니다" 문구)은 처음 보류 → 최종 리뷰가 뒤집어 분기 순서 수정.
10. T5: 칸 팝업 confirm 버튼은 대상(인스턴스·칸)별 `PushID` — 무장 3초가 다른 장비로 넘어가던 회귀를 필수 수정으로.
    소켓 라벨 `%.60s` 로 `##s%d` 보호.
11. T7 구현자 우려(콘솔 명령의 `refresh_live_actors` 가 stamp 를 안 올림)는 `swap`/`move` 갱신이라 주소가 겹칠 수
    없어 조치 불필요 — 최종 리뷰는 별도로 **스레드 경쟁**(콘솔 스레드가 렌더 스레드와 같은 벡터를 갈아엎음)을
    기존 결함으로 지목 → 후속 태스크(아래).
12. T7 리뷰 권장: 종 팝업 빈 상태 두 갈래 문구, 팝업 폭 하한 160 → T8 에 포함.
13. T9 이월 9건(주석 정밀화·테스트 전제 주석·`max_temper` 선형 탐색 제거·머리글 루프 PushID·열 폭 44·`g_tab`
    초기값·`msg` 널 금지 주석 등) 채택.
14. 최종 리뷰: I-1 아이템 탭 게이트 레인(Item), I-2 지급 결과 줄 순서(대기 먼저), I-3 본창 표식 `*`,
    M-1·M-2·M-6·M-10 — 한 wave 로 수정(d132143).
15. 보류(코드 그대로): 개수 줄 한 프레임 지연(표 9개 공통, 자가 복구) · 소켓 상한 표 매 프레임 정렬(접힘 안) ·
    장비 `부위` 열은 분류 번호로 정렬(같은 부위끼리 모임) · 보관함 줄 루프의 `item_by_key`(수십 행) ·
    포획 탭 이름 열 정렬 키 `label`(표시 값과 일치) · confirm 라벨 200바이트 초과 시 표시만 잘림.

### 알려진 문제 · 후속
- **콘솔 명령 스레드 경쟁(기존 결함)**: `actordiff`/`actordump` 가 명령 스레드에서 `refresh_live_actors` 를 불러
  렌더 스레드가 그리는 `g_live` 를 갈아엎는다(`actors.cpp` 의 "그리는 스레드만" 규약 위반). 값싼 경화: 갱신
  세대 카운터를 `game::` 안에 두고 `refresh_live_actors` 가 올리게 하면 어느 스레드가 불러도 캐시가 무효화된다 —
  정공법은 콘솔 refresh 를 렌더 스레드로 넘기는 것. 별도 태스크.
- 지급 창 결과 줄은 여전히 "내 결과가 끝난 뒤 남이 덮으면" `다른 창의 지급 결과` 가 된다(참인 문구).
- 오버레이가 켜진 동안 게임 입력(raw input·`GetAsyncKeyState`)이 새는 문제는 별도 브랜치 `fix/overlay-input`.

### 화면 검증 (2026-09-11 17:47~17:51, 게임 1.0.0.2850, develop@6a0347d 빌드, PostMessage 캡처 26장)
- 본창: `*` 표식 4개 + 범례 한 줄, 200px 열과 안 겹침 ✓. 버전 줄 `1.0.0.2850`.
- 아이템 목록: 머리글 정렬 키 ▲ → ▼ → 해제(화살표 없음)에서 키 오름차순 유지 ✓. ★ 열·행 클릭 ✓.
- 인벤토리: 버튼 `지급`·`보관` 160 열 안에 들어감 ✓, 안내 3줄 ✓, 검색어 "zzzz" 로 빈 상태 줄이 뜸 —
  **결함**: 문구가 잘리고 [지우기] 가 안 보임 - 저장된 창 배치에서 이름 열(0번, stretch)이 좁아진
  탓이었다. 처음엔 열 번호를 1로 옮겼으나 오진(Codex 지적) → `table_empty_row` 가 화면 첫 표시 열에
  쓰되 클립을 표 안쪽 폭으로 넓히도록 고쳐 열 폭·순서에 매이지 않게 했다(열 번호 인자 제거).
- 지급: 세션 게이트 통과, `고급` 세션 표 횟수 내림차순 정렬·높이 9줄·ScrollX ✓.
- 보관함: 로딩 게이트 통과, 즐겨찾기 목록 ✓.
- 장비: 부위·장비·연마·소켓·염색 5열, 부위 오름차순, 소켓 칸 버튼이 칸 안에서 2~3줄로 흘림 ✓,
  칸 팝업(`차가운 어둠의 판금 갑옷 소켓 3` · 지금 보석 + [비우기] · 보석 목록 · [적용]) ✓ — 누르지 않음.
- 로스터: 탭 순서 내 동반자·근처·동반자 아이템·포획 가능 종·탈것·캐릭터·용병 타입 ✓, 범례 ✓, `자동 2초` ✓,
  내 동반자 `23 / 42 개체, 그중 월드에 3` ✓, 근처 `80 / 368 액터` 이름 기본 정렬 ✓, 동반자 아이템 표 ✓,
  포획 가능 종 키 정렬 ▲/▼ ✓(야생·고용 44px 에 글자 들어감), 탈것·캐릭터(7250)·용병 타입(21) 표 ✓,
  종 바꾸기 팝업 폭이 창 안에 들어가고 `후보 512개` ✓(적용 안 누름). 창 폭 560 은 여전히 내 동반자·근처
  열을 좁게 만든다(관찰, 사용자가 창을 넓히면 됨).
- 플레이어: `(내부 수치입니다 …)` 두 줄로 접힘 ✓.
- 툴팁(★ 머리글·체크박스)은 PostMessage 호버로는 안 찍힌다(백엔드 WM_MOUSELEAVE) — 코드 검토로 갈음.
- 메모리 쓰기 로그 없음(검증 전후 `쓰기 ` 3줄 동일 = 시작 시 소켓 상한뿐).

## 10. 결과 — 3단계 (2026-09-11)

브랜치 `feat/uiux-p3`(develop@a560e4b 에서) — 태스크 4개 + 최종 수정 wave 2회 + 마무리 1회, 커밋 9개.
태스크마다 opus 구현자·리뷰어(전부 승인, Task 4 만 브리프 결함 D1), 브랜치 전체 최종 리뷰
("수정 후 머지": Important 2·Minor 13 → 수정 wave 2 → 범위 재리뷰 "전부 해결", 새 결함 2건은 마무리
커밋에서). 테스트 **438 → 452**(전부 통과). 스펙 §5 요구 10항목과 §0 T3 6건 전부 해소(최종 리뷰 대조표).

### 새 조각·구조
- `render/stash_queue.{h,cpp}` — `StashQueue`·`QueueStep`·`stash_queue_step/sent/clear/remaining`,
  `stash_autosave_due`(ImGui 없음, 시험 9개). `game::Stash::find_set`·`dedupe_set_names`(시험 5개).
- `stash_tick()` 은 `overlay::on_frame()` 의 **가시성 무관 블록**(가드 설치 뒤)에서 매 Present 돈다 -
  오버레이를 숨겨도, 창을 닫아도 자동 저장·큐가 이어진다. 그래서 보관함의 시각은 ImGui 시계가
  아니라 단조 시계(`stash_clock` = GetTickCount64)다(알림만 ImGui 시계). 해체 경로는 큐를 접고
  `stash_flush()` 한다.
- 담기 목적지는 **펼쳐진 세트의 이름**(`g_open_set_name`, 루프 끝에 대입, 창을 안 그린 프레임엔 비움)이고
  머리글 ID 는 `###set:이름` 으로 고정된다. 같은 이름 세트는 만들 때 거절하고 파일에서 오면 읽을 때
  `" (2)"` 로 바꾼다. `stash_open_set()` 은 세트 세대·이름으로 캐시한다.
- 본창은 보관함 창 본문이 안 그려지는 동안(`stash_body_visible()` 거짓 - 닫힘·접힘) 진행 줄·`중단`·
  알림을 대신 그린다. `confirm_small_button(label, needs_write_guard=false)` 변형(세트 지우기).

### 판정 목록 (원장 `.superpowers/sdd/2026-09-11-uiux-p3/progress.md` 의 `Ruling:` 전부)
1. `stash_queue_step` 의 `interval` 을 `stash_queue_sent` 로 옮김(step 안에서 미사용). (서명)
2. 실행 방식은 서브에이전트 구동(opus) — 1·2단계와 같은 선택. (비용)
3. `GetWindowContentRegionMax`(폐기 예정) 대신 커서 기준 계산. (수 px)
4. Task 2·3 를 Task 1·2 리뷰와 병행 dispatch — 전사 태스크라 API 가 플랜에 고정. (순서)
5. 숨김 중 tick 정지 → tick 을 가시성 무관 블록으로 **옮기되 시계를 단조 시계로**(Task 3 리뷰가
   ImGui 시계가 NewFrame 밖에서 얼어붙음을 잡음). Task 2 리뷰의 시계 리셋 가드는 이로써 불필요. (설계)
6. 세트 지우기의 쓰기 잠금 게이트는 본체가 아니라 인자 변형으로 해제(다른 호출부 5곳은 게임 메모리 쓰기). (한 줄)
7. 이름 자르기 폭은 `draw_item_line` 안에서 아이콘 폭을 뺌(세트 줄 150 유지). (수치)
8. 최종 리뷰 I-1·I-2·M-1·M-3~M-9·M-11·M-12 는 수정 wave 2 로, M-2(세션 없음 때 본창 문구)·M-10(자르기
   성능)·M-13(STATUS 목록)은 기록만. (범위)
9. 재리뷰의 새 결함 2(즐겨찾기 줄 높이 `max(아이콘, 프레임 높이)`, `notice_put_now` 컨텍스트 널 가드)는
   마무리 커밋에서 재리뷰 없이. (한 줄씩)

### 알려진 것 (고치지 않음)
- 세션이 없어 멈춘 큐는 본창에 `보관함 지급 중 0 / N` 으로만 남고, 이유 알림은 60초 뒤 사라진다(M-2).
- `draw_name_clipped` 는 글자 하나씩 떼며 `CalcTextSize` 를 반복한다(M-10, 이름 30자 수준).
- 게임이 그냥 종료되면 마지막 1초 안의 변경은 유실된다(`DLL_PROCESS_DETACH` 에서 파일 IO 를 하지 않는다).
- 스펙 §5 문구 "N개 지급 완료" 는 "-습니다" 체 규칙에 따라 "N개 지급했습니다" 로 냈다.

### 화면 검증 (2026-09-11 21:41~21:44, 게임 1.0.0.2850, develop@b448075 빌드 md5 aaf04c6f, PostMessage 캡처 8장)
- 로그: 첫 Present 에 `보관함 읽음: 즐겨찾기 26개, 세트 5개`(창을 열기 전, 중복 이름 없음). ★ 토글 →
  `보관함: 즐겨찾기 28917 추가` → 0.99초 뒤 `보관함 저장: 즐겨찾기 27개, 세트 5개`, 되돌리기도 같은 간격 ✓.
- 즐겨찾기 26개 상태: `즐겨찾기 (26)` 머리글 + 자식 영역이 창 높이 45% 에서 멈추고 스크롤바, 그 아래
  `세트 (5)` 머리글·이름 입력·`세트 만들기`(빈 이름이라 비활성)·세트 5줄이 밀려나지 않고 보임 ✓.
- 바닥 줄 `cdtoybox_stash.txt 에 자동 저장 · 마지막 21:42:13` + `다시 읽기` — 로그의 저장 시각과 일치 ✓
  (창이 400px 이라 휠 스크롤로 내려 찍음).
- 창 닫고 본창 진행 줄: 큐가 실제로 돌아야 보이므로 미실시 — 코드 검토(`stash_body_visible` 조건)로 갈음하고
  사용자 실사용 확인을 요청. 접힌 창(▶)도 같은 조건으로 본창이 대신 그린다.
- 메모리 쓰기 로그 없음(검증 전후 `쓰기 ` 줄 동일 = 시작 시 소켓 상한뿐). 1차 시도(로딩 화면)에서 제 클릭이
  보관함 창의 접기 화살표를 눌러 창이 접혔고, 사용자가 다시 펼침 — 제목 줄은 화살표(x≈+10)를 피해 가운데를 누를 것.

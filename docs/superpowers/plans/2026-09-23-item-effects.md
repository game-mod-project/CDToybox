# 아이템 효과 문구 구현 계획 (PR2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 아이템 목록 · 인벤토리 창의 "설명" 툴팁이 게임 툴팁과 같은 내용을 보이게 한다 — 효과 줄(숫자 · 지속시간 포함) · 장착 가능 부위 · 설명 문단.

**Architecture:** 시작할 때 배경 스레드가 게임의 정적 표만 걸어 아이템별 효과 줄을 **미리 조립**하고 불변 스냅샷으로 게시한다. 실행 중 상태(플레이어 · 인벤토리)는 보지 않는다. 문구는 `PatternDescriptionInfo` 의 형식 문자열에 값을 끼워 만들고, 값의 배율은 **게임 코드에서 뽑아 커밋한 표**로 정한다.

**Tech Stack:** C++20 / MSVC / 기존 `mem::Reader` · `mem::Rtti` · `game::localization` · 단위 시험 하니스(`tests/harness.h`) · 파이썬 3.14(표 생성기, capstone).

**Spec:** `docs/superpowers/specs/2026-09-22-item-description-effects-design.md` — 특히 **§4.7**(조립 규칙 · 확정)과 §4.7-H''(배율의 출처). 이 계획은 그 명세의 구현이며, 어긋나면 **명세가 이긴다**.

**이 계획이 대체하는 것:** `docs/superpowers/plans/2026-09-22-item-description-effects.md` 의 Task 7~10(PR2 1단계). 그 문서의 실측 과제는 2026-09-23 에 끝났고 결과가 명세 §4.7 에 들어갔다.

## Global Constraints

- **추측 금지.** 오프셋 · 배율 · 행 번호는 명세 §4.7 에 적힌 실측값만 쓴다. 명세에 없는 값이 필요하면 멈추고 `NEEDS_CONTEXT` 로 보고한다.
- 게임 메모리에 **쓰지 않는다**. 이 PR 은 전부 읽기다.
- 게임 함수를 부르지 않는다(현지화 조회 함수 포함 — `localization.h` 머리 주석).
- 값은 **부호 있는 32비트로 읽는다**(u64 로 읽으면 상위 4바이트가 잔여물이고, 부호 없이 읽으면 음수 값이 40억대가 된다). 4바이트 핸들도 **하위 u16 만** 쓴다.
- 사용자 대면 문구는 **합쇼체**. 로그는 기존 투(`log::infof`)를 따른다.
- 새 파일은 `CMakeLists.txt` 의 세 목록(DLL · cdtb_core · cdtb_tests)에 맞게 등록한다.
- 시험은 `tests/fake_memory.h` 의 `FakeMemory` 로 쓴다(게임 없이 돈다). 각 Task 끝에 `scripts\build.ps1` 성공 + `build\cdtb_tests.exe` `0 failures` 를 확인한다.
- 빌드는 **숨긴 창**으로: `Start-Process -WindowStyle Hidden -PassThru` + `$p.WaitForExit(600000)`. `-Wait` 는 멈춘다.
- 브랜치는 `feat/item-effects`(이미 있다, develop 기준). `main` · `develop` 직접 push 금지.

---

### Task 1: 버프 파라미터 표 생성기

**Files:**
- Create: `tools/rtti/buff_params.py`
- Create: `src/game/buff_param_table.inc` (생성물 — 커밋한다)
- Test: `tests/buff_param_table_tests.cpp`

**Interfaces:**
- Produces: `.inc` 파일이 `constexpr BuffParamRule kBuffParamRules[]` 배열 리터럴을 낸다. 항목은 `{std::uint64_t vtable_va; std::uint16_t offset; double divisor; const char* note;}`.

**왜 생성기인가:** 배율은 클래스마다 다르고(§4.7-H''), 28종을 손으로 옮기면 틀린다. 게임 갱신 때 다시 돌려야 한다.

- [ ] **Step 1: 이미지 덤프 확보** — `cdtb_probe dumpimage <경로>` (게임 실행 중에만 된다). 없으면 생성기는 "이미지가 필요합니다" 로 **실패**한다(조용히 빈 표를 내지 말 것).

- [ ] **Step 2: 생성기 작성** — `python tools/rtti/buff_params.py <덤프이미지> <vtable 목록 파일> > src/game/buff_param_table.inc`
  - vtable 목록은 `{16진 vtable, 이름}` 줄들. 이름은 `cdtb_probe whatis` 나 RTTI 로 얻는다.
  - 각 vtable 의 **슬롯 11**(`vtable + 11*8`)을 역어셈블한다(capstone, `find_class.Image` 로 RVA→오프셋).
  - `jmp` 로 시작하면 **thunk 다 — 대상까지 따라간다**(0x1458FC230 · 0x1458FC1C0 이 그렇다).
  - 함수 안에서 (a) `rcx + 0xNN` 로 읽는 칸, (b) 나눗셈을 찾는다:
    - 정수: `movabs rax, <매직>` + `imul` + `sar` → 매직 표(`0x20C49BA5E353F7CF`=1000, `0x346DC5D63886594B`=10000, …)
    - 실수: `vdivsd`/`vmulsd` 의 rip 상대 상수를 읽어 곱해 합친다
  - **분기를 구분한다.** 파라미터 종류 검사(`dec r8b` · `sub r8b, N` · `test r8b, 0xFB`)로 갈라지는 분기마다 결과가 다르다 — 한 함수에서 상수를 모두 긁어 곱하면 틀린다(명세 §4.7-H'' 마지막 문단).

- [ ] **Step 3: 자기 검증(생성기 안에서)** — 아래 셋이 안 맞으면 **0이 아닌 종료 코드로 죽는다**:
  ```
  0x1458FBD20 VaryStatMaxValue          -> +0x98 ÷1000
  0x1458FA908 (소켓 치명타)              -> +0x98 ÷10000
  0x1458FB5A0 VaryDataDefinedStatRate   -> +0x98 ÷10000000
  ```

- [ ] **Step 4: 시험** — `tests/buff_param_table_tests.cpp`
  ```cpp
  TEST(buff_param_table_has_the_three_measured_rules) {
      // 생성물이 실측값과 같은지. 생성기를 다시 돌려 표가 바뀌면 여기서 깨진다.
      CHECK_EQ(buff_param_divisor(0x1458FBD20ull), 1000.0);
      CHECK_EQ(buff_param_divisor(0x1458FA908ull), 10000.0);
      CHECK_EQ(buff_param_divisor(0x1458FB5A0ull), 10000000.0);
      CHECK_EQ(buff_param_offset(0x1458FBD20ull), 0x98);
  }
  TEST(buff_param_table_reports_unknown_classes) {
      CHECK_EQ(buff_param_divisor(0xDEADBEEFull), 0.0);   // 0 = 모르는 클래스
  }
  ```

- [ ] **Step 5: 커밋** — `chore(tools): 버프 파라미터 배율 표 생성기`

---

### Task 2: 효과 문구 포맷터 (순수 함수)

**Files:**
- Create: `src/game/effect_format.h`, `src/game/effect_format.cpp`
- Test: `tests/effect_format_tests.cpp`

**Interfaces:**
```cpp
namespace cdtb::game {
struct EffectValues {
    double param = 0.0;        // {Param1}/{Param2} 자리 값(배율 적용 뒤)
    double repeat_tick = 0.0;  // {RepeatTick} 자리, 초 단위
    std::uint32_t duration_ms = 0;   // 접미 "(1분)" 계산용, 0 이면 접미 없음
};
// 형식 문자열에 값을 끼운다. {Staticinfo:…} 는 name_of 로 바꾼다(없으면 토큰을 그대로 둔다).
std::string effect_line(std::string_view format, const EffectValues& v,
                        const std::function<std::string(std::string_view table,
                                                        std::string_view key)>& name_of);
// 지속시간 접미. 60000ms 이상이면 "(N분)", 아니면 "(N초)", 정수 내림, 0 이면 빈 문자열.
std::string duration_suffix(std::uint32_t ms);
// 숫자 표기: 정수면 정수로, 아니면 소수점 이하 불필요한 0 을 떼고 내림 없이 그대로.
std::string effect_number(double v);
}
```

- [ ] **Step 1: 실패하는 시험 먼저** — `tests/effect_format_tests.cpp` (전부 명세 §4.7-E 의 실측 문자열이다)
  ```cpp
  TEST(effect_line_fills_param_and_duration) {
      EffectValues v; v.param = 45; v.duration_ms = 60000;
      CHECK_EQ(effect_line("{Staticinfo:SubLevel:Hp} 최대치 {Param1} 증가", v, names),
               std::string("생명 최대치 45 증가(1분)"));
  }
  TEST(effect_line_fills_repeat_tick_and_absolute_param) {
      EffectValues v; v.param = -15; v.repeat_tick = 1; v.duration_ms = 20000;
      CHECK_EQ(effect_line("{Staticinfo:SubLevel:Hp} : {RepeatTick} 초마다 {|Param1|} 감소", v, names),
               std::string("생명 : 1 초마다 15 감소(20초)"));
  }
  TEST(effect_line_leaves_unknown_tokens_alone) {
      // 이름표가 비면 토큰을 지우지 않는다 - 빈 자리가 남으면 문장이 깨진다.
      EffectValues v; v.param = 6;
      CHECK_EQ(effect_line("{Staticinfo:Status:Xyz} Lv{Param1}", v, empty_names),
               std::string("{Staticinfo:Status:Xyz} Lv6"));
  }
  TEST(duration_suffix_floors_to_minutes_then_seconds) {
      CHECK_EQ(duration_suffix(90000), std::string("(1분)"));   // 실측: 90000 은 "1분"
      CHECK_EQ(duration_suffix(60000), std::string("(1분)"));
      CHECK_EQ(duration_suffix(20000), std::string("(20초)"));
      CHECK_EQ(duration_suffix(6000),  std::string("(6초)"));
      CHECK_EQ(duration_suffix(0),     std::string(""));
  }
  TEST(effect_number_drops_trailing_zeros) {
      CHECK_EQ(effect_number(75.0), std::string("75"));
      CHECK_EQ(effect_number(2.5),  std::string("2.5"));
  }
  TEST(effect_line_handles_key_and_money_tokens_by_leaving_them) {
      EffectValues v;
      CHECK_EQ(effect_line("대지의 울림 ({Key:Key_Skill_1})", v, names),
               std::string("대지의 울림 ({Key:Key_Skill_1})"));
  }
  ```

- [ ] **Step 2: 구현** — `{Param0..3}` · `{|Param0..3|}`(절대값) · `{RepeatTick}` 치환, `{Staticinfo:<표>:<키>}` 는 콜백. 그 밖의 `{…}` 는 **손대지 않는다**.

- [ ] **Step 3: 빌드 · 시험 · 커밋** — `feat(effects): 효과 문구 포맷터`

---

### Task 3: 스탯 이름표 (`{Staticinfo:…}` 치환)

**Files:**
- Create: `src/game/stat_names.h`, `src/game/stat_names.cpp`
- Test: `tests/stat_names_tests.cpp`

**Interfaces:**
```cpp
namespace cdtb::game {
// 표(SubLevel · Status) + 내부 키("Hp") -> 표시 이름("생명"). 한 번 만들고 캐시한다.
class StatNames {
  public:
    bool build(const mem::Rtti& rtti, const mem::Reader& reader, const LocSystem& loc);
    std::string name_of(std::string_view table, std::string_view key) const;  // 없으면 ""
    std::size_t size() const;
  private:
    std::unordered_map<std::string, std::string> map_;   // "SubLevel:Hp" -> "생명"
};
}
```

**사슬(명세 §4.7-H' 1번, 그대로 구현한다):**
```
SubLevelInfoManager(40행) / StatusInfoManager(84행)  ← RTTI 로 찾는다
  레코드 _stringKey(+0x08) = 문자열 객체 포인터 {char*, u32 len, u32 hash}
  지식 행 = SubLevel 은 +0x46, Status 는 +0x34   (0xFFFF 면 없음, 0 은 유효한 행이다)
KnowledgeInfoManager(전역 RVA 0x06D69AB8, 개수 +0x08, 배열 +0x58 — knowledge.h 가 이미 쓴다)
  레코드 +0x08 문자열 객체의 **hash** 를 엔티티로 현지화 cat 9 / 필드 0x490
```

- [ ] **Step 1: 시험 먼저** — `FakeMemory` 로 두 표 · 지식 표 · 현지화 항목을 세우고
  ```cpp
  TEST(stat_names_resolves_sublevel_through_knowledge) { /* "SubLevel:Hp" -> "생명" */ }
  TEST(stat_names_resolves_status_through_active_knowledge) { /* "Status:IceResistance" -> "냉기 저항" */ }
  TEST(stat_names_treats_knowledge_row_zero_as_valid) { /* 행 0 은 있음, 0xFFFF 만 없음 */ }
  TEST(stat_names_returns_empty_for_unknown_key) { }
  TEST(stat_names_build_fails_when_a_manager_is_missing) { }
  ```
- [ ] **Step 2: 구현** — 실패는 조용히 빈 표(효과 문구는 토큰을 그대로 보인다).
- [ ] **Step 3: 빌드 · 시험 · 커밋** — `feat(effects): 스탯 이름표`

---

### Task 4: 효과 걷기 (`item_effects`)

**Files:**
- Create: `src/game/item_effects.h`, `src/game/item_effects.cpp`
- Test: `tests/item_effects_tests.cpp`

**Interfaces:**
```cpp
namespace cdtb::game {
struct ItemEffect {
    std::string text;          // 완성된 한 줄
    std::uint32_t duration_ms = 0;
};
struct ItemEffects {
    std::vector<ItemEffect> lines;
    int unresolved = 0;        // 해석 못 한 효과 수(배율 모르는 클래스 등)
};
// 전체 표를 한 번 걷는다. 오래 걸리므로 배경 스레드에서 부른다.
bool build_item_effects(const mem::Rtti& rtti, const mem::Reader& reader,
                        const LocSystem& loc, const StatNames& names,
                        std::vector<ItemEffects>* out);   // 색인 = 아이템 행 번호
}
```

**규칙(명세 §4.7-B · C · D, 그대로):**
1. 소모품: `ItemInfo +0x80` u32 행 배열 → `ItemUseInfoManager` 레코드 → `+0x18` ItemUseData → `+0x18` 종류 0x00(Skill) **그리고** `+0x19` 문맥 0x00 인 것만 → `+0x30` `{u32 스킬행, u32 레벨}` → `SkillInfo +0x18` 레벨 목록 → BuffData 배열.
2. 어비스/인챈트: `ItemInfo +0x248` → `EnchantData +0x58` `_equipBuffs`(0x20 스트라이드, `+0x00` 하위 u16 = BuffInfo 행, `+0x04` i32 레벨) → BuffInfo.
3. BuffInfo 레벨 목록은 `+0x18`(개수 `+0x20`), 칸 16바이트 `{i32 레벨, u32, BuffData*}` — **레벨을 값으로 찾는다**(음수 가능).
4. BuffData: `+0x38` 하위 u16 이 패턴 행. `0xFFFF` 면 `ChangeBuffLevel`(vtable 로 판정) 일 때만 `+0x90` 하위 u16 BuffInfo 행 · `+0x94` 레벨로 **한 칸 더**(최대 2단, 실측 3단 없음).
5. 값: `buff_param_table` 에서 vtable 로 `{offset, divisor}` 를 찾는다. 없으면 그 줄은 **버리고 `unresolved` 를 센다**.
6. 지속시간은 **바깥쪽**(스킬 레벨에 직접 달린) BuffData 의 `+0x18` 이다.

- [ ] **Step 1: 시험 먼저** — `FakeMemory` 로 작은 표를 세워 다음을 못박는다
  ```cpp
  TEST(item_effects_takes_only_the_tooltip_context) { /* +0x19 != 0 은 버린다 */ }
  TEST(item_effects_follows_change_buff_level_one_hop) { }
  TEST(item_effects_finds_negative_buff_levels_by_value) { }
  TEST(item_effects_uses_the_outer_duration) { }
  TEST(item_effects_counts_unknown_classes_as_unresolved) { }
  TEST(item_effects_reads_enchant_equip_buffs) { }
  TEST(item_effects_survives_a_garbage_handle_upper_half) { /* {u16, 0xFBCD} */ }
  ```
- [ ] **Step 2: 구현.** 표는 RTTI 로 찾고(`find_static_manager`), 매니저마다 `+0x08` 개수 · `+0x58` 배열.
- [ ] **Step 3: 빌드 · 시험 · 커밋** — `feat(effects): 효과 걷기`

---

### Task 5: 장착 가능 부위

**Files:**
- Create: `src/game/equip_types.h`, `src/game/equip_types.cpp`
- Test: `tests/equip_types_tests.cpp`

**Interfaces:**
```cpp
// _equipAbleHash 로 걸리는 EquipTypeInfo 이름들. 게임 툴팁은 이것을 "무기 · 장갑 · 신발" 로
// 접어 보이는데, 접는 규칙은 아직 데이터에서 못 찾았다(명세 §4.7-H 5번) - 우리는 이름을 그대로
// 보이되 개수가 많으면 잘라 "외 N종" 을 붙인다.
std::vector<std::string> equip_type_names(const mem::Rtti&, const mem::Reader&,
                                          const LocSystem&, std::uint64_t equip_able_hash);
std::string equip_types_line(const std::vector<std::string>& names, std::size_t max_shown = 4);
```
근거: `EquipTypeInfo`(117행) `_equipAbleHashList +0x48` 에 해시가 들어 있으면 그 행이 대상. 이름은 `_equipTypeName +0x58` → 현지화 cat 46 / 필드 0x2E0.

- [ ] **Step 1: 시험** — 해시 매칭 · 이름 나열 · `외 N종` 자르기 · 해시 0 이면 빈 목록.
- [ ] **Step 2: 구현 · 빌드 · 시험 · 커밋** — `feat(effects): 장착 가능 부위`

---

### Task 6: 탐침 명령 `effects`

**Files:**
- Modify: `tools/probe/main.cpp`

`cdtb_probe effects <아이템키>` 가 그 아이템의 효과 줄 · 해석 못 한 수 · 장착 부위를 찍는다. **모드가 쓰는 것과 같은 함수를 부른다**(`cmd_items` 와 같은 이유 — 배포 전에 여기서 본다).

- [ ] **Step 1: 구현 · 빌드.**
- [ ] **Step 2: 게임에서 확인** — `effects 751123` 과 `effects 1002072` 가 명세 §4.7-H' 의 여섯 줄을 그대로 내야 한다. 안 나오면 멈추고 보고한다.
- [ ] **Step 3: 커밋** — `feat(probe): effects 명령`

---

### Task 7: 창에 붙이기

**Files:**
- Modify: `src/game/items.h` · `items.cpp` (효과 스냅샷 게시), `src/render/item_style.cpp`(툴팁), `src/render/item_panel.cpp` · `inventory_panel.cpp`(검색)

- [ ] **Step 1:** 배경 스레드에서 `build_item_effects` 를 돌리고 **따로 게시**한다(`item_effects_for(key)`) — 아이템 표는 게시 뒤 안 바꾼다(명세 §4.5).
- [ ] **Step 2:** 설명 툴팁을 `효과 줄들 → 장착 부위 → 설명 전문` 순으로 그린다(분류는 이미 표의 열이라 툴팁에 다시 넣지 않는다). 효과가 없으면 그 절은 빼고, 해석 못 한 것이 있으면 마지막에 `해석 못 한 효과 N개` 를 흐리게 붙인다.
- [ ] **Step 3:** 검색이 효과 문구에도 걸리게 한다(`game::passes` 에 효과 문자열 추가 — 스냅샷이 준비된 뒤부터).
- [ ] **Step 4:** 빌드 · 시험 · 커밋 — `feat(ui): 설명 툴팁에 효과 · 장착 부위`

---

### Task 8: 문서

- [ ] `docs/STATUS.md` §1.7 에 효과 문단(경로 두 갈래 · 이름표 · 배율 표의 출처).
- [ ] `docs/TROUBLESHOOTING.md` 에 두 함정: **값은 부호 있는 32비트로 읽는다**(u64 로 읽으면 잔여물이 섞인다), **버프 레벨은 음수가 있어 첨자로 찍으면 안 된다**.
- [ ] `docs/README.md` · 루트 `README.md` · `CLAUDE.md` 의 수(시험 · 명세 · 계획) 갱신.
- [ ] 커밋 — `docs: 효과 문구`

---

## 검증 (PR 올리기 전)

1. `scripts\build.ps1` 성공 · `build\cdtb_tests.exe` `0 failures`.
2. `cdtb_probe effects 751123` · `1002072` 가 명세의 여섯 줄과 **글자까지** 같다.
3. `cdtb_probe effects 1003765` · `1003766` · `1003767` 이 `천 갑옷 타격 시 치명타 확률 2% / 5% / 7% 증가` 를 낸다(사용자 툴팁으로 확인된 값).
4. 게임에서 화면 확인 한 번(아이템 목록 툴팁 · 검색).

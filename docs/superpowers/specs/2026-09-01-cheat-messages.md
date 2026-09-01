# 출시 빌드에 남은 개발자 치트 메시지

2026-09-01. 게임을 켜지 않고 `bin64/CrimsonDesert.exe` 만 보고 알아낸 것.
도구는 `tools/rtti/` 에 있다.

## 무엇이 남아 있나

출시 빌드에 개발자 치트용 RPC 메시지 클래스 35개가 그대로 들어 있다.
RTTI 이름으로만 존재하고, 콘솔 명령 문자열은 없다 — 즉 **코드에서 직접
생성해 보내는 구조**지 문자열로 치는 콘솔이 아니다.

이름 규칙은 `pa::TrocTr<이름>CheatReq` / `...CheatAck`. `Req` 는
클라이언트 -> 서버, `Ack` 는 그 반대다.

## 메시지 ID

각 클래스마다 전역 타입 서술자가 하나씩 있다. 배치는:

```
+0x00  vtable 포인터   (정적 초기화 때 실제 클래스 vtable 로 덮인다)
+0x08  u32 방향        Req=4, Ack=1
+0x0C  u32 메시지 ID
```

`+0x08` 이 30개 Req 전부 4, 4개 Ack 전부 1로 갈린다. 필드 해석이 맞다는
근거로 삼았다.

아이템 관련만 옮기면:

| ID | 클래스 | 서술자 RVA |
|----|--------|-----------|
| 2440 | TrocTrSetInventorySlotCountByCheatReq | 0x0604CF20 |
| 2503 | TrocTrDeleteItemCheatReq | 0x0604B780 |
| 2736 | TrocTrVaryEnduranceItemByCheatReq | 0x0604C3E0 |
| 2944 | TrocTrCreateItemFromTrItemValueCheatReq | 0x0604B660 |
| 3013 | TrocTrSpawnItemToGroundByCheatReq | 0x0604B6F0 |

전체 목록은 `python tools/rtti/map_cheats.py <exe> CheatReq` 로 다시 뽑는다.

## 전선 형식

역직렬화 함수(`SpawnItemToGroundByCheatReq` 는 RVA 0x2594780)가 하는 검사:

```
r15 = [r8+0x18]                 버퍼
movzx ecx, word [r15+3]         페이로드 길이
movzx eax, word [r8+0x10]       전체 크기
sub   rax, 5
cmp   rax, rcx                  전체 - 5 == 길이
```

머리 5바이트 + 페이로드다. 길이가 `+3` 에 u16 으로 있으므로 ID 는
`+1..+2` 자리로 보인다(미확인 — 실행 중 확인이 필요하다).

페이로드는 `call [rax+8]` 형태의 스트림 읽기 함수를 순서대로 부른다.
`r8d` 가 읽을 바이트 수다. `SpawnItemToGroundByCheatReq` 의 순서:

```
 1.  4바이트
 2.  8바이트
 3.  2바이트
 4. 12바이트        float 3개 - 위치. 함수 앞머리에서 1.0f 로 초기화한다
 5. 특수읽기 0x120CB20
 6.  4바이트
 7. 특수읽기 0x120CC00
 8. 특수읽기 0x10C46C0
 9.  1바이트
```

## 왜 이 경로가 나은가

인벤토리 주소를 몰라도 된다. 값 스캔으로 인벤토리 구조를 찾으려던
시도는 실패했다(키 옆에 개수가 있는 구조가 여럿인데 전부 UI/정의
캐시였다). 서버가 자기 경로로 처리하면 슬롯 배치·저장·UI 갱신이
전부 딸려 온다.

`SpawnItemToGroundByCheatReq` 는 페이로드가 평평하고 위치는 이미
카메라 쪽에서 알고 있으므로 만들기 쉽다. 대신 바닥에 떨어지므로 줍는
동작이 한 번 더 필요하다.

## 등록표 (실행 중 확인)

메시지 서술자를 가리키는 표가 힙에 하나 있다. 16바이트 엔트리다.

```
+0x00  u32 색인
+0x04  u32 메시지 ID
+0x08  q   서술자 포인터
```

2026-09-01 실측에서 0x407C0500030 부터였다(주소는 실행마다 바뀐다).
`cdtb_probe heapptr <서술자주소>` 로 다시 찾는다. 서술자들은 실제로
0x90 간격의 배열이었다 - 0x604B660, 0x604B6F0, 0x604B780 ...

## 처리기 사슬

`SpawnItemToGroundByCheatReq` 기준. 역직렬화(RVA 0x2594780)가 필드를
다 읽고 길이가 맞으면 처리기를 부른다.

```
0x2594780  역직렬화        (this=서술자, out=결과, 패킷)
   -> 0x2791C80  처리기
        rdi = [패킷]            세션
        [rdi+0xA0] -> +0x68 -> +0x130   객체
        call [vtable+0x140](객체, 0)    권한 검사. false 면 조용히 실패
        call 0x1DF6940(세션)            액터 조회. null 이면 오류 0x3F5
   -> 0x26A2C50  실제 작업
```

`0x26A2C50` 의 인자가 전부 드러났다.

```
rcx        액터
rdx        결과 dword 포인터
r8         u32 아이템 키 포인터    0 이면 실패
r9         i64 개수 포인터         0 이하면 실패
[rsp+0x20] u16 필드3 포인터
[rsp+0x28] float3 위치 포인터
```

키/개수 검사는 코드에 그대로 있다.

```
mov eax, [r8]      ; 아이템 키
test eax, eax
je  실패
cmp qword ptr [r9], 0
jle 실패
```

이어서 `[0x6331358]+0x68` 에서 키로 조회해 아이템 정보를 얻는다.

**이 함수를 직접 부르면 권한 검사를 건너뛴다.** 필요한 것은 액터
포인터 하나뿐이다.

## 남은 것

서술자의 나머지 칸과 ID -> 처리기 표는 **런타임에 채워진다.** 파일에는
0으로만 있다. 다음 단계는:

1. 액터 포인터를 구한다 (`0x1DF6940` 이 세션에서 꺼내는 그 객체)
2. `0x26A2C50` 을 게임 스레드에서 부른다 - 렌더 훅이 이미 그 스레드다
3. 화살 1개를 발밑에 떨궈 보고 화면으로 확인한다

필드3(2바이트)의 뜻은 아직 모른다. 0 으로 두고 시작한다.

## 35개 전수 분석 (2026-09-01)

`tools/rtti/cheat_report.py` 로 뽑는다. 산출물은 `cheat_report.txt`.

35개 중 33개가 메시지 서술자를 갖는다(ConditionData_TestCheat 과
DummyTaskCheat 은 메시지가 아니다). 방향은 Req 30개가 4, Ack 4개가 1.
머리는 전부 5바이트, 최대 크기는 전부 32710.

**모든 치트가 같은 문 하나를 지난다.** 처리기 앞머리가 전부 같은
모양이다.

```
rdi = [패킷]                    세션
rax = [rdi+0xA0]
rdx = [rax+0x68]
rcx = [rdx+0x130]
call [rcx의 vtable + N]         거짓이면 조용히 반환
```

`N` 은 무리마다 다르다.

| 슬롯 | 쓰는 치트 |
|------|-----------|
| `+0xD0` | 퀘스트·지식·용병·월드 등 대부분 (20개) |
| `+0x140` | 아이템 생성·삭제·바닥 스폰 |
| `+0x160` | 내구도·AI 제어 |
| 없음 | Kill, VaryStat, SpawnCharacter, CheatDirectPlay, ResetGameAdvice |

**이 문 하나를 열면 35개가 전부 열린다.** 치트마다 우회하는 것보다
훨씬 낫다. 실측에서 바닥 스폰은 죽지도 않고 아무 일도 없었는데,
이 문에서 조용히 빠져나간 것으로 보인다.

문이 없는 다섯 개(Kill, VaryStat, SpawnCharacter, CheatDirectPlay,
ResetGameAdvice)는 검사 없이 바로 처리한다 - 문 없이도 시험해 볼 수
있는 것들이다.

### 아이템 관련

| ID | 클래스 | 페이로드 | 문 | 작업 함수 |
|----|--------|----------|-----|----------|
| 2440 | SetInventorySlotCountByCheatReq | ? | +0xD0 | ? |
| 2503 | DeleteItemCheatReq | 3B (2,1) | +0x140 | 0x26A2EA0 |
| 2736 | VaryEnduranceItemByCheatReq | 4B (2,2) | +0x160 | 0x1D9CBB0 |
| 2944 | CreateItemFromTrItemValueCheatReq | ? | +0x140 | 0x26A2600 |
| 3013 | SpawnItemToGroundByCheatReq | 151B | +0x140 | 0x26A2C50 |

작업 함수들이 `0x26A2xxx` 에 몰려 있다 - 같은 인벤토리 계열이다.

`SpawnItemToGround` 의 페이로드 151바이트는 전부 고정 길이다.
가변 길이가 없어 만들 수 있다.

```
4(아이템키) 8(개수) 2(?) 12(위치)
16(0x120CB20) 4 40(0x120CC00) 64(0x10C46C0) 1
```

### 다음 수

문 하나가 전부를 막고 있으므로, 그 가상 함수가 무엇을 보는지 알아야
한다. 실행 중에 `세션→[0xA0]→[0x68]→[0x130]` 의 클래스를 확인하고
`vtable+0x140` 을 디스어셈블하면 된다. 단순한 플래그면 코드가 아니라
데이터만 바꾸면 된다.

## 동작 확인 (2026-09-01 22:07)

`SpawnItemToGroundByCheatReq` 로 바닥에 아이템을 떨구는 데 성공했다.
화면에서 생성되고 주울 수 있었다.

### 부르는 법

```
요청을 걸어 둔다 (렌더 스레드)
  -> 액터 조회 후킹 안에서 집어 간다 (TLS 가 선 스레드)
     -> 처리기(0x2791C80)를 부른다
        서술자, 패킷{[0]=세션}, &키, &개수, &필드3, &위치
```

### 반드시 지켜야 하는 세 가지

**스레드.** 작업 함수 안쪽이 TLS 를 쓴다.

```
mov rax, qword ptr gs:[0x58]
mov rcx, qword ptr [rax]
mov rax, qword ptr [rdx + rcx]      ; TLS + 0x250
mov dword ptr [rax + 0x2a0], r13d
```

렌더 스레드에는 그 블록이 없어 널을 참조하고 죽는다(RVA 0x25493D2).
`gs:[0x58] -> [0] -> [+0x250]` 이 전부 널이 아닌 스레드에서만 부른다.

**세션.** 서버 세션이어야 한다. 클라이언트 세션은
`[[[세션+0xA0]+0x68]+0x130]` 이 비어 처리기가 조용히 되돌아간다.
액터 조회 후킹이 세션(인자)과 액터(반환값)를 함께 보므로, 액터가
`ServerInventoryActorComponent` 로 나오는 세션을 고른다. 호출 횟수가
압도적으로 많은 것이 플레이어 것이다.

**위치.** 게임이 좌표를 그대로 쓴다. 지금은 PlayerCameraComponent 의
좌표를 넣는데 그건 카메라 위치라 3인칭에서는 캐릭터에서 떨어진
곳에 생긴다 - 바닥을 보고 누르면 발밑에 생긴다.

### 문은 잠겨 있지 않았다

`CommonAdminActorComponent` 의 vtable +0x140 은 `mov al,1; ret` 이다.
권한은 처음부터 열려 있었다.

## 인벤토리 직행 (2026-09-02, 동작 확인)

`CreateItemFromTrItemValueCheatReq` (ID 2944) 로 아이템이 인벤토리에
바로 들어간다. 바닥을 안 거치므로 위치가 필요 없다.

처리기(0x2791AC0)는 인자가 셋뿐이다.

```
rcx  서술자
rdx  패킷 { [0] = 세션 }
r8   TrItemValue*
```

### TrItemValue

작업 함수(0x26A2600)가 검사하는 자리가 코드에 그대로 있다.

```
mov eax, dword ptr [r8 + 8]      +0x08 u32  아이템 키   0이면 실패
cmp qword ptr [r8 + 0x10], r12   +0x10 i64  개수        0 이하면 실패
jle 실패
```

**나머지 칸은 게임 생성자(0x20CE900)에게 맡긴다.** `-1`, `0xFFFF`,
`0xFF` 같은 기본값을 흉내내면 틀리기 쉽다.

**버퍼는 최소 0x1B4 바이트.** 생성자가 거기까지 쓴다.

```
mov qword ptr [rbx + 0x1a4], rdi
mov dword ptr [rbx + 0x1b0], edi
```

0x100 으로 잡았다가 스택을 180바이트 넘겨 써서 게임이 나중에
죽었다. 호출은 정상 반환하고 SEH 도 안 걸렸다 - 그 조합이면
메모리를 망가뜨린 것이다.

### 부를 때 지켜야 하는 것

바닥 스폰과 같다 - 서버 세션, TLS 가 선 스레드. 추가로:

**후킹 안에서 게임 함수를 부르면 교착할 수 있다.** 그 자리가 이미
락을 쥐고 있으면 자기 자신과 물린다 - 실측에서 세 번째 호출이
돌아오지 않고 게임 조작이 통째로 멈췄다. 후킹 진입 깊이를 세어
중첩되지 않은 자리에서만 실행하고, 한 번에 하나만, 사이에 2초를 둔다.
완화이지 보장이 아니다. 근본 해결은 게임 로직 스레드의 프레임
경계를 찾는 것이다.

## 도구

```
python tools/rtti/find_class.py <exe> <이름조각>     한 클래스의 vtable
python tools/rtti/map_cheats.py <exe> [조각]         전부 훑기
python tools/rtti/xref_data.py  <exe> <RVA> [길이]   그 주소를 쓰는 코드
python tools/rtti/disasm.py     <exe> <RVA> [개수]   함수 디스어셈블
python tools/rtti/cheat_report.py <exe> [조각]       35개 전수 분석
```

`disasm.py` 는 capstone 이 필요하다 (`pip install capstone`).

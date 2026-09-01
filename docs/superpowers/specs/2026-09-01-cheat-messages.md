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

## 남은 것 (게임 실행 중에만 가능)

서술자의 나머지 칸과 ID -> 처리기 표는 **런타임에 채워진다.** 파일에는
0으로만 있다. 다음 단계는:

1. 서술자 RVA 를 실행 중에 읽어 `+0x10` 이후에 무엇이 들어오는지 본다
2. 그 표를 참조하는 디스패처를 찾는다
3. 5바이트 머리를 만들어 디스패처에 직접 넘긴다

## 도구

```
python tools/rtti/find_class.py <exe> <이름조각>     한 클래스의 vtable
python tools/rtti/map_cheats.py <exe> [조각]         전부 훑기
python tools/rtti/xref_data.py  <exe> <RVA> [길이]   그 주소를 쓰는 코드
python tools/rtti/disasm.py     <exe> <RVA> [개수]   함수 디스어셈블
```

`disasm.py` 는 capstone 이 필요하다 (`pip install capstone`).

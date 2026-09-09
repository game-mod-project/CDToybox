# 장비 에디터 이식 설계 (소켓·연마·염색, both-realms, NPC 불필요)

**출처/저작권:** 소켓·장비 컴포넌트 매핑의 원출처는 **XeTrinityz/Trinity
(MIT, github.com/XeTrinityz/Trinity)**. 사용자가 받은 "Crimson Desert
Enhanced Ultimate" CT(Nexus 3209)가 이를 Cheat Engine 테이블로 이식했고,
그 CT의 Lua를 실측 참고해 우리 C++ 모드로 다시 이식한다.

## 핵심 착상 — "both realms"

메모리로 소켓/연마/염색을 직접 써도 **클라이언트 사본 하나만 쓰면 게임이
서버 사본에서 되쓴다**(우리 예전 poke 실패 원인). 참고 모드는 **클라
player + 서버 player 두 장비 테이블에 인스턴스 ID로 매칭해 둘 다 쓴다.**
한쪽만 쓰면 화면엔 보여도 저장에서 날아갈 수 있음. 재장착(RE-EQUIP)해야
표시됨.

## 능력·한계 (실측 확정)

- ✅ **NPC·핸들 없이:** 이미 열린 소켓에 보석 박기/빼기, 연마 변경, 염색.
  착용 장비(worn gear) 대상. cplayer 포인터 경로라 컨테이너 핸들 불필요.
- ~~⛔ **불가:** 잠긴 소켓 **열기**(Add).~~ **2026-09-08 에 뒤집혔다 - 된다.**
  참고 모드의 "A LOCKED socket is refused, never force unlocked" 를 그대로
  받아들인 것이 잘못이었다. 락은 레코드 `+0x70`(열린 칸 수)과 칸 `[4]`
  **둘뿐**이고 게임이 검증하지 않는다 - 게임 자신의 지급 코드(0x234F930)가
  그 둘만 쓴다. 실측: 열린 칸 0개짜리 장비를 5칸으로 열어 저장·재시작을
  넘겼다. `eq_unlock_sockets`(착용, both-realms) ·
  `socket_unlock_record`(인벤, 단일 쓰기)로 구현했고 화면에도 붙였다.
  근거: `specs/2026-09-07-socket-grant-unlock-research.md` 9절.
  (소켓-메시지 구동은 여전히 컨테이너 핸들=NPC 가 필요해 죽은 경로다 -
  지금 기능은 그 경로를 안 쓰고 레코드에 직접 쓴다.)

## 포인터 체인 (TU 2.0.0.2 / build 1.0.0.2692 기준, "레퍼런스 오프셋+8")

```
G (코어 전역)  ── AOBScan MGRCHAIN 으로 해석, rip+3 = G, +0x0F=pm, +0x32=blk, +0x36=mo
[G] +pm(0x30) +ent(0x50)      = 플레이어 액터 (= 인벤토리 컨테이너)
actor +0x68 +0x38             = 장비 컴포넌트   (comp+0x08 == actor 백참조로 검증)
  (없으면 actor 안에서 +0x08==actor 인 포인터를 0x400 범위 백참조 검색)
findEquipTable(comp)          = 착용장비 테이블 {arr, cnt, stride}
```

MGRCHAIN AOB (CT 그대로, 우리 mem::scanner 로 포팅):
```
48 8B 05 ?? ?? ?? ?? 48 8D 54 24 ?? 48 8B 48 ?? E8 ?? ?? ?? ?? 90 44 38 7C 24 ?? 0F 84
?? ?? ?? ?? 48 8B ?? 24 ?? 48 85 ?? 0F 84 ?? ?? ?? ?? 48 8B ?? 68 48 8B ?? B8 00 00 00
```
- G   = m + 7 + i32(m+3)
- pm  = u8(m+0x0F)   (0x30)
- blk = u8(m+0x32)   (0x68)
- mo  = u32(m+0x36)  (0xB8, 인벤 매니저용)
- ent = 0x50 (고정)
- 여러 사이트가 맞으면 전부 같은 값이어야(자기검증). 다르면 실패.

인벤 매니저(참고): `[G]+0x30+0x50+0x68+0xB8` = mgr; mgr+0x18 Container**,
mgr+0x20 u32 count; Container+0x00 slots, +0x08 slotCount, +0x10 id.

## findEquipTable(comp) — 착용장비 배열 찾기

comp+0..0x3F8 를 8바이트씩 훑어, 아래를 만족하는 배열을 점수화해 최고를 택함.
- 후보 stride: **0xD0**(TU1.17 확정, 우선) · 0xC8 · 0xC0 · 0xD8
- 직접 배열: comp+o 가 배열 포인터. 아니면 comp+o=desc, desc+{0x08,0,0x10,0x18}=arr,
  desc+{0x10,0x08,0x18,0x0C,0x20}=cnt(1..64).
- 점수 = (서로 다른 슬롯태그 수)*100 + 실제아이템수.
  - equipArrayScore: entry+0x08 하위16=key. 0xFFFF=빈칸(정상), 0<key<=0x4000 &&
    qty(+0x10)>0 && inst(+0x00)>0 = 실아이템.
  - equipTagScore: entry+(stride-8) 하위16 = 슬롯태그, 0..31, 유일해야 진짜 장비표.

## entry 필드 (worn gear)

```
+0x00  u64  인스턴스 ID (both-realms 매칭 키)
+0x08  u16(하위) 아이템 순번(key). 0xFFFF=빈 슬롯
+0x0A  u16  연마(refinement)
+0x10  u64  수량
+0x60  ptr  소켓 벡터 (아래)
+0x78  ptr  염색 벡터 ;  +0x80 u32 염색 레코드 수(1..12)
+(stride-8) u16 슬롯 태그
```

### 소켓 벡터 (entry+0x60 → sp), 6바이트/칸, k=0..4
```
sp + k*6:
  +0 u16  보석 순번(gear id).  빈칸 표기시 0xFFFF
  +2 u16  마커.  채움=0xFFFF, 빔=0
  +4 u8   인덱스.  0,1,2.. = 열림(해당 k와 같아야) ; 0xFF = **잠김**
  +5 u8   상태(이 빌드 4, 안 씀)
sockUnlocked(sp): k=0..4, +4==0xFF 면 중단, 그 전까지 개수. 잠긴 칸 못 씀.
socketWrite(entry,k,gear): sp=+0x60; r=sp+k*6; if u8(r+4)==0xFF return false(잠김);
  write u16(r)=gear; write u16(r+2)= (gear==0xFFFF?0:0xFFFF); read-back 검증.
```

### 연마 (entry+0x0A)
```
refineWrite(entry,lvl): write u16(entry+0x0A)=lvl; read-back 검증.
```

### 염색 (entry+0x78 dp, +0x80 dc)
```
레코드 16바이트: a=dp+i*16; a+6 u8=zone, a+7,8,9 = R,G,B.
dyeWrite(entry,rec,r,g,b): dc>0 && dc<=12 && rec<dc; write 3바이트 a+7. read-back.
zone 이 정체성(행 번호 아님). 12 zone/아이템, 일부만 보유.
```

## both-realms 쓰기

```
equipTabs(): 후보 액터 = { 클라 playerChar(), (심볼 있으면) 서버 player 액터 }.
  각 액터→장비컴포넌트(백참조)→findEquipTable → 서로 다른 arr 만 수집.
eqWriteBoth(inst, op, ...): 각 realm 테이블에서 equipEntryByInst(inst)로 entry 찾고
  op(refine/socket/dye) 적용. 쓴 realm 수 반환.
equipEntryByInst(t,inst): arr 훑어 u32(e+8)하위16 != 0xFFFF && u64(e)==inst.
```
클라만 있어도 화면엔 보임(저장 위해 서버도). 우리 모드는 서버 액터를
actor-getter/세션 해석으로 이미 잡으므로, 서버 realm 도 넣는다.

## 우리 모드 이식 계획

1. **G 해석**: mem::scanner 로 MGRCHAIN 스캔 → G,pm,blk,mo 디코드(자기검증).
   (RVA 박지 않음 - 패치 내성)
2. **플레이어 액터**: 클라 = [G]+0x30+0x50. 서버 = 우리 세션/actor-getter 로 잡은
   서버 액터(참고 모드의 player_ServerChildOnlyInGameActor 대체).
3. **장비 컴포넌트**: actor+0x68+0x38, comp+0x08==actor 검증. 실패시 백참조 검색.
4. **findEquipTable** 이식(위 점수화).
5. 읽기 뷰: 착용장비 목록 + 소켓/연마/염색 상태(READ ONLY 먼저 - 검증용).
6. 쓰기: socket/refine/dye, both-realms, read-back, 잠긴소켓 거부. 실험 UI.

## 검증 순서 (게임 켠 채, 쓰기 전 필수)

1. G/MGRCHAIN 이 우리 mem::scanner 로 유일 해석되는지.
2. [G]+0x30+0x50 이 유효 액터, +0x68+0x38 백참조 성립하는지.
3. findEquipTable 이 착용장비(슬롯태그 유일)를 찾는지 - READ 로 목록 확인.
4. 소켓 벡터 6바이트 구조·sockUnlocked 가 실제와 맞는지.
5. 서버 realm 액터도 같은 인스턴스로 잡히는지.
6. **버릴 세이브**에서 이미 열린 소켓에 보석 1개 both-realms 쓰기 → 재장착 →
   표시·저장 확인. 그 다음 연마·염색.

**주의:** 메모리 직접 쓰기라 오염 위험. 위 1~5(읽기) 검증 전 쓰기 금지.

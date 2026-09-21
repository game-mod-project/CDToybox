# 포획으로 얻는 탈것·반려동물 목록 (2026-09-05, 실측)

> **정리 (2026-09-21) — ✅ 참조 자료.** 라이브 메모리에서 뽑은 표이고 이후 문서가
> 원본으로 인용한다(`2026-09-08-companion-catalog-and-acquire-research.md`). 반박된 줄은 없다.

라이브 게임 메모리(PID 45420)에서 `CharacterInfoManager`(7250) 와
`MercenaryInfoManager`(21) 를 교차해 뽑았다. 스크래치 `catch_scan.py`.

## 읽는 법

- `CharacterInfo._mercenaryInfo`(+0xBE, u16) = **MercenaryInfo 레코드 행 번호**
  (0xFFFF = 없음). 행 1 Vehicle_Horse, 5 Vehicle_Special, 6 Vehicle, 8 Wagon,
  9 Pet, 11 Domestic, 12 Fish, 13 Insect … 이 값이 있어야 "동반자가 될 수 있는
  캐릭터"다. 880개.
- `_isCatchable`(+0x148) 은 **거의 모든 캐릭터가 1** 이다(동반자 아님 6175개
  포함 — 붙잡기·던지기 공용 플래그). 그러므로 포획 대상 판별은
  **용병 타입 + 내부 이름의 `_Wild`** 로 본다. `_Domestic`(마구간·농장 소유판),
  `_Saddle`/`_Bagpack`/`_WagonConnecter`(장비 변형), `Riding_*`(탑승 상태
  변형), `_Quest`/`Unique`(연출용) 는 포획이 아니라 상태 변형이다.
- `_isHirable`(+0x156) = 고용(소유 등록) 가능. Wild 는 전부 1.
- `_ownedMercenaryCharacterInfo`(+0x100) 은 Damian·Oongka 둘만 쓴다(동료 캐릭터).
  야생→소유 대응 필드는 아니다.

## 요약: 야생(`_Wild`) 포획 대상

| 타입 | 종 | 야생 개체 키 |
|---|---|---|
| Vehicle_Horse (말) | Ayut 6, Davrella 8, Lumif 10, Phuket 1, Pukret 5, Stefano 1, Stefero 7 | 아래 표 |
| Vehicle_Horse 전설마 (unique) | White_Horse 31377, Black_Horse 31378, RedHare_Horse 31379 | 도감 `*_Horse_Report` 와 1:1 |
| Vehicle_Special (특수 탑승물) | Wolf 16, White_Wolf 2, Bear/Black_Bear/Grizzly/Ride_Bear 4, Lion 1, Tiger 2 | 30019~30034, 30048/30108/30111/30116, 30374, 18037/20736 |
| Vehicle (짐승 탈것) | Camel/BactrianCamel/Female/Baby 5, Bull/Female/Baby 3, Longhorn/Female/Baby 3, Hamish/Female/Baby 3, Water_Buffalo 1, Ride 2 | 30067/30199/32386/32387, 32217/32322/30110, 32246/32396/32397, 32223/32260/32259, 17368, 30376/30378 |
| Pet (반려동물) | 야생 개체 약 120 (여우·토끼·고슴도치·너구리·족제비·다람쥐·오리·거위·매·독수리·올빼미·앵무·비둘기·까마귀·참새·꿩·아르마딜로·비버·오소리·스컹크·웜뱃·카카포·미어캣·오리너구리 …) | 본문 Pet 표 |
| Domestic (가축) | Pig/Female/Baby, Goat/Female/Kid, Sheep/Female, MilkCow ×2 | 32212/20071/32219, 32214/20065/32262, 32213/20067, 17977/18019 |
| Fish (물고기, 낚시) | 41종 전부 Wild | 본문 Fish 표 |

Dragon·WarMachine(ATAG)·Raptor·Ship·Wagon·Insect 은 **포획 대상이 없다**
(스토리 부여 또는 제작·구매). 개·고양이 상당수는 `_Wild` 가 아닌 마을 개체
(Animal_Dog_*/Cat_*) 로만 있어 포획이 아니라 친밀도 방식으로 보인다.

---

## 전체 표 (원본 추출)

캐릭터 7250개 중 용병 타입(_mercenaryInfo)이 붙은 것 880개. 아래는 동반자 타입(탈것·마차·펫·가축·물고기·곤충) 이면서 `_isCatchable=1` 인 것.

| 용병 타입 | 캐릭터 수 | 그중 포획 가능 |
|---|---|---|
| Mercenary_Main | 6 | 6 |
| Vehicle_Horse | 219 | 88 |
| Vehicle_Dragon | 1 | 0 |
| Vehicle_WarMachine | 2 | 0 |
| Vehicle_WarMachine_Raptor | 1 | 0 |
| Vehicle_Special | 63 | 47 |
| Vehicle | 69 | 35 |
| Wagon | 75 | 0 |
| Pet | 230 | 229 |
| Domestic | 33 | 33 |
| Fish | 48 | 41 |
| Insect | 76 | 0 |
| Mercenary_Melee | 24 | 24 |
| Mercenary_Range | 14 | 14 |
| Mercenary_Worker | 1 | 1 |
| Mercenary_Shop | 16 | 16 |
| Observer | 1 | 0 |
| RecoveryItem | 1 | 1 |

## Vehicle_Horse — 포획 가능 88개

종(species) 20개: Animal_Ayut(6), Animal_Black_Horse(1), Animal_Davrella(8), Animal_Lumif(10), Animal_Phuket(1), Animal_Pukret(5), Animal_RedHare_Horse(1), Animal_Stefano(1), Animal_Stefero(11), Animal_UniqueNpc_Horse(5), Animal_White_Horse(1), MainVehicle(1), Riding_Deer(1), Riding_Horse(1), Riding_Horse_Ayut(6), Riding_Horse_Dabrera(8), Riding_Horse_Lumiph(6), Riding_Horse_Pukre(6), Riding_Horse_Strapero(6), Riding_Horse_Tiuta(3)

| 키 | 내부 이름 | hirable | unique | ctype |
|---|---|---|---|---|
| 32340 | Animal_Ayut_Wild_32340 | 1 | 0 | 0 |
| 32591 | Animal_Ayut_Wild_32591 | 1 | 0 | 0 |
| 32592 | Animal_Ayut_Wild_32592 | 1 | 0 | 0 |
| 32593 | Animal_Ayut_Wild_32593 | 1 | 0 | 0 |
| 32594 | Animal_Ayut_Wild_32594 | 1 | 0 | 0 |
| 32595 | Animal_Ayut_Wild_32595 | 1 | 0 | 0 |
| 31378 | Animal_Black_Horse_Wild_31378 | 1 | 1 | 0 |
| 31370 | Animal_Davrella_Wild_31370 | 1 | 0 | 0 |
| 32531 | Animal_Davrella_Wild_32531 | 1 | 0 | 0 |
| 32532 | Animal_Davrella_Wild_32532 | 1 | 0 | 0 |
| 32533 | Animal_Davrella_Wild_32533 | 1 | 0 | 0 |
| 32534 | Animal_Davrella_Wild_32534 | 1 | 0 | 0 |
| 32535 | Animal_Davrella_Wild_32535 | 1 | 0 | 0 |
| 32606 | Animal_Davrella_Wild_32606 | 1 | 0 | 0 |
| 32607 | Animal_Davrella_Wild_32607 | 1 | 0 | 0 |
| 31357 | Animal_Lumif_Wild_31357 | 1 | 0 | 0 |
| 32501 | Animal_Lumif_Wild_32501 | 1 | 0 | 0 |
| 32502 | Animal_Lumif_Wild_32502 | 1 | 0 | 0 |
| 32503 | Animal_Lumif_Wild_32503 | 1 | 0 | 0 |
| 32504 | Animal_Lumif_Wild_32504 | 1 | 0 | 0 |
| 32505 | Animal_Lumif_Wild_32505 | 1 | 0 | 0 |
| 20498 | Animal_Lumif_Wild_32506 | 1 | 0 | 0 |
| 20499 | Animal_Lumif_Wild_32507 | 1 | 0 | 0 |
| 20500 | Animal_Lumif_Wild_32508 | 1 | 0 | 0 |
| 20501 | Animal_Lumif_Wild_32509 | 1 | 0 | 0 |
| 31373 | Animal_Phuket_Wild_31373 | 1 | 0 | 0 |
| 32546 | Animal_Pukret_Wild_32546 | 1 | 0 | 0 |
| 17350 | Animal_Pukret_Wild_32547 | 1 | 0 | 0 |
| 32548 | Animal_Pukret_Wild_32548 | 1 | 0 | 0 |
| 32549 | Animal_Pukret_Wild_32549 | 1 | 0 | 0 |
| 32550 | Animal_Pukret_Wild_32550 | 1 | 0 | 0 |
| 31379 | Animal_RedHare_Horse_Wild_31379 | 1 | 1 | 0 |
| 31364 | Animal_Stefano_Wild_31364 | 1 | 0 | 0 |
| 32701 | Animal_Stefero_Quest_32701 | 0 | 1 | 0 |
| 32704 | Animal_Stefero_Quest_32704 | 0 | 1 | 0 |
| 32705 | Animal_Stefero_Quest_32705 | 0 | 1 | 0 |
| 18014 | Animal_Stefero_Quest_32706 | 0 | 1 | 0 |
| 32516 | Animal_Stefero_Wild_32516 | 1 | 0 | 0 |
| 32517 | Animal_Stefero_Wild_32517 | 1 | 0 | 0 |
| 32518 | Animal_Stefero_Wild_32518 | 1 | 0 | 0 |
| 32519 | Animal_Stefero_Wild_32519 | 1 | 0 | 0 |
| 32520 | Animal_Stefero_Wild_32520 | 1 | 0 | 0 |
| 20502 | Animal_Stefero_Wild_32521 | 1 | 0 | 0 |
| 20503 | Animal_Stefero_Wild_32522 | 1 | 0 | 0 |
| 18423 | Animal_UniqueNpc_Horse_1 | 0 | 1 | 5 |
| 18425 | Animal_UniqueNpc_Horse_2 | 0 | 1 | 5 |
| 18426 | Animal_UniqueNpc_Horse_3 | 0 | 1 | 5 |
| 18431 | Animal_UniqueNpc_Horse_4 | 0 | 1 | 5 |
| 18437 | Animal_UniqueNpc_Horse_5 | 0 | 1 | 5 |
| 31377 | Animal_White_Horse_Wild_31377 | 1 | 1 | 0 |
| 17133 | MainVehicle | 1 | 1 | 5 |
| 18853 | Riding_Deer_Boss | 1 | 0 | 5 |
| 2011 | Riding_Horse_Ayut_2008 | 1 | 0 | 5 |
| 2052 | Riding_Horse_Ayut_2052 | 1 | 0 | 5 |
| 2053 | Riding_Horse_Ayut_2053 | 1 | 0 | 5 |
| 2054 | Riding_Horse_Ayut_2054 | 1 | 0 | 5 |
| 2055 | Riding_Horse_Ayut_2055 | 1 | 0 | 5 |
| 2056 | Riding_Horse_Ayut_2056 | 1 | 0 | 5 |
| 2002 | Riding_Horse_Dabrera_2002 | 1 | 0 | 5 |
| 2034 | Riding_Horse_Dabrera_2014 | 1 | 0 | 5 |
| 2031 | Riding_Horse_Dabrera_2031 | 1 | 0 | 5 |
| 2032 | Riding_Horse_Dabrera_2032 | 1 | 0 | 5 |
| 2033 | Riding_Horse_Dabrera_2033 | 1 | 0 | 5 |
| 2035 | Riding_Horse_Dabrera_2035 | 1 | 0 | 5 |
| 2057 | Riding_Horse_Dabrera_2057 | 1 | 0 | 5 |
| 2058 | Riding_Horse_Dabrera_2058 | 1 | 0 | 5 |
| 2000 | Riding_Horse_Lumiph_2000 | 1 | 0 | 5 |
| 2022 | Riding_Horse_Lumiph_2009 | 1 | 0 | 5 |
| 2023 | Riding_Horse_Lumiph_2010 | 1 | 0 | 5 |
| 2024 | Riding_Horse_Lumiph_2011 | 1 | 0 | 5 |
| 2025 | Riding_Horse_Lumiph_2012 | 1 | 0 | 5 |
| 2021 | Riding_Horse_Lumiph_2021 | 1 | 0 | 5 |
| 2005 | Riding_Horse_Pukre_2005 | 1 | 0 | 5 |
| 2036 | Riding_Horse_Pukre_2015 | 1 | 0 | 5 |
| 2037 | Riding_Horse_Pukre_2016 | 1 | 0 | 5 |
| 2038 | Riding_Horse_Pukre_2017 | 1 | 0 | 5 |
| 2039 | Riding_Horse_Pukre_2018 | 1 | 0 | 5 |
| 2040 | Riding_Horse_Pukre_2019 | 1 | 0 | 5 |
| 2001 | Riding_Horse_Strapero_2001 | 1 | 0 | 5 |
| 2027 | Riding_Horse_Strapero_2013 | 1 | 0 | 5 |
| 2026 | Riding_Horse_Strapero_2026 | 1 | 0 | 5 |
| 2028 | Riding_Horse_Strapero_2028 | 1 | 0 | 5 |
| 2029 | Riding_Horse_Strapero_2029 | 1 | 0 | 5 |
| 2030 | Riding_Horse_Strapero_2030 | 1 | 0 | 5 |
| 18133 | Riding_Horse_Tiuta_Unique_2050_Demian | 0 | 1 | 5 |
| 18132 | Riding_Horse_Tiuta_Unique_2050_Oongka | 0 | 1 | 5 |
| 20080 | Riding_Horse_Tiuta_Unique_2050_kliff | 1 | 1 | 5 |
| 17303 | Riding_Horse_Unique_Marius | 0 | 1 | 5 |

## Vehicle_Special — 포획 가능 47개

종(species) 18개: Animal(1), Animal_AlpineIbex(1), Animal_Bear(1), Animal_Black_Bear(1), Animal_Grizzly_Bear(1), Animal_Lion(2), Animal_Lion_Circus(1), Animal_Ride_Battle_Boar(6), Animal_Ride_Bear(1), Animal_Tiger(2), Animal_Trained_Bear(1), Animal_White_Wolf(2), Animal_Wolf(16), Riding_AlpineIbex(2), Riding_Bear(1), Riding_SandfangBandits_Wolf(1), Riding_Warthog(1), Riding_Wolf(6)

| 키 | 내부 이름 | hirable | unique | ctype |
|---|---|---|---|---|
| 19275 | Animal_AlpineIbex_10 | 1 | 0 | 0 |
| 30048 | Animal_Bear_Wild_30048 | 1 | 0 | 0 |
| 30108 | Animal_Black_Bear_Wild_30108 | 1 | 0 | 0 |
| 30191 | Animal_Domestic_Bear_Domestic_30191 | 1 | 0 | 0 |
| 30111 | Animal_Grizzly_Bear_Wild_30111 | 1 | 0 | 0 |
| 19276 | Animal_Lion_Circus_32349 | 1 | 0 | 0 |
| 20752 | Animal_Lion_Domestic_30374 | 1 | 0 | 0 |
| 30374 | Animal_Lion_Wild_30374 | 1 | 0 | 0 |
| 30271 | Animal_Ride_Battle_Boar_30271 | 1 | 0 | 0 |
| 20976 | Animal_Ride_Battle_Boar_30272 | 1 | 0 | 0 |
| 20977 | Animal_Ride_Battle_Boar_30273 | 1 | 0 | 0 |
| 20978 | Animal_Ride_Battle_Boar_30274 | 1 | 0 | 0 |
| 20979 | Animal_Ride_Battle_Boar_30275 | 1 | 0 | 0 |
| 20980 | Animal_Ride_Battle_Boar_30276 | 1 | 0 | 0 |
| 30116 | Animal_Ride_Bear_Wild_30116 | 1 | 0 | 0 |
| 18037 | Animal_Tiger_Wild_1 | 1 | 0 | 0 |
| 20736 | Animal_Tiger_Wild_2 | 1 | 1 | 0 |
| 16982 | Animal_Trained_Bear_59006 | 1 | 0 | 0 |
| 20729 | Animal_White_Wolf_Wild_30034 | 1 | 0 | 0 |
| 20970 | Animal_White_Wolf_Wild_30035 | 1 | 0 | 0 |
| 17969 | Animal_Wolf_Wild_30019 | 1 | 0 | 0 |
| 20955 | Animal_Wolf_Wild_30020 | 1 | 0 | 0 |
| 20956 | Animal_Wolf_Wild_30021 | 1 | 0 | 0 |
| 20957 | Animal_Wolf_Wild_30022 | 1 | 0 | 0 |
| 20958 | Animal_Wolf_Wild_30023 | 1 | 0 | 0 |
| 20959 | Animal_Wolf_Wild_30024 | 1 | 0 | 0 |
| 20960 | Animal_Wolf_Wild_30025 | 1 | 0 | 0 |
| 20961 | Animal_Wolf_Wild_30026 | 1 | 0 | 0 |
| 20962 | Animal_Wolf_Wild_30027 | 1 | 0 | 0 |
| 20963 | Animal_Wolf_Wild_30028 | 1 | 0 | 0 |
| 20964 | Animal_Wolf_Wild_30029 | 1 | 0 | 0 |
| 20965 | Animal_Wolf_Wild_30030 | 1 | 0 | 0 |
| 20966 | Animal_Wolf_Wild_30031 | 1 | 0 | 0 |
| 20967 | Animal_Wolf_Wild_30032 | 1 | 0 | 0 |
| 20968 | Animal_Wolf_Wild_30033 | 1 | 0 | 0 |
| 20969 | Animal_Wolf_Wild_30034 | 1 | 0 | 0 |
| 29448 | Riding_AlpineIbex_1 | 0 | 1 | 0 |
| 20875 | Riding_AlpineIbex_1000 | 1 | 1 | 0 |
| 17230 | Riding_Bear_1 | 1 | 0 | 0 |
| 20708 | Riding_SandfangBandits_Wolf_2083 | 1 | 1 | 5 |
| 17451 | Riding_Warthog_1 | 1 | 0 | 0 |
| 17224 | Riding_Wolf_1 | 1 | 0 | 0 |
| 20971 | Riding_Wolf_2 | 1 | 0 | 0 |
| 20972 | Riding_Wolf_3 | 1 | 0 | 0 |
| 20973 | Riding_Wolf_4 | 1 | 0 | 0 |
| 20974 | Riding_Wolf_5 | 1 | 0 | 0 |
| 20975 | Riding_Wolf_6 | 1 | 0 | 0 |

## Vehicle — 포획 가능 35개

종(species) 25개: Animal_Baby_Bull(1), Animal_Baby_Camel(1), Animal_Baby_Hamish(1), Animal_Baby_Longhorn(1), Animal_BactrianCamel(2), Animal_Bottle_Camel(1), Animal_Bull(2), Animal_Camel(2), Animal_Female_Bull(1), Animal_Female_Camel(1), Animal_Female_Hamish(2), Animal_Female_Longhorn(2), Animal_Giant_Bull(2), Animal_Hamish(1), Animal_Longhorn(2), Animal_Marni_GolemHorse(1), Animal_Merchant_Camel(1), Animal_Ride(2), Animal_Ride_Battle_Boar(2), Animal_Water_Buffalo(1), Animal_art_Camel(1), Animal_clothe_Camel(1), Animal_machinery_Camel(1), Animal_weapon_Camel(1), Riding_Wolf(2)

| 키 | 내부 이름 | hirable | unique | ctype |
|---|---|---|---|---|
| 30110 | Animal_Baby_Bull_Wild_30110 | 1 | 0 | 0 |
| 32387 | Animal_Baby_Camel_Wild_32387 | 1 | 0 | 0 |
| 32259 | Animal_Baby_Hamish_Wild_32259 | 1 | 0 | 0 |
| 32397 | Animal_Baby_Longhorn_Wild_32397 | 1 | 0 | 0 |
| 30200 | Animal_BactrianCamel_Domestic_30200 | 1 | 0 | 0 |
| 30199 | Animal_BactrianCamel_Wild_30199 | 1 | 0 | 0 |
| 32421 | Animal_Bottle_Camel_Domestic_Bagpack_32421 | 1 | 0 | 0 |
| 30034 | Animal_Bull_Domestic_30034 | 1 | 0 | 0 |
| 32217 | Animal_Bull_Wild_32217 | 1 | 0 | 0 |
| 30106 | Animal_Camel_Domestic_30106 | 1 | 0 | 0 |
| 30067 | Animal_Camel_Wild_30067 | 1 | 0 | 0 |
| 32322 | Animal_Female_Bull_Wild_32322 | 1 | 0 | 0 |
| 32386 | Animal_Female_Camel_Wild_32386 | 1 | 0 | 0 |
| 32398 | Animal_Female_Hamish_Domestic_32398 | 1 | 0 | 0 |
| 32260 | Animal_Female_Hamish_Wild_32260 | 1 | 0 | 0 |
| 18744 | Animal_Female_Longhorn_Domestic_1 | 1 | 0 | 0 |
| 32396 | Animal_Female_Longhorn_Wild_32396 | 1 | 0 | 0 |
| 32895 | Animal_Giant_Bull_32895 | 1 | 0 | 0 |
| 18438 | Animal_Giant_Bull_Domestic_1 | 1 | 0 | 0 |
| 32223 | Animal_Hamish_Wild_32223 | 1 | 0 | 0 |
| 18743 | Animal_Longhorn_Domestic_1 | 1 | 0 | 0 |
| 32246 | Animal_Longhorn_Wild_32246 | 1 | 0 | 0 |
| 2508 | Animal_Marni_GolemHorse_2508 | 0 | 0 | 5 |
| 32420 | Animal_Merchant_Camel_Domestic_Bagpack_32420 | 1 | 0 | 0 |
| 18573 | Animal_Ride_Battle_Boar_1 | 1 | 0 | 0 |
| 30270 | Animal_Ride_Battle_Boar_30270 | 1 | 0 | 0 |
| 30376 | Animal_Ride_Wild_30376 | 1 | 0 | 5 |
| 30378 | Animal_Ride_Wild_30378 | 1 | 0 | 5 |
| 17368 | Animal_Water_Buffalo_Wild_1 | 1 | 0 | 0 |
| 32423 | Animal_art_Camel_Domestic_Bagpack_32423 | 1 | 0 | 0 |
| 32422 | Animal_clothe_Camel_Domestic_Bagpack_32422 | 1 | 0 | 0 |
| 32418 | Animal_machinery_Camel_Domestic_Bagpack_32418 | 1 | 0 | 0 |
| 32419 | Animal_weapon_Camel_Domestic_Bagpack_32419 | 1 | 0 | 0 |
| 2504 | Riding_Wolf_2083 | 1 | 0 | 5 |
| 17480 | Riding_Wolf_Boss | 1 | 0 | 0 |

## Pet — 포획 가능 229개

종(species) 149개: Animal(1), Animal_AmericanBullDog(8), Animal_Arctic_Fox(2), Animal_Armadillo(1), Animal_Baby_GreatDane(8), Animal_Baby_Wyvern(1), Animal_Badger(1), Animal_Beagle(5), Animal_Beaver(1), Animal_Bicolor_BritishCat(1), Animal_Bicolor_BritishKitten(1), Animal_Bicolor_Cat(1), Animal_Bicolor_Kitten(1), Animal_BlackDuck(1), Animal_BlackTail_Flycatcher(1), Animal_Black_Cat(1), Animal_Black_Kitten(1), Animal_BlueJay(1), Animal_BritishCat(1), Animal_BritishKitten(1), Animal_Brown_BritishCat(1), Animal_Brown_BritishKitten(1), Animal_BullDog(8), Animal_Bunting_Bird(1), Animal_Cat(1), Animal_Cat_Delesyian(1), Animal_Cat_Demeniss(1), Animal_Cat_Hernand(1), Animal_Cat_Pailune(1), Animal_Cat_Pororin(1), Animal_Cat_RedOrc(1), Animal_Cat_Tashkalp(1), Animal_Choppy(1), Animal_CitronCockatoo(2), Animal_Collared_Dove(1), Animal_Condor(1), Animal_Crow(1), Animal_Cuckoo(1), Animal_Desert_Cat(1), Animal_Desert_Fox(3), Animal_Desert_Goose(1), Animal_Desert_Rabbit(1), Animal_Dog_Delesyian(1), Animal_Dog_Demeniss(1), Animal_Dog_Hernand(2), Animal_Dog_JijeongTemple(1), Animal_Dog_LongleafTree(1), Animal_Dog_Pailune(1), Animal_Dog_Tashkalp(1), Animal_Duck(1), Animal_Eagle(1), Animal_EclectusParrot(2), Animal_EuropeanPartridge(1), Animal_FalcoCherrug(1), Animal_Fat_Bicolor_Cat(1), Animal_Fat_Black_Cat(1), Animal_Fat_Cat(1), Animal_Fat_RussianBlue_Cat(1), Animal_Fat_Tricolor_Cat(1), Animal_Fat_White_Cat(1), Animal_Fat_Yellow_Cat(1), Animal_Flying_Squirrel(1), Animal_Fox(6), Animal_Fulica(1), Animal_GaviaStellata(1), Animal_Goose(1), Animal_GreatDane(9), Animal_Greyhound(11), Animal_Hawk(1), Animal_Hedgehog(6), Animal_Husky(1), Animal_Jindodog(3), Animal_Kitten(1), Animal_Kiwi_Bird(1), Animal_KukuBirdbaby(1), Animal_Little_Grebe(1), Animal_Longtailedtit(1), Animal_Machine_Eagle(1), Animal_Mallard(1), Animal_Meerkat(1), Animal_Mergellus(1), Animal_Mesite(1), Animal_Norwegianforest(1), Animal_Oriole(1), Animal_Owl(1), Animal_Oxpecker(1), Animal_Parrot(3), Animal_Pigeon(2), Animal_Platypus(1), Animal_Porcupine(1), Animal_Porcupine_Desert(1), Animal_Pygmy_Teal(1), Animal_Rabbit(8), Animal_Raccoon(1), Animal_Ragdoll(1), Animal_RedPanda(1), Animal_Red_Footed_Falcon(1), Animal_Red_Squirrel(1), Animal_RoseringedParakeet(2), Animal_RuddyshelDuck(1), Animal_RussianBlue_Cat(1), Animal_RussianBlue_Kitten(1), Animal_Saluki(6), Animal_Seagull(2), Animal_Shepherd(1), Animal_Skinny_Bicolor_Cat(1), Animal_Skinny_Black_Cat(1), Animal_Skinny_Cat(1), Animal_Skinny_RussianBlue_Cat(1), Animal_Skinny_Tricolor_Cat(1), Animal_Skinny_White_Cat(1), Animal_Skinny_Yellow_Cat(1), Animal_Skunk(1), Animal_Skunky(1), Animal_SnowRabbit(1), Animal_Snow_Chipmunk(1), Animal_Snow_Weasel(1), Animal_Snowy_Plover(1), Animal_Sparrow(1), Animal_SpixsMacaw(2), Animal_Squirrel(1), Animal_Steppe_Eagle(1), Animal_Sterna(1), Animal_Striped_BritishCat(1), Animal_Striped_BritishKitten(1), Animal_SunConure(2), Animal_SyrrhaptesParadoxus(1), Animal_Takahe(1), Animal_Trained_Dog(1), Animal_Tricolor_Cat(1), Animal_Tricolor_Kitten(1), Animal_Watercock(1), Animal_Weasel(1), Animal_Whistling_Duck(1), Animal_WhiteBilled_Loon(1), Animal_WhiteHawk(1), Animal_WhiteWing_Flycatcher(1), Animal_White_BritishCat(1), Animal_White_BritishKitten(1), Animal_White_Cat(1), Animal_White_Kitten(1), Animal_White_Norwegianforest(1), Animal_Wombat(1), Animal_Wood_Pecker(1), Animal_Wood_Weasel(1), Animal_Yellow_Cat(1), Animal_Yellow_Kitten(1), Animal_kakapo(1), BlackWolf(1)

| 키 | 내부 이름 | hirable | unique | ctype |
|---|---|---|---|---|
| 20634 | Animal_AmericanBullDog_Domestic_01 | 1 | 0 | 0 |
| 20635 | Animal_AmericanBullDog_Domestic_02 | 1 | 0 | 0 |
| 20636 | Animal_AmericanBullDog_Domestic_03 | 1 | 0 | 0 |
| 20637 | Animal_AmericanBullDog_Domestic_04 | 1 | 0 | 0 |
| 20638 | Animal_AmericanBullDog_Domestic_05 | 1 | 0 | 0 |
| 20639 | Animal_AmericanBullDog_Domestic_06 | 1 | 0 | 0 |
| 20640 | Animal_AmericanBullDog_Domestic_07 | 1 | 0 | 0 |
| 18356 | Animal_AmericanBullDog_Domestic_1 | 1 | 0 | 0 |
| 30038 | Animal_Arctic_Fox_Wild_30038 | 1 | 0 | 0 |
| 21026 | Animal_Arctic_Fox_Wild_30039 | 1 | 0 | 0 |
| 30078 | Animal_Armadillo_Wild_30078 | 1 | 0 | 0 |
| 20648 | Animal_Baby_GreatDane_Domestic_01 | 1 | 0 | 0 |
| 20649 | Animal_Baby_GreatDane_Domestic_02 | 1 | 0 | 0 |
| 20650 | Animal_Baby_GreatDane_Domestic_03 | 1 | 0 | 0 |
| 20651 | Animal_Baby_GreatDane_Domestic_04 | 1 | 0 | 0 |
| 20652 | Animal_Baby_GreatDane_Domestic_08 | 1 | 0 | 0 |
| 20653 | Animal_Baby_GreatDane_Domestic_09 | 1 | 0 | 0 |
| 20654 | Animal_Baby_GreatDane_Domestic_10 | 1 | 0 | 0 |
| 31336 | Animal_Baby_GreatDane_Domestic_31336 | 1 | 0 | 0 |
| 21005 | Animal_Baby_Wyvern_1 | 1 | 0 | 0 |
| 30142 | Animal_Badger_Wild_30142 | 1 | 0 | 0 |
| 20670 | Animal_Beagle_Domestic_01 | 1 | 0 | 0 |
| 20671 | Animal_Beagle_Domestic_02 | 1 | 0 | 0 |
| 20672 | Animal_Beagle_Domestic_03 | 1 | 0 | 0 |
| 20673 | Animal_Beagle_Domestic_04 | 1 | 0 | 0 |
| 32189 | Animal_Beagle_Domestic_32189 | 1 | 0 | 0 |
| 30139 | Animal_Beaver_Wild_30139 | 1 | 0 | 0 |
| 17318 | Animal_Bicolor_BritishCat_Domestic | 1 | 0 | 0 |
| 17326 | Animal_Bicolor_BritishKitten_Domestic | 1 | 0 | 0 |
| 17275 | Animal_Bicolor_Cat_Domestic | 1 | 0 | 0 |
| 17309 | Animal_Bicolor_Kitten_Domestic | 1 | 0 | 0 |
| 30172 | Animal_BlackDuck_Wild_30172 | 1 | 0 | 0 |
| 32366 | Animal_BlackTail_Flycatcher_Wild_32366 | 1 | 0 | 0 |
| 17274 | Animal_Black_Cat_Domestic | 1 | 0 | 0 |
| 17308 | Animal_Black_Kitten_Domestic | 1 | 0 | 0 |
| 30079 | Animal_BlueJay_Wild_30079 | 1 | 0 | 0 |
| 31467 | Animal_BritishCat_Domestic_31467 | 1 | 0 | 0 |
| 31468 | Animal_BritishKitten_Domestic_31468 | 1 | 0 | 0 |
| 17316 | Animal_Brown_BritishCat_Domestic | 1 | 0 | 0 |
| 17324 | Animal_Brown_BritishKitten_Domestic | 1 | 0 | 0 |
| 20627 | Animal_BullDog_Domestic_02 | 1 | 0 | 0 |
| 20628 | Animal_BullDog_Domestic_03 | 1 | 0 | 0 |
| 20629 | Animal_BullDog_Domestic_04 | 1 | 0 | 0 |
| 20630 | Animal_BullDog_Domestic_05 | 1 | 0 | 0 |
| 20631 | Animal_BullDog_Domestic_06 | 1 | 0 | 0 |
| 20632 | Animal_BullDog_Domestic_07 | 1 | 0 | 0 |
| 20633 | Animal_BullDog_Domestic_08 | 1 | 0 | 0 |
| 30186 | Animal_BullDog_Domestic_30186 | 1 | 0 | 0 |
| 32367 | Animal_Bunting_Bird_Wild_32367 | 1 | 0 | 0 |
| 17297 | Animal_Cat_Delesyian_Unique_1 | 1 | 0 | 0 |
| 17296 | Animal_Cat_Demeniss_Unique_1 | 1 | 0 | 0 |
| 30024 | Animal_Cat_Domestic_30024 | 1 | 0 | 0 |
| 17295 | Animal_Cat_Hernand_Unique_1 | 1 | 0 | 0 |
| 17298 | Animal_Cat_Pailune_Unique_1 | 1 | 0 | 0 |
| 18813 | Animal_Cat_Pororin_Unique_1 | 1 | 0 | 0 |
| 18814 | Animal_Cat_RedOrc_Unique_1 | 1 | 0 | 0 |
| 19859 | Animal_Cat_Tashkalp_Unique_1 | 1 | 0 | 0 |
| 32357 | Animal_Choppy_Wild_32357 | 1 | 0 | 0 |
| 32886 | Animal_CitronCockatoo_Wild_32886 | 1 | 0 | 0 |
| 20891 | Animal_CitronCockatoo_Wild_32887 | 1 | 0 | 0 |
| 32411 | Animal_Collared_Dove_Wild_32411 | 1 | 0 | 0 |
| 31415 | Animal_Condor_Wild_31415 | 1 | 0 | 0 |
| 30029 | Animal_Crow_Wild_30029 | 1 | 0 | 0 |
| 32038 | Animal_Cuckoo_Wild_32038 | 1 | 0 | 0 |
| 32033 | Animal_Desert_Cat_Domestic_32033 | 1 | 0 | 0 |
| 31410 | Animal_Desert_Fox_Wild_31410 | 1 | 0 | 0 |
| 21027 | Animal_Desert_Fox_Wild_31411 | 1 | 0 | 0 |
| 21028 | Animal_Desert_Fox_Wild_31412 | 1 | 0 | 0 |
| 18849 | Animal_Desert_Goose_Wild_1 | 1 | 0 | 0 |
| 31409 | Animal_Desert_Rabbit_Wild_31409 | 1 | 0 | 0 |
| 17330 | Animal_Dog_Delesyian_Unique_1 | 1 | 0 | 0 |
| 17329 | Animal_Dog_Demeniss_Unique_1 | 1 | 0 | 0 |
| 17328 | Animal_Dog_Hernand_Unique_1 | 1 | 0 | 0 |
| 19231 | Animal_Dog_Hernand_Unique_2 | 1 | 0 | 0 |
| 18815 | Animal_Dog_JijeongTemple_Unique_1 | 1 | 0 | 0 |
| 18816 | Animal_Dog_LongleafTree_Unique_1 | 1 | 0 | 0 |
| 17331 | Animal_Dog_Pailune_Unique_1 | 1 | 0 | 0 |
| 19860 | Animal_Dog_Tashkalp_Unique_1 | 1 | 0 | 0 |
| 30020 | Animal_Duck_Wild_30020 | 1 | 0 | 0 |
| 30050 | Animal_Eagle_Wild_30050 | 1 | 0 | 0 |
| 32881 | Animal_EclectusParrot_Wild_32881 | 1 | 0 | 0 |
| 20886 | Animal_EclectusParrot_Wild_32882 | 1 | 0 | 0 |
| 17740 | Animal_EuropeanPartridge_Wild_30184 | 1 | 0 | 0 |
| 32395 | Animal_FalcoCherrug_Wild_32395 | 1 | 0 | 0 |
| 17281 | Animal_Fat_Bicolor_Cat_Domestic | 1 | 0 | 0 |
| 17280 | Animal_Fat_Black_Cat_Domestic | 1 | 0 | 0 |
| 17277 | Animal_Fat_Cat_Domestic | 1 | 0 | 0 |
| 20919 | Animal_Fat_RussianBlue_Cat_Domestic | 1 | 0 | 0 |
| 17282 | Animal_Fat_Tricolor_Cat_Domestic | 1 | 0 | 0 |
| 17279 | Animal_Fat_White_Cat_Domestic | 1 | 0 | 0 |
| 17278 | Animal_Fat_Yellow_Cat_Domestic | 1 | 0 | 0 |
| 17764 | Animal_Flying_Squirrel_Wild_1 | 1 | 0 | 0 |
| 30046 | Animal_Fox_Wild_30046 | 1 | 0 | 0 |
| 21021 | Animal_Fox_Wild_30047 | 1 | 0 | 0 |
| 21022 | Animal_Fox_Wild_30048 | 1 | 0 | 0 |
| 21023 | Animal_Fox_Wild_30049 | 1 | 0 | 0 |
| 21024 | Animal_Fox_Wild_30050 | 1 | 0 | 0 |
| 21025 | Animal_Fox_Wild_30051 | 1 | 0 | 0 |
| 17743 | Animal_Fulica_Wild_1 | 1 | 0 | 0 |
| 32267 | Animal_GaviaStellata_Wild_32267 | 1 | 0 | 0 |
| 30072 | Animal_Goose_Wild_30072 | 1 | 0 | 0 |
| 20641 | Animal_GreatDane_Domestic_01 | 1 | 0 | 0 |
| 20642 | Animal_GreatDane_Domestic_02 | 1 | 0 | 0 |
| 20643 | Animal_GreatDane_Domestic_03 | 1 | 0 | 0 |
| 20644 | Animal_GreatDane_Domestic_04 | 1 | 0 | 0 |
| 20645 | Animal_GreatDane_Domestic_08 | 1 | 0 | 0 |
| 20646 | Animal_GreatDane_Domestic_09 | 1 | 0 | 0 |
| 20647 | Animal_GreatDane_Domestic_10 | 1 | 0 | 0 |
| 30022 | Animal_GreatDane_Domestic_30022 | 1 | 0 | 0 |
| 32427 | Animal_GreatDane_Domestic_Bagpack_32427 | 1 | 0 | 0 |
| 20660 | Animal_Greyhound_Domestic_01 | 1 | 0 | 0 |
| 20661 | Animal_Greyhound_Domestic_02 | 1 | 0 | 0 |
| 20662 | Animal_Greyhound_Domestic_03 | 1 | 0 | 0 |
| 20663 | Animal_Greyhound_Domestic_04 | 1 | 0 | 0 |
| 20664 | Animal_Greyhound_Domestic_05 | 1 | 0 | 0 |
| 20665 | Animal_Greyhound_Domestic_06 | 1 | 0 | 0 |
| 20666 | Animal_Greyhound_Domestic_07 | 1 | 0 | 0 |
| 20667 | Animal_Greyhound_Domestic_08 | 1 | 0 | 0 |
| 20668 | Animal_Greyhound_Domestic_09 | 1 | 0 | 0 |
| 20669 | Animal_Greyhound_Domestic_10 | 1 | 0 | 0 |
| 32181 | Animal_Greyhound_Domestic_32181 | 1 | 0 | 0 |
| 30059 | Animal_Hawk_Wild_30059 | 1 | 0 | 0 |
| 30045 | Animal_Hedgehog_Wild_30045 | 1 | 0 | 0 |
| 31504 | Animal_Hedgehog_Wild_31504 | 1 | 0 | 0 |
| 31505 | Animal_Hedgehog_Wild_31505 | 1 | 0 | 0 |
| 31506 | Animal_Hedgehog_Wild_31506 | 1 | 0 | 0 |
| 31507 | Animal_Hedgehog_Wild_31507 | 1 | 0 | 0 |
| 31510 | Animal_Hedgehog_Wild_31510 | 1 | 0 | 0 |
| 30070 | Animal_Husky_Domestic_30070 | 1 | 0 | 0 |
| 20625 | Animal_Jindodog_Domestic_02 | 1 | 0 | 0 |
| 20626 | Animal_Jindodog_Domestic_03 | 1 | 0 | 0 |
| 19218 | Animal_Jindodog_Domestic_1 | 1 | 0 | 0 |
| 31511 | Animal_Kitten_Domestic_31511 | 1 | 0 | 0 |
| 17748 | Animal_Kiwi_Bird_Wild_1 | 1 | 0 | 0 |
| 21047 | Animal_KukuBirdbaby_Wild_1 | 1 | 0 | 0 |
| 18335 | Animal_Little_Grebe_Wild_1 | 1 | 0 | 0 |
| 20739 | Animal_Longtailedtit_Wild_32368 | 1 | 0 | 0 |
| 20909 | Animal_Machine_Eagle_1 | 1 | 1 | 0 |
| 30171 | Animal_Mallard_Wild_30171 | 1 | 0 | 0 |
| 17939 | Animal_Meerkat_Wild_1 | 1 | 0 | 0 |
| 32285 | Animal_Mergellus_Wild_32285 | 1 | 0 | 0 |
| 17760 | Animal_Mesite_Wild_1 | 1 | 0 | 0 |
| 30007 | Animal_Norwegianforest_Domestic_30007 | 1 | 0 | 0 |
| 30087 | Animal_Oriole_Wild_30087 | 1 | 0 | 0 |
| 30060 | Animal_Owl_Wild_30060 | 1 | 0 | 0 |
| 32247 | Animal_Oxpecker_Wild_32247 | 1 | 0 | 0 |
| 30082 | Animal_Parrot_Wild_30082 | 1 | 0 | 0 |
| 32883 | Animal_Parrot_Wild_32883 | 1 | 0 | 0 |
| 20888 | Animal_Parrot_Wild_32884 | 1 | 0 | 0 |
| 30081 | Animal_Pigeon_Wild_30081 | 1 | 0 | 0 |
| 20893 | Animal_Pigeon_Wild_30082 | 1 | 0 | 0 |
| 17883 | Animal_Platypus_Wild_1 | 1 | 0 | 0 |
| 17951 | Animal_Porcupine_Desert_Wild | 1 | 0 | 0 |
| 32261 | Animal_Porcupine_Wild_32261 | 1 | 0 | 0 |
| 18839 | Animal_Pygmy_Teal_Wild_1 | 1 | 0 | 0 |
| 30189 | Animal_Rabbit_Domestic_30189 | 1 | 0 | 0 |
| 21018 | Animal_Rabbit_Domestic_30191 | 1 | 0 | 0 |
| 21019 | Animal_Rabbit_Domestic_30192 | 1 | 0 | 0 |
| 21020 | Animal_Rabbit_Domestic_30193 | 1 | 0 | 0 |
| 30001 | Animal_Rabbit_Wild_30001 | 1 | 0 | 0 |
| 21015 | Animal_Rabbit_Wild_30003 | 1 | 0 | 0 |
| 21016 | Animal_Rabbit_Wild_30004 | 1 | 0 | 0 |
| 21017 | Animal_Rabbit_Wild_30005 | 1 | 0 | 0 |
| 30042 | Animal_Raccoon_Wild_30042 | 1 | 0 | 0 |
| 20912 | Animal_Ragdoll_Domestic | 1 | 0 | 0 |
| 20982 | Animal_RedPanda_Wild_30052 | 1 | 0 | 0 |
| 32380 | Animal_Red_Footed_Falcon_Wild_32380 | 1 | 0 | 0 |
| 32370 | Animal_Red_Squirrel_Wild_32370 | 1 | 0 | 0 |
| 32885 | Animal_RoseringedParakeet_Wild_32885 | 1 | 0 | 0 |
| 20890 | Animal_RoseringedParakeet_Wild_32886 | 1 | 0 | 0 |
| 32284 | Animal_RuddyshelDuck_Wild_32284 | 1 | 0 | 0 |
| 20911 | Animal_RussianBlue_Cat_Domestic | 1 | 0 | 0 |
| 20921 | Animal_RussianBlue_Kitten_Domestic | 1 | 0 | 0 |
| 20655 | Animal_Saluki_Domestic_01 | 1 | 0 | 0 |
| 20656 | Animal_Saluki_Domestic_02 | 1 | 0 | 0 |
| 20657 | Animal_Saluki_Domestic_03 | 1 | 0 | 0 |
| 20658 | Animal_Saluki_Domestic_04 | 1 | 0 | 0 |
| 20659 | Animal_Saluki_Domestic_05 | 1 | 0 | 0 |
| 31400 | Animal_Saluki_Domestic_31400 | 1 | 0 | 0 |
| 30084 | Animal_Seagull_Wild_30084 | 1 | 0 | 0 |
| 20892 | Animal_Seagull_Wild_30085 | 1 | 0 | 0 |
| 19007 | Animal_Shepherd_Domestic_1 | 1 | 0 | 0 |
| 17287 | Animal_Skinny_Bicolor_Cat_Domestic | 1 | 0 | 0 |
| 17286 | Animal_Skinny_Black_Cat_Domestic | 1 | 0 | 0 |
| 17283 | Animal_Skinny_Cat_Domestic | 1 | 0 | 0 |
| 20920 | Animal_Skinny_RussianBlue_Cat_Domestic | 1 | 0 | 0 |
| 17288 | Animal_Skinny_Tricolor_Cat_Domestic | 1 | 0 | 0 |
| 17285 | Animal_Skinny_White_Cat_Domestic | 1 | 0 | 0 |
| 17284 | Animal_Skinny_Yellow_Cat_Domestic | 1 | 0 | 0 |
| 30141 | Animal_Skunk_Wild_30141 | 1 | 0 | 0 |
| 32355 | Animal_Skunky_Wild_32355 | 1 | 0 | 0 |
| 30017 | Animal_SnowRabbit_Wild_30017 | 1 | 0 | 0 |
| 32035 | Animal_Snow_Chipmunk_Wild_32035 | 1 | 0 | 0 |
| 32034 | Animal_Snow_Weasel_Wild_32034 | 1 | 0 | 0 |
| 32352 | Animal_Snowy_Plover_Wild_32352 | 1 | 0 | 0 |
| 30071 | Animal_Sparrow_Wild_30071 | 1 | 0 | 0 |
| 32884 | Animal_SpixsMacaw_Wild_32884 | 1 | 0 | 0 |
| 20889 | Animal_SpixsMacaw_Wild_32885 | 1 | 0 | 0 |
| 30065 | Animal_Squirrel_Wild_30065 | 1 | 0 | 0 |
| 31420 | Animal_Steppe_Eagle_Wild_31420 | 1 | 0 | 0 |
| 32288 | Animal_Sterna_Wild_32288 | 1 | 0 | 0 |
| 17319 | Animal_Striped_BritishCat_Domestic | 1 | 0 | 0 |
| 17327 | Animal_Striped_BritishKitten_Domestic | 1 | 0 | 0 |
| 32882 | Animal_SunConure_Wild_32882 | 1 | 0 | 0 |
| 20887 | Animal_SunConure_Wild_32883 | 1 | 0 | 0 |
| 32381 | Animal_SyrrhaptesParadoxus_Wild_32381 | 1 | 0 | 0 |
| 17765 | Animal_Takahe_Wild_1 | 1 | 0 | 0 |
| 59002 | Animal_Trained_Dog_59002 | 1 | 0 | 0 |
| 17276 | Animal_Tricolor_Cat_Domestic | 1 | 0 | 0 |
| 17310 | Animal_Tricolor_Kitten_Domestic | 1 | 0 | 0 |
| 20876 | Animal_Unique_Macaw_Parrot_1 | 1 | 1 | 0 |
| 17763 | Animal_Watercock_Wild_1 | 1 | 0 | 0 |
| 30039 | Animal_Weasel_Wild_30039 | 1 | 0 | 0 |
| 18848 | Animal_Whistling_Duck_Wild_1 | 1 | 0 | 0 |
| 32266 | Animal_WhiteBilled_Loon_Wild_32266 | 1 | 0 | 0 |
| 31416 | Animal_WhiteHawk_Wild_31416 | 1 | 0 | 0 |
| 30009 | Animal_WhiteWing_Flycatcher_Wild_30009 | 1 | 0 | 0 |
| 17317 | Animal_White_BritishCat_Domestic | 1 | 0 | 0 |
| 17325 | Animal_White_BritishKitten_Domestic | 1 | 0 | 0 |
| 17273 | Animal_White_Cat_Domestic | 1 | 0 | 0 |
| 17307 | Animal_White_Kitten_Domestic | 1 | 0 | 0 |
| 17256 | Animal_White_Norwegianforest_Domestic | 1 | 0 | 0 |
| 17801 | Animal_Wombat_Wild_1 | 1 | 0 | 0 |
| 32040 | Animal_Wood_Pecker_Wild_32040 | 1 | 0 | 0 |
| 32359 | Animal_Wood_Weasel_Wild_32359 | 1 | 0 | 0 |
| 17272 | Animal_Yellow_Cat_Domestic | 1 | 0 | 0 |
| 17306 | Animal_Yellow_Kitten_Domestic | 1 | 0 | 0 |
| 17767 | Animal_kakapo_Wild_1 | 1 | 0 | 0 |
| 19465 | BlackWolf | 1 | 1 | 0 |

## Domestic — 포획 가능 33개

종(species) 16개: Animal(2), Animal_Baby_MilkCow(2), Animal_Baby_Pig(3), Animal_Chick(1), Animal_Duckling(2), Animal_Goat(2), Animal_GoatFemale(2), Animal_Hen(1), Animal_Kid(3), Animal_Lamb(2), Animal_MilkCow(4), Animal_Pig(2), Animal_PigFemale(2), Animal_Rooster(1), Animal_Sheep(2), Animal_SheepFemale(2)

| 키 | 내부 이름 | hirable | unique | ctype |
|---|---|---|---|---|
| 32399 | Animal_Baby_MilkCow_Domestic_32399 | 1 | 0 | 0 |
| 20724 | Animal_Baby_MilkCow_Domestic_Sale_32399 | 1 | 0 | 0 |
| 30114 | Animal_Baby_Pig_Domestic_30114 | 1 | 0 | 0 |
| 20727 | Animal_Baby_Pig_Domestic_Sale_30114 | 1 | 0 | 0 |
| 32219 | Animal_Baby_Pig_Wild_32219 | 1 | 0 | 0 |
| 30080 | Animal_Chick_Domestic_30080 | 1 | 0 | 0 |
| 20069 | Animal_Domestic_DuckFemale_Domestic_1 | 1 | 0 | 0 |
| 30151 | Animal_Domestic_Duck_Domestic_30151 | 1 | 0 | 0 |
| 32269 | Animal_Duckling_Domestic_32269 | 1 | 0 | 0 |
| 20728 | Animal_Duckling_Domestic_Sale_32269 | 1 | 0 | 0 |
| 20066 | Animal_GoatFemale_Domestic_1 | 1 | 0 | 0 |
| 20065 | Animal_GoatFemale_Wild_1 | 1 | 0 | 0 |
| 30027 | Animal_Goat_Domestic_30027 | 1 | 0 | 0 |
| 32214 | Animal_Goat_Wild_32214 | 1 | 0 | 0 |
| 30023 | Animal_Hen_Domestic_30023 | 1 | 0 | 0 |
| 30109 | Animal_Kid_Domestic_30109 | 1 | 0 | 0 |
| 20725 | Animal_Kid_Domestic_Sale_30109 | 1 | 0 | 0 |
| 32262 | Animal_Kid_Wild_32262 | 1 | 0 | 0 |
| 31178 | Animal_Lamb_Domestic_31178 | 1 | 0 | 0 |
| 20726 | Animal_Lamb_Domestic_Sale_31178 | 1 | 0 | 0 |
| 17937 | Animal_MilkCow_Domestic_30028 | 1 | 0 | 0 |
| 18016 | Animal_MilkCow_Domestic_32308 | 1 | 0 | 0 |
| 17977 | Animal_MilkCow_Wild_32215 | 1 | 0 | 0 |
| 18019 | Animal_MilkCow_Wild_32310 | 1 | 0 | 0 |
| 20072 | Animal_PigFemale_Domestic_1 | 1 | 0 | 0 |
| 20071 | Animal_PigFemale_Wild_1 | 1 | 0 | 0 |
| 30025 | Animal_Pig_Domestic_30025 | 1 | 0 | 0 |
| 32212 | Animal_Pig_Wild_32212 | 1 | 0 | 0 |
| 30015 | Animal_Rooster_Domestic_30015 | 1 | 0 | 0 |
| 20068 | Animal_SheepFemale_Domestic_1 | 1 | 0 | 0 |
| 20067 | Animal_SheepFemale_Wild_1 | 1 | 0 | 0 |
| 30026 | Animal_Sheep_Domestic_30026 | 1 | 0 | 0 |
| 32213 | Animal_Sheep_Wild_32213 | 1 | 0 | 0 |

## Fish — 포획 가능 41개

종(species) 41개: Animal_Big_Eel(1), Animal_Big_Sturgeon(1), Animal_Dune_Coelacanth(1), Animal_Dune_Golden_Coelacanth(1), Animal_FootballFish(1), Animal_Giant_MudFish(1), Animal_GoldenCarp(1), Animal_GoldenTench(1), Animal_Marni_MachineFish(1), Animal_Middle_BlackSeaBream(1), Animal_Middle_Burbot(1), Animal_Middle_Carp(1), Animal_Middle_Catfish(1), Animal_Middle_Cuvier(1), Animal_Middle_Lenok(1), Animal_Middle_Pikefish(1), Animal_Middle_Red_Seabream(1), Animal_Middle_Red_Snapper(1), Animal_Middle_Rock_Bream(1), Animal_Middle_Salmon(1), Animal_Middle_Sockeye_Salmon(1), Animal_Middle_Trout(1), Animal_Normal_Bass(1), Animal_Normal_Benjari(1), Animal_Normal_Crucian_Carp(1), Animal_Normal_Dover_Sole(1), Animal_Normal_Golden_mandarinFish(1), Animal_Normal_Mackerel(1), Animal_Normal_Perch(1), Animal_Normal_Tench(1), Animal_Normal_Yellow_Bass(1), Animal_PaddleFish(1), Animal_PagrusMajor(1), Animal_RedOpaleye(1), Animal_SevenGill(1), Animal_Small_BaliOgluChub_Fish(1), Animal_Small_Beyaz_Fish(1), Animal_Small_BlackbrowBleak_Fish(1), Animal_Small_Mudfish_Fish(1), Animal_Small_Sammy_Fish(1), Animal_Small_Schneider_Fish(1)

| 키 | 내부 이름 | hirable | unique | ctype |
|---|---|---|---|---|
| 18934 | Animal_Big_Eel_Wild_1 | 1 | 0 | 0 |
| 18937 | Animal_Big_Sturgeon_Wild_1 | 1 | 0 | 0 |
| 18303 | Animal_Dune_Coelacanth | 1 | 1 | 0 |
| 18313 | Animal_Dune_Golden_Coelacanth | 1 | 1 | 0 |
| 39111 | Animal_FootballFish_Wild_39111 | 1 | 0 | 0 |
| 39105 | Animal_Giant_MudFish_Wild_39105 | 1 | 0 | 0 |
| 39109 | Animal_GoldenCarp_Wild_39109 | 1 | 1 | 0 |
| 39107 | Animal_GoldenTench_Wild_39107 | 1 | 1 | 0 |
| 35223 | Animal_Marni_MachineFish_Wild_35223 | 1 | 1 | 0 |
| 18757 | Animal_Middle_BlackSeaBream_Wild_1 | 1 | 0 | 0 |
| 18170 | Animal_Middle_Burbot_Wild_1 | 1 | 0 | 0 |
| 19430 | Animal_Middle_Carp_Wild_1 | 1 | 0 | 0 |
| 18160 | Animal_Middle_Catfish_Wild_1 | 1 | 0 | 0 |
| 18909 | Animal_Middle_Cuvier_Wild_1 | 1 | 0 | 0 |
| 18168 | Animal_Middle_Lenok_Wild_1 | 1 | 0 | 0 |
| 18151 | Animal_Middle_Pikefish_Wild_1 | 1 | 0 | 0 |
| 18748 | Animal_Middle_Red_Seabream_Wild_1 | 1 | 0 | 0 |
| 18760 | Animal_Middle_Red_Snapper_Wild_1 | 1 | 0 | 0 |
| 18896 | Animal_Middle_Rock_Bream_Wild_1 | 1 | 0 | 0 |
| 18925 | Animal_Middle_Salmon_Wild_1 | 1 | 0 | 0 |
| 18928 | Animal_Middle_Sockeye_Salmon_Wild_1 | 1 | 0 | 0 |
| 18931 | Animal_Middle_Trout_Wild_1 | 1 | 0 | 0 |
| 18177 | Animal_Normal_Bass_Wild_1 | 1 | 0 | 0 |
| 18629 | Animal_Normal_Benjari_Wild_1 | 1 | 0 | 0 |
| 18188 | Animal_Normal_Crucian_Carp_Wild_1 | 1 | 0 | 0 |
| 18204 | Animal_Normal_Dover_Sole_Wild_1 | 1 | 0 | 0 |
| 18187 | Animal_Normal_Golden_mandarinFish_Wild_1 | 1 | 0 | 0 |
| 18899 | Animal_Normal_Mackerel_Wild_1 | 1 | 0 | 0 |
| 18176 | Animal_Normal_Perch_Wild_1 | 1 | 0 | 0 |
| 18152 | Animal_Normal_Tench_Wild_1 | 1 | 0 | 0 |
| 18178 | Animal_Normal_Yellow_Bass_Wild_1 | 1 | 0 | 0 |
| 39101 | Animal_PaddleFish_Wild_39101 | 1 | 0 | 0 |
| 39103 | Animal_PagrusMajor_Wild_39103 | 1 | 0 | 0 |
| 39104 | Animal_RedOpaleye_Wild_39104 | 1 | 0 | 0 |
| 39102 | Animal_SevenGill_Wild_39102 | 1 | 0 | 0 |
| 17351 | Animal_Small_BaliOgluChub_Fish_Wild_1 | 1 | 0 | 0 |
| 18145 | Animal_Small_Beyaz_Fish_Wild_1 | 1 | 0 | 0 |
| 17349 | Animal_Small_BlackbrowBleak_Fish_Wild_1 | 1 | 0 | 0 |
| 18153 | Animal_Small_Mudfish_Fish_Wild_1 | 1 | 0 | 0 |
| 18143 | Animal_Small_Sammy_Fish_Wild_1 | 1 | 0 | 0 |
| 17353 | Animal_Small_Schneider_Fish_Wild_1 | 1 | 0 | 0 |

## 참고: 포획 가능하지만 동반자 타입이 아닌 캐릭터 6175개 (용병 타입별)

(없음) 6113, Mercenary_Melee 24, Mercenary_Shop 16, Mercenary_Range 14, Mercenary_Main 6, RecoveryItem 1, Mercenary_Worker 1

# 게임이 알려 주는 필드 이름 (2026-09-03)

## 이 문서의 근거

게임의 데이터 역직렬화 함수가 필드마다 실패 메시지를 들고 있다.

```
lea  rdx, [rsi + 0x400]      <- 대상 필드 주소
mov  r8d, 2                  <- 크기
call 읽기
test al, al
jne  계속
lea  rax, "ItemInfo의 _maxEndurance를 읽어들이는데 실패했다."
```

메시지가 **필드 이름을 그대로 들고 있으므로**, 직전의
`lea rdx,[base+off]` 와 짝지으면 이름 -> 오프셋 표가 나온다.
`tools/rtti/fields.py` 가 그것을 뽑는다.

```
python tools/rtti/fields.py "...\bin64\CrimsonDesert.exe" ItemInfo
```

게임을 켜지 않고 **디스크 파일만** 보고 한다.

문자열은 **UTF-8** 이다. 처음에 CP949 로 잘못 읽었다 - 콘솔이
CP949 라 그냥 찍으면 깨져 보이는데, 그것을 파일 인코딩 문제로
읽었다. 터미널 코드페이지와 파일 인코딩은 다른 이야기다.

### 검증

이미 실측으로 알던 칸과 전부 맞는다.

| 우리가 알던 것 | 게임이 부르는 이름 |
|---|---|
| `+0x18` 최대 스택 | `_maxStackCount` |
| `+0xA3` 분류 | `_itemType` |
| `+0x210` 등급 | `_itemTier` |
| `+0x42` 0xFFFF 면 담금질 없음 | `_equipTypeInfo` |
| `+0x250` 담금질 상한+1 | `_enchantDataList` 의 개수 칸 |

`_enchantDataList` 가 `{ptr +0x248, u32 개수 +0x250}` 라서 상한이
"개수 - 1" 이었던 것이다 - 담금질 0..N 이 목록의 칸 하나씩이다.

오프셋이 겹치는 짝은 0자리다(109개 중). 겹치면 도구가 표시한다.

## 내구도 (드디어 이름이 붙었다)

| 필드 | 오프셋 | 뜻 |
|---|---|---|
| `_maxEndurance` | `+0x400` (u16) | **0xFFFF 면 내구도가 없는 아이템** |
| `_isDestoryWhenBroken` | `+0x1FC` (u8) | 부서지면 사라지는가 (게임의 오타 그대로) |
| `_repairDataList` | `+0x408` | 수리 데이터 목록 |
| `_brokenItemPrefixString` | `+0x40` | "부서진 ~" 접두사 |
| `_SharpnessData` | `+0x2E8` | 예리도 (변환 함수가 이걸로 자른다) |

아이템을 쓸 때의 거부 조건(RVA `0x1D9CBB0`)이 이 셋으로 이루어진다.

```
_maxEndurance != 0xFFFF  &&  인스턴스 내구도 <= 0  &&  _isDestoryWhenBroken
  -> 오류 0x36
```

변환 함수가 레코드 `+0x58` 을 `표 _SharpnessData(+0x2E8)` 로 자르므로
**레코드 `+0x58` 은 예리도**다.

`ItemCatalogEntry::max_endurance` 로 읽어 둔다. `probe items` 가
내구도·소켓·담금질을 함께 낸다.

## 아이템 인스턴스 (`ItemSaveData`)

세이브 구조의 프로퍼티 이름이다. 리플렉션이 **이름 기반**이라
오프셋은 안 나온다 - 이름만 안다.

```
_endurance  _sharpness  _isBroken  _enchantLevel  _stackCount
_averagePrice  _batteryStat  _maxBatteryStat  _useableCtc
_itemNo  _saveVersion  _subItemDataKeyRaw
_customizationAppearanceIndexKey  _armorDyeAppearanceIndexKey
```

담금질이 `_enchantLevel`, 개수가 `_stackCount` 인 것으로 보아 이것이
인벤토리 레코드가 담는 값들이다. `_endurance` 와 `_sharpness` 가
나란히 있는 것이 변환 함수에서 레코드 `+0x40` 과 `+0x58` 이 나란히
채워지는 것과 맞는다.

## `EquipTypeInfo`

| 필드 | 오프셋 |
|---|---|
| `_decreaseEndurancePercent` | `+0x28` |
| `_enableEnchant` | `+0x3A` |
| `_enableTransfer` | `+0x39` |
| `_onGuardDamageReductionPercent` | `+0x30` |
| `_isCriticalCollidable` | `+0x38` |

## 뽑을 수 있는 클래스 528개

클래스를 안 주면 나열한다.

```
python tools/rtti/fields.py <exe>
  GimmickInfo      207개    CharacterInfo   191개    ItemInfo    115개
  StageInfo         91개    QuestInfo        37개    SkillInfo    35개
  StatusInfo        34개    EquipInfoData    20개    EquipTypeInfo 19개
  ...
```

실패 메시지가 전부 4,661개다. 게임의 데이터 구조가 통째로 이름과
함께 들어 있는 셈이다 - 아이템 말고도 캐릭터 · 스킬 · 퀘스트 ·
기믹까지 같은 방법으로 뽑힌다.

`EquipInfoData` 20개는 전부 짝지어졌다 (`_decreaseEndurancePercent`
`+0x18`, `_isWeaponSlot` `+0x69` 등). `RepairData` 4개는 코드 모양이
달라 하나도 못 짝지었다 - 그런 클래스도 있다.

## `ItemInfo` 전체 (109개)

| 오프셋 | 필드 |
|---|---|
| `+0x8` | `_stringKey` |
| `+0x10` | `_isBlocked` |
| `+0x18` | `_maxStackCount` |
| `+0x20` | `_itemName` |
| `+0x30` | `_FactionManagementData` |
| `+0x40` | `_brokenItemPrefixString` |
| `+0x42` | `_equipTypeInfo` |
| `+0x48` | `_occupiedEquipSlotDataList` |
| `+0x58` | `_itemTagList` |
| `+0x68` | `_equipAbleHash` |
| `+0x70` | `_consumableTypeList` |
| `+0x90` | `_itemIconList` |
| `+0xA0` | `_mapIconPath` |
| `+0xA2` | `_useMapIconAlert` |
| `+0xA3` | `_itemType` |
| `+0xB0` | `_itemDesc` |
| `+0xD0` | `_itemDesc2` |
| `+0xF0` | `_equipableLevel` |
| `+0xF4` | `_categoryInfo` |
| `+0xF6` | `_knowledgeInfo` |
| `+0xF8` | `_knowledgeObtainType` |
| `+0xFA` | `_destroyEffecInfo` |
| `+0x100` | `_equipPassiveSkillList` |
| `+0x110` | `_useImmediately` |
| `+0x111` | `_applyMaxStackCap` |
| `+0x112` | `_extractAdditionalDropSetInfo` |
| `+0x114` | `_minimumExtractEnchantLevel` |
| `+0x118` | `_itemMemo` |
| `+0x120` | `_filterType` |
| `+0x128` | `_gimmickInfo` |
| `+0x130` | `_gimmickTagList` |
| `+0x140` | `_maxDropResultSubItemCount` |
| `+0x144` | `_useDropSetTarget` |
| `+0x145` | `_isAllGimmickSealable` |
| `+0x148` | `_sealableItemInfoList` |
| `+0x158` | `_sealableCharacterInfoList` |
| `+0x168` | `_sealableGimmickInfoList` |
| `+0x178` | `_sealableGimmickTagList` |
| `+0x188` | `_sealableTribeInfoList` |
| `+0x198` | `_sealableMoneyInfoList` |
| `+0x1A8` | `_deleteByGimmickUnlock` |
| `+0x1AA` | `_gimmickUnlockMessageLocalStringInfo` |
| `+0x1AC` | `_canDisassemble` |
| `+0x1B0` | `_transmutationMaterialGimmickList` |
| `+0x1C0` | `_transmutationMaterialItemList` |
| `+0x1D0` | `_transmutationMaterialItemGroupList` |
| `+0x1E0` | `_isRegisterTradeMarket` |
| `+0x1E8` | `_multiChangeInfoList` |
| `+0x1F8` | `_isEditorUsable` |
| `+0x1F9` | `_discardable` |
| `+0x1FA` | `_isDyeable` |
| `+0x1FB` | `_isEditableGrime` |
| `+0x1FC` | `_isDestoryWhenBroken` |
| `+0x1FD` | `_isHousingOnly` |
| `+0x1FE` | `_isExtractAbleItem` |
| `+0x1FF` | `_quickSlotIndex` |
| `+0x200` | `_reserveSlotTargetDataList` |
| `+0x210` | `_itemTier` |
| `+0x211` | `_isImportantItem` |
| `+0x212` | `_isRewardLootDrop` |
| `+0x213` | `_applyDropStatType` |
| `+0x218` | `_dropDefaultData` |
| `+0x248` | `_enchantDataList` |
| `+0x258` | `_priceList` |
| `+0x268` | `_dockingChildData` |
| `+0x270` | `_inventoryChangeData` |
| `+0x278` | `_defaultTexturePath` |
| `+0x280` | `_fixedPageDataList` |
| `+0x290` | `_dynamicPageDataList` |
| `+0x2A0` | `_inspectDataList` |
| `+0x2B0` | `_inspectAction` |
| `+0x2C0` | `_defaultSubItem` |
| `+0x2E0` | `_itemChargeType` |
| `+0x2E1` | `_usableAlertType` |
| `+0x2E8` | `_SharpnessData` |
| `+0x340` | `_hackableCharacterGroupInfoList` |
| `+0x350` | `_itemGroupInfoList` |
| `+0x360` | `_discardOffsetY` |
| `+0x364` | `_discardAttachTerrain` |
| `+0x365` | `_hideFromInventoryOnPopItem` |
| `+0x366` | `_isShieldItem` |
| `+0x367` | `_isTowerShieldItem` |
| `+0x368` | `_isWild` |
| `+0x36A` | `_packedItemInfo` |
| `+0x36C` | `_unpackedItemInfo` |
| `+0x36E` | `_convertItemInfoByDropNPC` |
| `+0x370` | `_stageInfo` |
| `+0x378` | `_patternDescriptionDataList` |
| `+0x388` | `_lookDetailGameAdviceInfoWrapper` |
| `+0x38A` | `_lookDetailMissionInfo` |
| `+0x38C` | `_enableAlertSystemToUI` |
| `+0x38D` | `_isSaveGameDataAtUseItem` |
| `+0x38E` | `_isLogoutAtUseItem` |
| `+0x390` | `_sharedCoolTimeGroupNameHash` |
| `+0x398` | `_itemBundleDataList` |
| `+0x3A8` | `_moneyTypeDefine` |
| `+0x3B0` | `_emojiTextureID` |
| `+0x3B8` | `_enableEquipInCloneActor` |
| `+0x3B9` | `_isBlockedStoreSell` |
| `+0x3BA` | `_isPreorderItem` |
| `+0x3BB` | `_isHasItemUseDataInventoryBuff` |
| `+0x3BC` | `_isPreservedOnExtract` |
| `+0x3BE` | `_itemEffectInfo` |
| `+0x3C0` | `_factionManagementData` |
| `+0x3F0` | `_useAveragePrice` |
| `+0x3F8` | `_respawnTimeSeconds` |
| `+0x400` | `_maxEndurance` |
| `+0x408` | `_repairDataList` |
| `+0x418` | `_prefabDataList` |

메시지 124개 중 109개를 짝지었다. 나머지는 `lea rdx,[rsi+rax*2]`
처럼 오프셋이 상수가 아닌 자리라 이 방법으로는 못 잡는다 - 도구가
그런 자리를 물려받지 않고 버리므로, 짝지어진 것은 믿어도 된다.

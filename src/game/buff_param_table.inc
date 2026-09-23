// 생성물이다 - 손으로 고치지 말 것.
//   python tools/rtti/buff_params.py <덤프이미지> <vtable목록> \
//          --out src/game/buff_param_table.inc
//
// BuffData 파생 클래스의 vtable -> 효과 문구 값이 사는 칸과 배율.
// 뽑는 방법과 함정은 `tools/rtti/buff_params.py` 머리 주석, 근거는
// `docs/superpowers/specs/2026-09-22-item-description-effects-design.md` §4.7-H''.
//
// 표에 없는 클래스 = **모름**이다(`buff_param_divisor` 가 0 을 돌려준다).
// 그런 효과 줄은 버리고 세기만 한다 - 틀린 숫자를 보이는 것보다 낫다.
//
// `note` 에 붙는 말
//   종류 N      - 이 규칙이 채우는 파라미터 종류. {Param0..3}=0..3,
//                 {|Param0..3|}=4..7, {RepeatTick}=8.
//   종류 N: ... - 같은 클래스가 **다른 칸**도 내준다는 뜻이다. 한 줄에 규칙
//                 하나만 담을 수 있어 가장 낮은 종류를 대표로 적었다.
//   절대표시 0  - `_isDisplayAbsoluteNumber`(슬롯 11 의 r9b)가 1 이 아니면
//                 배율이 저렇게 달라진다. 표에 적은 값은 **1 갈래**다(실측).
//   겹침 증분   - 게임은 그 칸이 아니라 `[칸] + [증분칸] * 겹침수` 를 쓴다.
//                 툴팁은 겹침수 1 로 부른다.
//
// 이미지 cdtb_image.exe
// 클래스 28종 중 19종에서 규칙을 얻었다.
// 못 얻은 9종:
//   0x1458FADE8  IgnoreUseResourceStatBuffData      표 조회(이름)만 있음
//   0x1458FAFF0  LogoutTimeDropSetKeyBuffData       파라미터 없음
//   0x1458FB6E0  StatusExpBuffData                  파라미터 없음
//   0x1458FB818  VoidPassiveBuffData                파라미터 없음
//   0x1458FB880  VoidActiveBuffData                 파라미터 없음
//   0x1458FC2A0  ImmuneBuffData                     파라미터 없음
//   0x1458FCAF8  BlockCrimeBuffData                 파라미터 없음
//   0x1458FD658  VarySkillDamagePercentStatBuffData 파라미터 없음
//   0x1458FDF48  DefenceBuffData                    파라미터 없음
// vtable 주소는 모듈 고정 VA(기준 0x140000000)다. 게임이 갱신되면 다시 돌린다.

constexpr BuffParamRule kBuffParamRules[] = {
    {0x1458FA908ULL, 0x98, 10000.0,
     "AddCritiacalRateByMaterialKeyBuffData · 종류 1,5"},
    {0x1458FAD18ULL, 0x90, 1.0,
     "AddDamageBonusFromComboBuffData · 종류 1"},
    {0x1458FAF20ULL, 0x98, 10000.0,
     "RegisterCrimePriceRateBuffData · 종류 1,5"},
    {0x1458FB0C0ULL, 0x94, 1.0,
     "VaryCustomIntInGameContentsBuffData · 종류 1,5"},
    {0x1458FB128ULL, 0x98, 10000.0,
     "AddPercentInGameContentsBuffData · 종류 1,5"},
    {0x1458FB1F8ULL, 0x98, 10000.0,
     "TribeAdditionalDamageRateBuffData · 종류 2,6 · 종류 1,5: 표 조회(이름)"},
    {0x1458FB5A0ULL, 0x98, 10000000.0,
     "VaryDataDefinedStatRateBuffData · 종류 2,6 · 절대표시 0 이면 +0x98 ÷1000"},
    {0x1458FB610ULL, 0x98, 10000.0,
     "VaryStatRateBuffData · 종류 1,5 · 절대표시 0 이면 +0x98 ÷1"},
    {0x1458FBC40ULL, 0x98, 10000000.0,
     "VaryDataDefinedRegenerateValueBuffData · 종류 1,5 · 절대표시 0 이면 +0x98 ÷1000"},
    {0x1458FBD20ULL, 0x98, 1000.0,
     "VaryStatMaxValueBuffData · 종류 1,5 · 겹침 증분 +0xA8"},
    {0x1458FBD88ULL, 0x98, 10000000.0,
     "VaryStatBuffData · 종류 1,5 · 절대표시 0 이면 +0x98 ÷1000 · 겹침 증분 +0xA8"},
    {0x1458FBDF0ULL, 0x98, 1000.0,
     "VaryStatOverMaxValueBuffData · 종류 1,5 · 겹침 증분 +0xA8"},
    {0x1458FC070ULL, 0x98, 10000000.0,
     "VaryStaticStatBuffData · 종류 1,5 · 절대표시 0 이면 +0x98 ÷1000 · thunk"},
    {0x1458FC1C0ULL, 0xA0, 1.0,
     "VaryStaticStatLevelBuffData · 종류 1,5 · thunk"},
    {0x1458FC230ULL, 0x98, 10000000.0,
     "VaryDataDefinedStatBuffData · 종류 1,5 · 절대표시 0 이면 +0x98 ÷1000 · thunk"},
    {0x1458FC370ULL, 0x98, 1000.0,
     "DamageBuffData · 종류 1,5 · 종류 3,7: +0xF8 ÷10000 · 겹침 증분 +0xA8"},
    {0x1458FC440ULL, 0x98, 10000.0,
     "VaryCollectDropRateBuffData · 종류 1,5"},
    {0x1458FD040ULL, 0x94, 1.0,
     "ChangeBuffLevelBuffData · 종류 1,5"},
    {0x1458FD728ULL, 0x98, 10000.0,
     "SetStatMinRateBuffData · 종류 1,5 · 종류 2,6: +0xA0 ÷10000"},
};

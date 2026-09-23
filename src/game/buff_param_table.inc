// 생성물이다 - 손으로 고치지 말 것.
//   python tools/rtti/buff_params.py <덤프이미지> <vtable목록> \
//          --out src/game/buff_param_table.inc
//
// BuffData 파생 클래스의 vtable -> 효과 문구 값이 사는 칸과 배율.
// 뽑는 방법과 함정은 `tools/rtti/buff_params.py` 머리 주석, 근거는
// `docs/superpowers/specs/2026-09-22-item-description-effects-design.md` §4.7-H''.
//
// 키가 **셋**이다: vtable · 파라미터 종류(슬롯 11 의 r8b) · BuffData `+0x3A`
// (슬롯 11 의 r9b). 같은 클래스라도 종류마다 칸이 다르고, `+0x3A` 값마다 배율이
// 다르다. 조합이 표에 없으면 **모름**이다(`buff_param_divisor` 가 0 을 돌려준다) -
// 그런 효과 줄은 버리고 세기만 한다. 틀린 숫자를 보이는 것보다 낫다.
//
// 종류 8({RepeatTick})은 여기 없다. 호출부가 슬롯 11 을 부르지 않고 BuffData
// `+0x28`(주기 ms)을 1000.0 으로 나눠 직접 만든다(RVA 0x1F29A51).
//
// `note` 에 붙는 말
//   절대값 표시 - 종류 4~7 = `{|ParamN|}` 자리. 게임이 부호를 떼고 보인다.
//   겹침 증분   - 게임은 그 칸이 아니라 `[칸] + [증분칸] * 겹침수` 를 쓴다.
//                 툴팁은 겹침수 1 로 부른다.
//   thunk       - 슬롯 11 이 점프 하나이고 본체는 딴 데 있다.
//
// 이미지 cdtb_image.exe
// 클래스 28종 중 19종에서 규칙 82줄을 얻었다.
// `+0x3A` 값에 따라 배율이 갈리는 클래스 6종:
//   0x1458FB5A0  VaryDataDefinedStatRateBuffData    종류 2: 0->÷1000 1->÷1e+07 · 종류 6: 0->÷1000 1->÷1e+07
//   0x1458FB610  VaryStatRateBuffData               종류 1: 0->÷1 1->÷10000 · 종류 5: 0->÷1 1->÷10000
//   0x1458FBC40  VaryDataDefinedRegenerateValueBuffData 종류 1: 0->÷1000 1->÷1e+07 · 종류 5: 0->÷1000 1->÷1e+07
//   0x1458FBD88  VaryStatBuffData                   종류 1: 0->÷1000 1->÷1e+07 · 종류 5: 0->÷1000 1->÷1e+07
//   0x1458FC070  VaryStaticStatBuffData             종류 1: 0->÷1000 1->÷1e+07 · 종류 5: 0->÷1000 1->÷1e+07
//   0x1458FC230  VaryDataDefinedStatBuffData        종류 1: 0->÷1000 1->÷1e+07 · 종류 5: 0->÷1000 1->÷1e+07
// 값이 숫자가 아니라 **표 조회(이름)** 인 조합 - 배율이 없어 줄을 내지 않았다:
//   0x1458FB1F8  TribeAdditionalDamageRateBuffData  종류 1,5
// 규칙을 못 얻은 9종:
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
    {0x1458FA908ULL, 1, 0, 0x98, 10000.0,
     "AddCritiacalRateByMaterialKeyBuffData"},
    {0x1458FA908ULL, 1, 1, 0x98, 10000.0,
     "AddCritiacalRateByMaterialKeyBuffData"},
    {0x1458FA908ULL, 5, 0, 0x98, 10000.0,
     "AddCritiacalRateByMaterialKeyBuffData · 절대값 표시"},
    {0x1458FA908ULL, 5, 1, 0x98, 10000.0,
     "AddCritiacalRateByMaterialKeyBuffData · 절대값 표시"},
    {0x1458FAD18ULL, 1, 0, 0x90, 1.0,
     "AddDamageBonusFromComboBuffData"},
    {0x1458FAD18ULL, 1, 1, 0x90, 1.0,
     "AddDamageBonusFromComboBuffData"},
    {0x1458FAF20ULL, 1, 0, 0x98, 10000.0,
     "RegisterCrimePriceRateBuffData"},
    {0x1458FAF20ULL, 1, 1, 0x98, 10000.0,
     "RegisterCrimePriceRateBuffData"},
    {0x1458FAF20ULL, 5, 0, 0x98, 10000.0,
     "RegisterCrimePriceRateBuffData · 절대값 표시"},
    {0x1458FAF20ULL, 5, 1, 0x98, 10000.0,
     "RegisterCrimePriceRateBuffData · 절대값 표시"},
    {0x1458FB0C0ULL, 1, 0, 0x94, 1.0,
     "VaryCustomIntInGameContentsBuffData"},
    {0x1458FB0C0ULL, 1, 1, 0x94, 1.0,
     "VaryCustomIntInGameContentsBuffData"},
    {0x1458FB0C0ULL, 5, 0, 0x94, 1.0,
     "VaryCustomIntInGameContentsBuffData · 절대값 표시"},
    {0x1458FB0C0ULL, 5, 1, 0x94, 1.0,
     "VaryCustomIntInGameContentsBuffData · 절대값 표시"},
    {0x1458FB128ULL, 1, 0, 0x98, 10000.0,
     "AddPercentInGameContentsBuffData"},
    {0x1458FB128ULL, 1, 1, 0x98, 10000.0,
     "AddPercentInGameContentsBuffData"},
    {0x1458FB128ULL, 5, 0, 0x98, 10000.0,
     "AddPercentInGameContentsBuffData · 절대값 표시"},
    {0x1458FB128ULL, 5, 1, 0x98, 10000.0,
     "AddPercentInGameContentsBuffData · 절대값 표시"},
    {0x1458FB1F8ULL, 2, 0, 0x98, 10000.0,
     "TribeAdditionalDamageRateBuffData"},
    {0x1458FB1F8ULL, 2, 1, 0x98, 10000.0,
     "TribeAdditionalDamageRateBuffData"},
    {0x1458FB1F8ULL, 6, 0, 0x98, 10000.0,
     "TribeAdditionalDamageRateBuffData · 절대값 표시"},
    {0x1458FB1F8ULL, 6, 1, 0x98, 10000.0,
     "TribeAdditionalDamageRateBuffData · 절대값 표시"},
    {0x1458FB5A0ULL, 2, 0, 0x98, 1000.0,
     "VaryDataDefinedStatRateBuffData"},
    {0x1458FB5A0ULL, 2, 1, 0x98, 10000000.0,
     "VaryDataDefinedStatRateBuffData"},
    {0x1458FB5A0ULL, 6, 0, 0x98, 1000.0,
     "VaryDataDefinedStatRateBuffData · 절대값 표시"},
    {0x1458FB5A0ULL, 6, 1, 0x98, 10000000.0,
     "VaryDataDefinedStatRateBuffData · 절대값 표시"},
    {0x1458FB610ULL, 1, 0, 0x98, 1.0,
     "VaryStatRateBuffData"},
    {0x1458FB610ULL, 1, 1, 0x98, 10000.0,
     "VaryStatRateBuffData"},
    {0x1458FB610ULL, 5, 0, 0x98, 1.0,
     "VaryStatRateBuffData · 절대값 표시"},
    {0x1458FB610ULL, 5, 1, 0x98, 10000.0,
     "VaryStatRateBuffData · 절대값 표시"},
    {0x1458FBC40ULL, 1, 0, 0x98, 1000.0,
     "VaryDataDefinedRegenerateValueBuffData"},
    {0x1458FBC40ULL, 1, 1, 0x98, 10000000.0,
     "VaryDataDefinedRegenerateValueBuffData"},
    {0x1458FBC40ULL, 5, 0, 0x98, 1000.0,
     "VaryDataDefinedRegenerateValueBuffData · 절대값 표시"},
    {0x1458FBC40ULL, 5, 1, 0x98, 10000000.0,
     "VaryDataDefinedRegenerateValueBuffData · 절대값 표시"},
    {0x1458FBD20ULL, 1, 0, 0x98, 1000.0,
     "VaryStatMaxValueBuffData · 겹침 증분 +0xA8"},
    {0x1458FBD20ULL, 1, 1, 0x98, 1000.0,
     "VaryStatMaxValueBuffData · 겹침 증분 +0xA8"},
    {0x1458FBD20ULL, 5, 0, 0x98, 1000.0,
     "VaryStatMaxValueBuffData · 절대값 표시 · 겹침 증분 +0xA8"},
    {0x1458FBD20ULL, 5, 1, 0x98, 1000.0,
     "VaryStatMaxValueBuffData · 절대값 표시 · 겹침 증분 +0xA8"},
    {0x1458FBD88ULL, 1, 0, 0x98, 1000.0,
     "VaryStatBuffData · 겹침 증분 +0xA8"},
    {0x1458FBD88ULL, 1, 1, 0x98, 10000000.0,
     "VaryStatBuffData · 겹침 증분 +0xA8"},
    {0x1458FBD88ULL, 5, 0, 0x98, 1000.0,
     "VaryStatBuffData · 절대값 표시 · 겹침 증분 +0xA8"},
    {0x1458FBD88ULL, 5, 1, 0x98, 10000000.0,
     "VaryStatBuffData · 절대값 표시 · 겹침 증분 +0xA8"},
    {0x1458FBDF0ULL, 1, 0, 0x98, 1000.0,
     "VaryStatOverMaxValueBuffData · 겹침 증분 +0xA8"},
    {0x1458FBDF0ULL, 1, 1, 0x98, 1000.0,
     "VaryStatOverMaxValueBuffData · 겹침 증분 +0xA8"},
    {0x1458FBDF0ULL, 5, 0, 0x98, 1000.0,
     "VaryStatOverMaxValueBuffData · 절대값 표시 · 겹침 증분 +0xA8"},
    {0x1458FBDF0ULL, 5, 1, 0x98, 1000.0,
     "VaryStatOverMaxValueBuffData · 절대값 표시 · 겹침 증분 +0xA8"},
    {0x1458FC070ULL, 1, 0, 0x98, 1000.0,
     "VaryStaticStatBuffData · thunk"},
    {0x1458FC070ULL, 1, 1, 0x98, 10000000.0,
     "VaryStaticStatBuffData · thunk"},
    {0x1458FC070ULL, 5, 0, 0x98, 1000.0,
     "VaryStaticStatBuffData · 절대값 표시 · thunk"},
    {0x1458FC070ULL, 5, 1, 0x98, 10000000.0,
     "VaryStaticStatBuffData · 절대값 표시 · thunk"},
    {0x1458FC1C0ULL, 1, 0, 0xA0, 1.0,
     "VaryStaticStatLevelBuffData · thunk"},
    {0x1458FC1C0ULL, 1, 1, 0xA0, 1.0,
     "VaryStaticStatLevelBuffData · thunk"},
    {0x1458FC1C0ULL, 5, 0, 0xA0, 1.0,
     "VaryStaticStatLevelBuffData · 절대값 표시 · thunk"},
    {0x1458FC1C0ULL, 5, 1, 0xA0, 1.0,
     "VaryStaticStatLevelBuffData · 절대값 표시 · thunk"},
    {0x1458FC230ULL, 1, 0, 0x98, 1000.0,
     "VaryDataDefinedStatBuffData · thunk"},
    {0x1458FC230ULL, 1, 1, 0x98, 10000000.0,
     "VaryDataDefinedStatBuffData · thunk"},
    {0x1458FC230ULL, 5, 0, 0x98, 1000.0,
     "VaryDataDefinedStatBuffData · 절대값 표시 · thunk"},
    {0x1458FC230ULL, 5, 1, 0x98, 10000000.0,
     "VaryDataDefinedStatBuffData · 절대값 표시 · thunk"},
    {0x1458FC370ULL, 1, 0, 0x98, 1000.0,
     "DamageBuffData · 겹침 증분 +0xA8"},
    {0x1458FC370ULL, 1, 1, 0x98, 1000.0,
     "DamageBuffData · 겹침 증분 +0xA8"},
    {0x1458FC370ULL, 3, 0, 0xF8, 10000.0,
     "DamageBuffData"},
    {0x1458FC370ULL, 3, 1, 0xF8, 10000.0,
     "DamageBuffData"},
    {0x1458FC370ULL, 5, 0, 0x98, 1000.0,
     "DamageBuffData · 절대값 표시 · 겹침 증분 +0xA8"},
    {0x1458FC370ULL, 5, 1, 0x98, 1000.0,
     "DamageBuffData · 절대값 표시 · 겹침 증분 +0xA8"},
    {0x1458FC370ULL, 7, 0, 0xF8, 10000.0,
     "DamageBuffData · 절대값 표시"},
    {0x1458FC370ULL, 7, 1, 0xF8, 10000.0,
     "DamageBuffData · 절대값 표시"},
    {0x1458FC440ULL, 1, 0, 0x98, 10000.0,
     "VaryCollectDropRateBuffData"},
    {0x1458FC440ULL, 1, 1, 0x98, 10000.0,
     "VaryCollectDropRateBuffData"},
    {0x1458FC440ULL, 5, 0, 0x98, 10000.0,
     "VaryCollectDropRateBuffData · 절대값 표시"},
    {0x1458FC440ULL, 5, 1, 0x98, 10000.0,
     "VaryCollectDropRateBuffData · 절대값 표시"},
    {0x1458FD040ULL, 1, 0, 0x94, 1.0,
     "ChangeBuffLevelBuffData"},
    {0x1458FD040ULL, 1, 1, 0x94, 1.0,
     "ChangeBuffLevelBuffData"},
    {0x1458FD040ULL, 5, 0, 0x94, 1.0,
     "ChangeBuffLevelBuffData · 절대값 표시"},
    {0x1458FD040ULL, 5, 1, 0x94, 1.0,
     "ChangeBuffLevelBuffData · 절대값 표시"},
    {0x1458FD728ULL, 1, 0, 0x98, 10000.0,
     "SetStatMinRateBuffData"},
    {0x1458FD728ULL, 1, 1, 0x98, 10000.0,
     "SetStatMinRateBuffData"},
    {0x1458FD728ULL, 2, 0, 0xA0, 10000.0,
     "SetStatMinRateBuffData"},
    {0x1458FD728ULL, 2, 1, 0xA0, 10000.0,
     "SetStatMinRateBuffData"},
    {0x1458FD728ULL, 5, 0, 0x98, 10000.0,
     "SetStatMinRateBuffData · 절대값 표시"},
    {0x1458FD728ULL, 5, 1, 0x98, 10000.0,
     "SetStatMinRateBuffData · 절대값 표시"},
    {0x1458FD728ULL, 6, 0, 0xA0, 10000.0,
     "SetStatMinRateBuffData · 절대값 표시"},
    {0x1458FD728ULL, 6, 1, 0xA0, 10000.0,
     "SetStatMinRateBuffData · 절대값 표시"},
};

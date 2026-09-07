#pragma once

#include "mem/reader.h"

namespace cdtb::game {

// 특수기능 아이템 표시 보정.
//
// 특수기능 아이템을 치트로 지급하면 특수 상태값(레코드 +0x30 게이지 최대,
// +0x40 내구도 등)이 0 이라, 크래시(specguard 가 막음)는 없어도 게이지·
// 내구도 표시가 기본값(0)으로 보인다. 저장/로드하면 게임이 채워 정상이
// 되지만, 그 전에 아이템표 기준값을 레코드에 써넣어 표시를 맞춘다.
//
// 인스턴스별 계산값(+0x38 게이지 현재치)은 재현 불가라 그 부분만은
// 저장/로드해야 정확하다. 분석 루프에서 주기적으로 부른다(값싼 in-process
// 쓰기, 이미 채워진 것은 건너뛴다).
void heal_special_items(const mem::Reader& reader);

}  // namespace cdtb::game

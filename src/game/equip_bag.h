#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "game/equip.h"
#include "mem/reader.h"

// 장비 표에 섞인 가방 아이템. 장비 표(EquipSlotActorComponent)에는 실제로 입은 장비 말고
// 가방에 든 장비도 슬롯 태그 23 으로 들어 있다 - 게임 툴팁이 "비활성화" 라고 부르는 것이다.
// 그런 장비는 인벤토리(가방) 레코드가 진짜다: 장비 표에만 쓴 소켓 5칸이 착용 변경 뒤 가방
// 레코드의 2칸으로 돌아갔고, 게임 툴팁도 가방 레코드와 같았다(2026-09-22 실측, 클리프 21개 중
// 방패 하나 - 나머지 20개는 어느 인벤토리 컨테이너에도 없다).

namespace cdtb::game {

// 레코드 하나의 담금질(+0x0A)·연마(+0x58)·소켓(+0x60 벡터)을 w 에 읽는다. 장비 표 entry 와
// 인벤토리 레코드가 같은 자리다(inventory.h 실측 주석). 소켓 벡터를 못 읽으면 다섯 칸 모두
// 잠김(기본값)이다. 염색(+0x78)은 읽지 않는다 - 가방 레코드 쪽 자리를 확인하지 않았다.
void read_level_and_sockets(const mem::Reader& reader, std::uintptr_t record,
                            WornPiece* w);

// 인벤토리 컴포넌트의 모든 레코드를 인스턴스 -> 레코드 주소로 모은다(빈 칸 제외).
// comp 가 0 이거나 못 읽으면 빈 표다.
std::unordered_map<std::uint64_t, std::uintptr_t> bag_index(
    const mem::Reader& reader, std::uintptr_t comp);

// pieces 중 bag 에 있는 장비는 담금질·연마·소켓을 가방 레코드에서 다시 읽고 in_bag 을
// 세운다. 없는 장비는 그대로 둔다.
void apply_bag_truth(const mem::Reader& reader,
                     const std::unordered_map<std::uint64_t, std::uintptr_t>& bag,
                     std::vector<WornPiece>* pieces);

// 쓰기 결과를 판정한다. 가방에도 있는 장비는 가방 레코드가 진짜라 bag >= 1 이면 All, 아니면
// None 이다(장비 표 사본만 써 봐야 게임이 가방 값으로 되맞춘다). 입은 장비는 예전처럼 realm
// 수로 가른다 - 2 이상 All(클라·서버 모두), 1 Partial(한쪽만), 0 None.
enum class EqVerdict { All, Partial, None };
EqVerdict eq_verdict(const EqWriteResult& r);

}  // namespace cdtb::game

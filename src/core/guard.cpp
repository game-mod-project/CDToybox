#include "core/guard.h"

namespace cdtb::guard {

bool is_safe_to_modify() {
    // Crimson Desert는 2026-03-19 출시 시점부터 현재 빌드(2.00.01)까지
    // co-op / PvP / 매치메이킹이 전무한 순수 싱글플레이다. 0단계에서
    // 온라인 인프라로 의심한 PartyManager / GuildManager는 BlackSpace
    // 엔진이 BDO와 공유하는 스캐폴딩이고, LinkingCheckAsync는 계정
    // 연동이지 게임 세션이 아니다.
    //
    // 관문 자체는 남긴다. Pearl Abyss가 멀티플레이를 검토 중이라고
    // 밝혔으므로, 출시되면 이 함수 하나만 고쳐 모든 쓰기 기능을
    // 한곳에서 차단할 수 있어야 한다.
    return true;
}

}  // namespace cdtb::guard

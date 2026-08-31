#pragma once

namespace cdtb::guard {

// 게임 상태를 수정해도 안전한지 판정한다.
//
// 이 게임은 PartyManager / GuildManager / LinkingCheckAsync 등
// 온라인 인프라를 갖고 있다. 실제 세션 판정은 1단계에서 구현하며,
// 그때까지는 항상 false를 반환해 쓰기 기능이 만들어지는 것을 막는다.
bool is_safe_to_modify();

}  // namespace cdtb::guard

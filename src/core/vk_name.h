#pragma once

#include <cstddef>

namespace cdtb {

// 가상 키 코드의 사람 이름. 본창의 단축키 안내가 설정값에서 글자를 만들게
// 한다 - 예전엔 "End 비활성화" 가 코드에 박혀 있어 키를 F10 으로 옮긴 뒤에도
// 안내가 거짓이었다. 모르는 코드는 buf 에 "0x.." 로 적어 돌려준다.
const char* vk_name(int vk, char* buf, std::size_t n);

}  // namespace cdtb

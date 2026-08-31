#pragma once

namespace cdtb::proxy {

// C:\Windows\System32\XINPUT1_4.dll 을 절대경로로 로드한다.
// 상대경로를 쓰면 애플리케이션 디렉터리가 먼저 검색되어
// 자기 자신을 다시 로드하게 되므로 절대 사용하지 않는다.
bool load_original();
void unload_original();

}  // namespace cdtb::proxy

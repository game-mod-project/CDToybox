#pragma once

namespace cdtb::crashlog {

// 치명적 예외가 나면 죽은 위치(모듈 RVA)와 스택에 남은 복귀 주소
// 후보를 전용 파일에 남긴다. 예외를 처리하지는 않고 그대로
// 흘려보내므로(EXCEPTION_CONTINUE_SEARCH) 게임의 원래 동작을 바꾸지
// 않는다.
//
// 왜 CDToybox.log 가 아니라 별도 파일인가: 힙이 깨진 채로 죽는
// 경우 std::format / ofstream 이 CRT 힙 락에서 멈춘다. 그러면
// 게임이 죽는 대신 굳어버려 사용자가 강제 종료해야 한다. 그래서
// 여기서는 CRT 를 한 줄도 쓰지 않고 CreateFileW/WriteFile 로만
// 쓴다.
void install(const wchar_t* crash_path);

}  // namespace cdtb::crashlog

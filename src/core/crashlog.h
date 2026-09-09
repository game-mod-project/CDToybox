#pragma once

namespace cdtb::crashlog {

// 게임이 죽거나 굳으면 그 자리를 bin64/CDToybox.crash.txt 에 남긴다.
//
// 세 갈래로 잡는다.
//  1) 벡터 예외 처리기 - 게임 모듈 안에서 난 치명적 예외.
//     밖에서 난 것은 무시한다. 우리 스캐너가 매핑 안 된 페이지를
//     훑다 자체 __try 로 삼키는 양성 예외가 그쪽에서 쏟아진다.
//  2) 처리되지 않은 예외 필터 - 위치를 가리지 않고 마지막 한 번.
//  3) 감시 스레드 - watch_begin 으로 감싼 호출이 정해진 시간 안에
//     돌아오지 않으면 **모든 스레드의 스택**을 뜬다. 예외 없이
//     멈추는 교착은 1·2 로는 아무것도 안 남는다.
//
// 왜 CRT 를 안 쓰는가: 힙이 깨졌거나 다른 스레드를 세운 상태에서
// std::format / ofstream 을 부르면 힙·로더 락에서 멈춘다. 그러면
// 게임이 죽는 대신 굳어 강제 종료해야 한다. CreateFileW/WriteFile 만
// 쓰고, 스택을 다 뜬 뒤에 스레드를 되살리고 나서 파일을 쓴다.
void install(const wchar_t* crash_path);

// 돌아오지 않을 수 있는 게임 함수를 감싼다. 중첩은 지원하지 않는다
// (가장 바깥 것만 본다). what 은 15자까지 남는다.
void watch_begin(const char* what, unsigned long long id);
void watch_end();

}  // namespace cdtb::crashlog

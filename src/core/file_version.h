#pragma once

#include <string>

namespace cdtb {

// 파일의 버전 자원(VS_FIXEDFILEINFO)을 "a.b.c.d" 로. 없거나 못 읽으면 false 이고
// out 은 손대지 않는다. 본창의 게임 버전 줄이 exe 에서 읽게 한다 - 예전엔
// "2.00.01" 이 문자열 리터럴이라 패치가 오면 화면에서 가장 권위 있어 보이는
// 줄이 가장 먼저 거짓이 됐다.
bool file_version_string(const std::wstring& path, std::string* out);

}  // namespace cdtb

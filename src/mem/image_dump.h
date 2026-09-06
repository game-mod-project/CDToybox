#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cdtb::mem {

// 실행 중인 프로세스에서 뜬 모듈 이미지를 디스어셈블러가 그대로
// 읽는 PE 파일로 바꾼다.
//
// 왜 필요한가. 디스크의 실행 파일은 Denuvo 로 싸여 있어 코드가
// 보이지 않는다. 풀린 코드는 프로세스 메모리에만 있다. 그런데
// 메모리에 매핑된 이미지는 파일과 배치가 다르다 - 파일은 섹션마다
// FileAlignment 로 촘촘히 붙어 있고, 메모리는 SectionAlignment
// 간격으로 벌어져 있다. 뜬 바이트를 그대로 저장하면 PE 로더가
// PointerToRawData 를 따라 엉뚱한 곳을 읽는다.
//
// 그래서 섹션 헤더를 "파일 오프셋 = RVA" 가 되도록 고친다. 그러면
// 덤프 파일 자체가 곧 메모리 배치이므로 Ghidra·IDA 가 별도 설정
// 없이 연다. 도구들이 흔히 unmap 이라 부르는 것과 같다.
//
//   FileAlignment    <- SectionAlignment
//   ImageBase        <- 실제로 올라가 있던 주소
//   섹션마다
//     PointerToRawData <- VirtualAddress
//     SizeOfRawData    <- VirtualSize (덤프 크기로 자른다)
//
// 제자리에서 헤더만 고친다. 본문 바이트는 건드리지 않는다.
struct DumpSection {
    std::string name;
    std::uint32_t rva = 0;
    std::uint32_t virtual_size = 0;
    std::uint32_t raw_size = 0;      // 고친 뒤의 값
    std::uint32_t characteristics = 0;
    bool executable = false;
    bool truncated = false;          // 덤프 끝에 걸려 잘렸다
};

struct DumpFixup {
    std::uint64_t image_base = 0;        // 새로 박은 ImageBase
    std::uint32_t section_alignment = 0;
    std::uint32_t size_of_image = 0;
    std::uint32_t entry_rva = 0;
    std::vector<DumpSection> sections;
};

// 실패하면 false 를 돌리고 이유를 err 에 담는다. buf 는 모듈
// 베이스부터 size 바이트까지를 그대로 뜬 것이어야 한다.
bool make_dump_loadable(std::uint8_t* buf, std::size_t size,
                        std::uint64_t actual_base, DumpFixup* out,
                        std::string* err);

}  // namespace cdtb::mem

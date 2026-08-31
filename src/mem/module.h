#pragma once

#include "mem/scanner.h"

namespace cdtb::mem {

struct ModuleInfo {
    const std::uint8_t* base = nullptr;
    std::size_t size = 0;
};

// name이 nullptr이면 주 실행 모듈. 로드돼 있지 않으면 nullopt.
std::optional<ModuleInfo> find_module(const wchar_t* name);

// IMAGE_SCN_MEM_EXECUTE가 설정된 섹션의 범위만 반환한다.
// 이 게임은 Denuvo가 섹션을 재배치해 실행 코드가 .text가 아니라
// .text1에 있으므로, 섹션 이름으로 판정해서는 안 된다.
std::vector<Range> executable_ranges(ModuleInfo mod);

}  // namespace cdtb::mem

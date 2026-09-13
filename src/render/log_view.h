#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/log.h"
#include "render/log_filter.h"

namespace cdtb::render {

// 로그 창이 들고 있는 상태. **ImGui 를 안 쓴다** - 색인을 옮기는 산술이 여기 전부
// 모여 있고, 그게 이 창에서 제일 조용히 깨지는 부분이다(5000줄을 넘겨야 나타나서
// 손으로 보기도 어렵다). 그리는 코드에서 떼어 두어야 시험이 덮는다.
struct LogView {
    std::vector<log::RingLine> lines;   // 받은 줄(앞에서부터 잘라 낸다)
    std::vector<int> view;              // lines 의 색인 중 거르개를 통과한 것
    std::uint64_t seen = 0;             // 마지막으로 받은 일련번호
    std::uint64_t missed = 0;           // 덮여 사라진 줄 수(누적)
    bool primed = false;                // 한 번이라도 채운 적이 있는가
};

// 창이 들고 있을 줄 수의 상한. 고리(2000)보다 넉넉히 잡아, 창을 열어 둔 동안에는
// 고리가 버린 뒤에도 한동안 볼 수 있게 한다.
inline constexpr std::size_t kLogViewMax = 5000;
// 상한에 닿을 때마다 한 줄씩 지우면 매번 색인을 다시 만들게 된다. 뭉텅이로 자른다.
inline constexpr std::size_t kLogTrimChunk = 1000;

// 고리에서 받아 온 새 줄을 반영한다. before 는 받기 **전**의 lines 크기,
// missed 는 ring_since 가 돌려준 놓친 줄 수다.
//
// **처음 채우는 것이면 missed 를 세지 않는다.** 창을 열기 전의 줄은 사용자가 놓친
// 것이 아니다 - 보여 주기로 약속한 적이 없다. 그걸 세면 10분 플레이 뒤 창을 처음
// 열었을 때 "놓친 줄 13000" 이 경고색으로 뜬다.
void log_view_absorb(LogView& v, std::size_t before, std::uint64_t missed,
                     const LogFilter& f);

// 거르개가 바뀌었을 때 색인을 통째로 다시 만든다.
void log_view_rebuild(LogView& v, const LogFilter& f);

// 앞쪽을 잘라 낸다. 색인이 통째로 밀리므로 살아남은 것만 당겨 온다 - 여기서
// 안 맞추면 스크롤이 엉뚱한 줄을 그린다.
void log_view_trim(LogView& v);

// 화면만 비운다(로그 파일도, 고리도 건드리지 않는다). seen 은 그대로 두어 이미
// 본 줄이 다시 들어오지 않게 한다.
void log_view_clear(LogView& v);

}  // namespace cdtb::render

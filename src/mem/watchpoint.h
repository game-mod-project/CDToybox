#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cdtb::mem {

// 하드웨어 브레이크포인트로 "누가 이 주소에 쓰는가"를 알아낸다.
//
// 디스어셈블러 없이 답을 얻는 방법이다. CPU 디버그 레지스터(DR0~DR3)에
// 감시 주소를 넣으면 그 주소에 쓰는 순간 단일 스텝 예외가 발생하고,
// 예외 컨텍스트의 RIP가 곧 쓴 명령의 다음 주소다.
//
// 게임이 매 프레임 덮어쓰는 값을 우리 것으로 유지하려면 그 코드를
// 후킹해야 하는데, 이것이 그 코드를 찾는 가장 곧은 길이다.
//
// 주의: 디버그 레지스터는 스레드마다 따로 있다. 설치 시점에 존재하는
// 스레드에만 걸리고, 이후 생성된 스레드는 잡지 못한다.
class WriteWatch {
public:
    ~WriteWatch();

    // addr 에 대한 쓰기를 감시한다. size 는 1/2/4/8 만 가능하고,
    // 주소는 size 에 정렬돼 있어야 한다.
    bool install(std::uintptr_t addr, int size);
    void remove();
    bool active() const { return active_; }

    // 잡힌 명령 주소들. 같은 RIP는 한 번만 담는다.
    std::vector<std::uintptr_t> hits() const;
    std::size_t hit_count() const;

private:
    bool active_ = false;
};

}  // namespace cdtb::mem

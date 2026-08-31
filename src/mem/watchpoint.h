#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cdtb::mem {

// 하드웨어 브레이크포인트로 "누가 이 주소에 쓰는가"를 알아낸다.
//
// 디스어셈블러 없이 답을 얻는 방법이다. CPU 디버그 레지스터에 감시
// 주소를 넣으면 그 주소에 쓰는 순간 단일 스텝 예외가 발생하고, 예외
// 컨텍스트의 RIP가 쓴 명령의 다음 주소다.
//
// 게임이 매 프레임 덮어쓰는 값을 우리 것으로 유지하려면 그 코드를
// 후킹해야 하는데, 이것이 그 코드를 찾는 가장 곧은 길이다.
//
// 한 번에 최대 4개를 건다(DR0~DR3). 게임 한 번 실행으로 위치·회전·
// FOV·대조군을 같은 창에서 잡기 위해서다. 창을 나눠 걸면 창마다 게임
// 상태가 달라져 "히트 0"이 무엇 때문인지 구분되지 않는다.
//
// 디버그 레지스터는 스레드마다 따로 있다. 설치 시점의 스레드에만
// 걸리므로, 감시 중에는 refresh_threads() 를 주기적으로 불러 새로
// 생긴 스레드에도 걸어야 한다. 이것을 빠뜨리면 나중에 만들어진
// 스레드가 쓰는 값은 영원히 히트 0으로 나온다.
//
// 예외 핸들러는 프로세스 전역이므로 한 번에 하나만 설치할 수 있다.
// 다만 감시 대상 목록은 인스턴스가 들고 있다 - 전역에 두면 설치하지
// 않은 객체의 등록이 다음 객체로 새어 나간다.
class WriteWatch {
public:
    static constexpr int kMaxSlots = 4;

    WriteWatch() = default;
    ~WriteWatch();
    WriteWatch(const WriteWatch&) = delete;
    WriteWatch& operator=(const WriteWatch&) = delete;

    // 감시 대상을 등록한다. install() 전에만 유효하다.
    // size 는 1/2/4/8 이고 주소는 size 에 정렬돼 있어야 한다.
    bool add(std::uintptr_t addr, int size, std::string label);
    int registered() const { return static_cast<int>(cfg_.size()); }

    bool install();
    void remove();
    bool active() const { return active_; }

    // 새로 생긴 스레드에 디버그 레지스터를 건다. 새로 건 스레드 수를
    // 돌려준다. 감시 중 주기적으로 부른다.
    int refresh_threads();

    struct Hit {
        std::uintptr_t rip = 0;
        std::size_t count = 0;
    };
    struct Slot {
        std::string label;
        std::uintptr_t addr = 0;
        int size = 0;
        std::size_t total = 0;
        std::vector<Hit> rips;   // 횟수 내림차순
    };

    // 감시 중에는 현재까지의 집계를, 해제 뒤에는 해제 시점의 집계를
    // 돌려준다. 해제한 뒤에도 결과를 읽을 수 있어야 한다.
    std::vector<Slot> results() const;

private:
    struct Cfg {
        std::uintptr_t addr = 0;
        int size = 0;
        std::string label;
    };

    std::vector<Cfg> cfg_;
    std::vector<Slot> snapshot_;
    bool active_ = false;
};

}  // namespace cdtb::mem

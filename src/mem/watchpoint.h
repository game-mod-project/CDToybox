#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cdtb::mem {

// 하드웨어 브레이크포인트로 "누가 이 주소에 쓰는가"를 알아낸다.
//
// CPU 디버그 레지스터에 감시 주소를 넣으면 그 주소에 쓰는 순간
// 단일 스텝 예외가 발생하고, 예외 컨텍스트의 RIP 가 쓴 명령의 다음
// 주소다. 디스어셈블러 없이 답을 얻는 방법이다.
//
// 한 번에 최대 4개를 건다(DR0~DR3). 게임 한 번 실행으로 여러 값을
// 같은 창에서 잡기 위해서다. 창을 나눠 걸면 창마다 게임 상태가
// 달라져 "히트 0"이 무엇 때문인지 구분되지 않는다.
//
// == 이 도구는 게임을 죽일 수 있다. 아래 세 가지가 그 이유다. ==
//
// 1. 디버그 레지스터는 스레드마다 따로 있고, OpenThread 는 실패할
//    수 있다. 실패를 조용히 넘기면 그 스레드는 예전 감시 주소를
//    그대로 들고 남는다.
// 2. 그 상태로 예외 핸들러를 내리면, 낙오 스레드가 다음에 그 주소를
//    쓰는 순간 처리되지 않은 단일 스텝 예외로 프로세스가 죽는다.
//    2026-08-31 실측에서 실제로 이렇게 죽었다(예외 0x80000004,
//    주소 0x140A59516 - 대조군이 1262회 잡았던 바로 그 명령).
// 3. 낙오 스레드의 히트는 다음 감시의 슬롯 라벨에 잘못 붙는다.
//    같은 실측에서 감시 2의 "705회"가 사실은 감시 1의 값이었다.
//
// 그래서 이 구현은 (a) 예외 핸들러를 DLL 수명 동안 유지하고,
// (b) 핸들러가 낙오 스레드를 만나면 그 자리에서 스스로 고치며,
// (c) 스레드 프로그래밍 실패를 세어 로그에 남긴다.
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

    // 새로 생긴 스레드에 걸고, 설정이 어긋난 스레드를 고친다.
    // 감시 중 주기적으로 부른다.
    int refresh_threads();

    // DLL 을 내리기 전에 부른다. 모든 스레드의 디버그 레지스터를
    // 지우고 예외 핸들러를 뗀다. 순서가 반대면 게임이 죽는다.
    static void shutdown();

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
    // 돌려준다.
    std::vector<Slot> results() const;

    // 스레드 프로그래밍 통계. 히트 0을 해석하려면 이것이 필요하다.
    struct ThreadStats {
        int enumerated = 0;    // 프로세스에서 본 스레드 수
        int programmed = 0;    // 디버그 레지스터를 건 수
        int open_failed = 0;   // OpenThread 실패
        int set_failed = 0;    // SetThreadContext 실패 또는 검증 불일치
        int self_healed = 0;   // 핸들러가 현장에서 고친 횟수
        int rearmed = 0;       // 보호 코드가 지운 것을 다시 건 횟수
    };
    ThreadStats stats() const;

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

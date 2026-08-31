#include "harness.h"
#include "mem/watchpoint.h"

#include <windows.h>

#include <atomic>
#include <thread>

using namespace cdtb::mem;

namespace {

// 감시 대상. 별도 스레드가 여기에 쓴다.
alignas(8) volatile std::uint32_t g_target = 0;
std::atomic<bool> g_writer_stop{false};

void writer_thread() {
    while (!g_writer_stop.load()) {
        g_target = g_target + 1;
        ::Sleep(1);
    }
}

std::uintptr_t target_addr() {
    return reinterpret_cast<std::uintptr_t>(const_cast<std::uint32_t*>(&g_target));
}

}  // namespace

TEST(watchpoint_rejects_bad_size) {
    WriteWatch w;
    CHECK(!w.add(target_addr(), 3, "3바이트"));
    CHECK(!w.add(target_addr(), 0, "0바이트"));
    CHECK_EQ(w.registered(), 0);
}

TEST(watchpoint_rejects_unaligned) {
    WriteWatch w;
    CHECK(!w.add(target_addr() + 1, 4, "정렬 안 됨"));
    CHECK_EQ(w.registered(), 0);
}

TEST(watchpoint_rejects_null) {
    WriteWatch w;
    CHECK(!w.add(0, 4, "널"));
    CHECK_EQ(w.registered(), 0);
}

TEST(watchpoint_holds_four_slots_and_no_more) {
    WriteWatch w;
    alignas(8) std::uint32_t a[8]{};
    for (int i = 0; i < WriteWatch::kMaxSlots; ++i) {
        CHECK(w.add(reinterpret_cast<std::uintptr_t>(&a[i]), 4, "슬롯"));
    }
    CHECK_EQ(w.registered(), WriteWatch::kMaxSlots);
    // 다섯 번째는 거절해야 한다. CPU 디버그 레지스터가 4개뿐이다.
    CHECK(!w.add(reinterpret_cast<std::uintptr_t>(&a[4]), 4, "다섯째"));
}

TEST(watchpoint_install_without_slots_fails) {
    WriteWatch w;
    CHECK(!w.install());
    CHECK(!w.active());
}

// 하드웨어 브레이크포인트가 실제로 쓰기를 잡는지 확인한다.
// 이 테스트가 실패하면 인게임 추적 결과 전체가 무의미해진다.
TEST(watchpoint_catches_write_from_existing_thread) {
    g_writer_stop.store(false);
    g_target = 0;
    std::thread writer(writer_thread);
    ::Sleep(50);   // 스레드가 실제로 돌기 시작하도록

    WriteWatch w;
    CHECK(w.add(target_addr(), 4, "대상"));
    const bool installed = w.install();
    CHECK(installed);

    ::Sleep(300);

    const auto slots = w.results();
    w.remove();
    g_writer_stop.store(true);
    writer.join();

    CHECK_EQ(slots.size(), static_cast<std::size_t>(1));
    if (!slots.empty()) {
        CHECK(slots[0].total > 0);
        CHECK(!slots[0].rips.empty());
    }
}

// 회귀 테스트.
//
// 예전 구현은 설치 시점의 스레드에만 디버그 레지스터를 걸었다.
// 그래서 나중에 만들어진 스레드가 쓰는 값은 영원히 "히트 0"으로
// 나왔고, 그것을 "게임이 이 값을 안 쓴다"로 잘못 읽었다.
// refresh_threads() 가 그 구멍을 메운다.
TEST(watchpoint_catches_write_from_thread_created_after_install) {
    g_writer_stop.store(false);
    g_target = 0;

    WriteWatch w;
    CHECK(w.add(target_addr(), 4, "대상"));
    CHECK(w.install());

    // 설치가 끝난 뒤에 쓰는 스레드가 생긴다.
    std::thread writer(writer_thread);
    ::Sleep(50);

    const int added = w.refresh_threads();
    CHECK(added >= 1);   // 새 스레드를 최소 하나는 흡수해야 한다

    ::Sleep(300);

    const auto slots = w.results();
    w.remove();
    g_writer_stop.store(true);
    writer.join();

    CHECK_EQ(slots.size(), static_cast<std::size_t>(1));
    if (!slots.empty()) {
        CHECK(slots[0].total > 0);
    }
}

// 슬롯을 여러 개 걸었을 때 히트가 슬롯별로 갈리는지 본다.
// DR6 의 어느 비트가 섰는지로 구분하는데, 이걸 틀리면 모든 히트가
// 한 슬롯에 몰려 결과 해석이 통째로 어긋난다.
TEST(watchpoint_attributes_hits_to_the_right_slot) {
    alignas(8) volatile std::uint32_t a = 0;
    alignas(8) volatile std::uint32_t b = 0;
    std::atomic<bool> stop{false};

    std::thread writer([&] {
        while (!stop.load()) {
            a = a + 1;    // a 만 쓴다. b 는 건드리지 않는다.
            ::Sleep(1);
        }
    });
    ::Sleep(50);

    WriteWatch w;
    CHECK(w.add(reinterpret_cast<std::uintptr_t>(const_cast<std::uint32_t*>(&a)), 4, "쓰는 값"));
    CHECK(w.add(reinterpret_cast<std::uintptr_t>(const_cast<std::uint32_t*>(&b)), 4, "안 쓰는 값"));
    CHECK(w.install());

    ::Sleep(300);

    const auto slots = w.results();
    w.remove();
    stop.store(true);
    writer.join();

    CHECK_EQ(slots.size(), static_cast<std::size_t>(2));
    if (slots.size() == 2) {
        CHECK(slots[0].total > 0);     // a
        CHECK_EQ(slots[1].total, static_cast<std::size_t>(0));   // b
    }
}

// 스레드 통계는 히트 0을 해석하는 데 필수다. 못 건 스레드가 있으면
// "게임이 이 값을 안 쓴다"고 결론지을 수 없다.
TEST(watchpoint_reports_thread_stats) {
    g_writer_stop.store(false);
    std::thread writer(writer_thread);
    ::Sleep(50);

    WriteWatch w;
    CHECK(w.add(target_addr(), 4, "대상"));
    CHECK(w.install());
    const auto s = w.stats();
    w.remove();
    g_writer_stop.store(true);
    writer.join();

    CHECK(s.enumerated > 0);
    CHECK(s.programmed > 0);
    CHECK(s.programmed <= s.enumerated);
}

// 해제한 뒤에도 결과를 읽을 수 있어야 한다. 호출부가 remove() 를
// 먼저 하고 결과를 보는 순서로 쓸 수 있기 때문이다.
TEST(watchpoint_results_survive_remove) {
    g_writer_stop.store(false);
    std::thread writer(writer_thread);
    ::Sleep(50);

    WriteWatch w;
    CHECK(w.add(target_addr(), 4, "대상"));
    CHECK(w.install());
    ::Sleep(200);
    w.remove();

    g_writer_stop.store(true);
    writer.join();

    const auto after = w.results();
    CHECK_EQ(after.size(), static_cast<std::size_t>(1));
    if (!after.empty()) CHECK(after[0].total > 0);
}

// 두 감시를 잇달아 돌려도 서로 오염되지 않아야 한다.
//
// 실측에서 두 번째 감시의 히트가 첫 번째 감시의 값이었다. 디버그
// 레지스터를 못 지운 스레드가 남아 예전 주소를 계속 들고 있었고,
// 그 히트가 두 번째 감시의 슬롯 라벨에 붙었다.
TEST(watchpoint_second_window_is_not_contaminated) {
    alignas(8) volatile std::uint32_t written = 0;
    alignas(8) volatile std::uint32_t untouched = 0;
    std::atomic<bool> stop{false};

    std::thread writer([&] {
        while (!stop.load()) {
            written = written + 1;
            ::Sleep(1);
        }
    });
    ::Sleep(50);

    {
        WriteWatch first;
        CHECK(first.add(reinterpret_cast<std::uintptr_t>(
                            const_cast<std::uint32_t*>(&written)), 4, "1창"));
        CHECK(first.install());
        ::Sleep(200);
        first.remove();
    }

    // 두 번째 창은 아무도 안 쓰는 주소만 본다. 첫 창이 깨끗이
    // 정리됐다면 히트가 0이어야 한다.
    WriteWatch second;
    CHECK(second.add(reinterpret_cast<std::uintptr_t>(
                         const_cast<std::uint32_t*>(&untouched)), 4, "2창"));
    CHECK(second.install());
    ::Sleep(200);
    const auto r = second.results();
    second.remove();

    stop.store(true);
    writer.join();

    CHECK_EQ(r.size(), static_cast<std::size_t>(1));
    if (!r.empty()) CHECK_EQ(r[0].total, static_cast<std::size_t>(0));
}

// shutdown() 은 여러 번 불러도 안전해야 한다. DLL 종료 경로와
// 분석 종료 경로 양쪽에서 불릴 수 있다.
TEST(watchpoint_shutdown_is_idempotent) {
    WriteWatch::shutdown();
    WriteWatch::shutdown();

    // 종료 뒤에도 다시 쓸 수 있어야 한다.
    g_writer_stop.store(false);
    std::thread writer(writer_thread);
    ::Sleep(50);

    WriteWatch w;
    CHECK(w.add(target_addr(), 4, "재사용"));
    CHECK(w.install());
    ::Sleep(200);
    const auto r = w.results();
    w.remove();

    g_writer_stop.store(true);
    writer.join();

    CHECK_EQ(r.size(), static_cast<std::size_t>(1));
    if (!r.empty()) CHECK(r[0].total > 0);
    WriteWatch::shutdown();
}

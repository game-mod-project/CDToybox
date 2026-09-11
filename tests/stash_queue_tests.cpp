#include <cstddef>

#include "render/stash_queue.h"
#include "harness.h"

namespace {

using cdtb::render::QueueStep;
using cdtb::render::StashQueue;
using cdtb::render::stash_autosave_due;
using cdtb::render::stash_queue_clear;
using cdtb::render::stash_queue_remaining;
using cdtb::render::stash_queue_sent;
using cdtb::render::stash_queue_step;

StashQueue two_items() {
    StashQueue q;
    q.items = {cdtb::game::StashEntry{1, 1}, cdtb::game::StashEntry{2, 1}};
    return q;
}

}  // namespace

TEST(stash_queue_is_idle_when_empty) {
    StashQueue q;
    CHECK(stash_queue_step(&q, true, 0.0) == QueueStep::Idle);
    CHECK(stash_queue_step(nullptr, true, 0.0) == QueueStep::Idle);
    CHECK_EQ(stash_queue_remaining(q), 0u);
}

// 보낸 직후에는 간격만큼 기다리고, 간격이 차면 다음 것을 보낸다.
TEST(stash_queue_sends_then_waits_for_the_interval) {
    StashQueue q = two_items();
    CHECK(stash_queue_step(&q, true, 10.0) == QueueStep::Send);
    stash_queue_sent(&q, 10.0, 2.0);
    CHECK_EQ(q.at, 1u);
    CHECK(stash_queue_step(&q, true, 11.9) == QueueStep::Wait);
    CHECK(stash_queue_step(&q, true, 12.0) == QueueStep::Send);
}

// 세션이 없으면 큐를 버리지 않는다 - 세션이 돌아오면 그 자리에서 이어진다.
TEST(stash_queue_keeps_items_without_a_session) {
    StashQueue q = two_items();
    CHECK(stash_queue_step(&q, false, 10.0) == QueueStep::NoSession);
    CHECK_EQ(stash_queue_remaining(q), 2u);
    CHECK(stash_queue_step(&q, true, 10.0) == QueueStep::Send);
}

// 마지막 것까지 보낸 뒤 첫 호출이 Done 이고 그때 큐가 비워진다. 그 다음은 Idle.
TEST(stash_queue_reports_done_once_then_idle) {
    StashQueue q = two_items();
    stash_queue_sent(&q, 0.0);
    stash_queue_sent(&q, 2.0);
    CHECK(stash_queue_step(&q, true, 4.0) == QueueStep::Done);
    CHECK(q.items.empty());
    CHECK(stash_queue_step(&q, true, 4.0) == QueueStep::Idle);
}

TEST(stash_queue_clear_resets_everything) {
    StashQueue q = two_items();
    q.no_session_noted = true;
    stash_queue_sent(&q, 5.0);
    stash_queue_clear(&q);
    CHECK(q.items.empty());
    CHECK_EQ(q.at, 0u);
    CHECK(!q.no_session_noted);
    CHECK_EQ(q.next_at, 0.0);
}

TEST(stash_queue_remaining_counts_the_unsent) {
    StashQueue q = two_items();
    CHECK_EQ(stash_queue_remaining(q), 2u);
    stash_queue_sent(&q, 0.0);
    CHECK_EQ(stash_queue_remaining(q), 1u);
    stash_queue_sent(&q, 2.0);
    CHECK_EQ(stash_queue_remaining(q), 0u);
    stash_queue_sent(&q, 4.0);   // 끝을 넘겨 불러도 at 은 더 안 오른다
    CHECK_EQ(q.at, 2u);
}

TEST(stash_autosave_is_due_one_second_after_the_change) {
    CHECK(!stash_autosave_due(-1.0, 100.0));   // 변경 없음
    CHECK(!stash_autosave_due(10.0, 10.5));
    CHECK(stash_autosave_due(10.0, 11.0));
    CHECK(!stash_autosave_due(10.0, 10.9, 1.0));
    CHECK(stash_autosave_due(10.0, 15.0, 5.0));
}

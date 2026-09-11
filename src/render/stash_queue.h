#pragma once

#include <cstddef>
#include <vector>

#include "game/stash.h"

namespace cdtb::render {

// 보관함 일괄 지급 큐. ImGui 없음 - 창이 닫혀도 overlay 가 매 프레임
// stash_tick() 으로 돌린다. 지급 요청(request_give)은 레인 쿨다운(2초)이 있어
// 한 번에 다 못 보낸다 - 한 개씩, 간격을 두고 흘려보낸다. 보내는 일 자체는
// 이 파일 밖(stash_panel)이 한다 - 여기는 "이번 프레임에 무엇을 할지" 만 정한다.
struct StashQueue {
    std::vector<game::StashEntry> items;
    std::size_t at = 0;              // 다음에 보낼 항목
    double next_at = 0.0;            // 이 시각 전에는 보내지 않는다
    bool no_session_noted = false;   // 세션 없음 경고를 한 번만 내려고
};

enum class QueueStep {
    Idle,        // 큐가 비어 있다
    Wait,        // 간격을 기다린다
    Send,        // items[at] 을 지금 보내라 - 보냈으면 stash_queue_sent()
    Done,        // 마지막까지 보냈다 - 한 번만 나고, 그때 큐가 비워진다
    NoSession,   // 세션이 없다 - 큐는 남긴다
};

QueueStep stash_queue_step(StashQueue* q, bool have_session, double now);
// items[at] 을 보냈다. at 을 올리고 다음 시각을 now + interval 로 잡는다.
void stash_queue_sent(StashQueue* q, double now, double interval = 2.0);
// 큐를 비운다(중단·완료).
void stash_queue_clear(StashQueue* q);
// 아직 보내지 않은 항목 수.
std::size_t stash_queue_remaining(const StashQueue& q);

// 자동 저장 판정. dirty_at 은 마지막 변경 시각(음수면 변경 없음). delay 초가
// 지났으면 true.
bool stash_autosave_due(double dirty_at, double now, double delay = 1.0);

}  // namespace cdtb::render

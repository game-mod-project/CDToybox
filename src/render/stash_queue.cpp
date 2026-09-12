#include "render/stash_queue.h"

namespace cdtb::render {

QueueStep stash_queue_step(StashQueue* q, bool have_session, double now) {
    if (q == nullptr || q->items.empty()) return QueueStep::Idle;
    if (q->at >= q->items.size()) {
        stash_queue_clear(q);
        return QueueStep::Done;
    }
    if (!have_session) return QueueStep::NoSession;
    if (now < q->next_at) return QueueStep::Wait;
    return QueueStep::Send;
}

void stash_queue_sent(StashQueue* q, double now, double interval) {
    if (q == nullptr) return;
    if (q->at < q->items.size()) ++q->at;
    q->next_at = now + interval;
}

void stash_queue_clear(StashQueue* q) {
    if (q == nullptr) return;
    q->items.clear();
    q->at = 0;
    q->next_at = 0.0;
    q->no_session_noted = false;
}

std::size_t stash_queue_remaining(const StashQueue& q) {
    return q.at < q.items.size() ? q.items.size() - q.at : 0;
}

bool stash_autosave_due(double dirty_at, double now, double delay) {
    return dirty_at >= 0.0 && now - dirty_at >= delay;
}

}  // namespace cdtb::render

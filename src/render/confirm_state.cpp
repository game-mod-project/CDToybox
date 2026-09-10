#include "render/confirm_state.h"

namespace cdtb::render {
namespace {

bool armed_now(const ConfirmState& s, unsigned id, double now, double window) {
    return s.id == id && s.id != 0 && s.armed_at >= 0.0 &&
           now - s.armed_at < window;
}

void disarm(ConfirmState* s) {
    s->id = 0;
    s->armed_at = -1.0;
}

}  // namespace

ConfirmStep confirm_step(ConfirmState* s, unsigned id, bool clicked, double now,
                         double window) {
    const bool armed = armed_now(*s, id, now, window);
    // 이 버튼이었는데 시간이 지났으면 지금 푼다
    if (s->id == id && s->id != 0 && !armed) disarm(s);
    if (!clicked) return armed ? ConfirmStep::Armed : ConfirmStep::Idle;
    if (armed) {
        disarm(s);
        return ConfirmStep::Fired;
    }
    s->id = id;
    s->armed_at = now;
    return ConfirmStep::Armed;
}

double confirm_left(const ConfirmState& s, unsigned id, double now,
                    double window) {
    if (!armed_now(s, id, now, window)) return 0.0;
    return window - (now - s.armed_at);
}

}  // namespace cdtb::render

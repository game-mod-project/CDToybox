#include "render/notice_state.h"

#include <cstring>

namespace cdtb::render {

NoticeAge notice_age(const Notice& n, double now, double fade_after,
                     double hide_after) {
    if (n.at < 0.0) return NoticeAge::Gone;
    const double age = now - n.at;
    if (age < 0.0) return NoticeAge::Fresh;
    if (age >= hide_after) return NoticeAge::Gone;
    if (age >= fade_after) return NoticeAge::Faded;
    return NoticeAge::Fresh;
}

void notice_clear(Notice* n) {
    n->level = NoticeLevel::Info;
    n->text[0] = '\0';
    n->at = -1.0;
}

void notice_put(Notice* n, NoticeLevel lv, double now, const char* text) {
    n->level = lv;
    n->at = now;
    std::strncpy(n->text, text != nullptr ? text : "", sizeof(n->text) - 1);
    n->text[sizeof(n->text) - 1] = '\0';
}

}  // namespace cdtb::render

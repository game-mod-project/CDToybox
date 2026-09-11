#include "render/notice_state.h"

#include <cstring>

namespace cdtb::render {
namespace {

// 끝이 잘린 UTF-8 글자를 떼어 낸다. 한글은 3바이트라 199바이트 자리에서 셋 중
// 하나만 들어오면 물음표가 찍힌다.
void trim_partial_utf8(char* s) {
    const std::size_t len = std::strlen(s);
    if (len == 0) return;
    std::size_t i = len;
    // 이어지는 바이트(10xxxxxx)를 건너 마지막 글자의 첫 바이트로
    while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80) --i;
    if (i == 0) {
        s[0] = '\0';
        return;
    }
    const unsigned char lead = static_cast<unsigned char>(s[i - 1]);
    std::size_t need = 1;
    if (lead >= 0xF0) need = 4;
    else if (lead >= 0xE0) need = 3;
    else if (lead >= 0xC0) need = 2;
    if (len - (i - 1) < need) s[i - 1] = '\0';
}

}  // namespace

NoticeAge notice_age(const Notice& n, double now, double fade_after,
                     double hide_after) {
    if (n.at < 0.0) return NoticeAge::Gone;
    const double age = now - n.at;
    if (age < 0.0) return NoticeAge::Gone;   // 시계가 뒤로 갔다 - 옛 시계의 결과
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
    trim_partial_utf8(n->text);
}

}  // namespace cdtb::render

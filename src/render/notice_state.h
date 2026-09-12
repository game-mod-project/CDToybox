#pragma once

// 창의 결과 줄 상태. ImGui 없이 쓸 수 있어 테스트한다.
//
// 결과 문자열이 창마다 전역 char 배열(g_msg 등)로 흩어져 있었고, 한 번 찍히면
// 세션 끝까지 남아 20분 전 결과가 방금 것처럼 보였다. 등급과 시각을 같이
// 담아 색과 만료를 한 곳에서 정한다.
namespace cdtb::render {

enum class NoticeLevel { Ok, Warn, Bad, Info };

struct Notice {
    NoticeLevel level = NoticeLevel::Info;
    char text[200] = "";
    double at = -1.0;   // 찍힌 시각(초). 음수면 비어 있다
};

enum class NoticeAge { Fresh, Faded, Gone };

// now 기준 나이. fade_after 초가 지나면 Faded(회색), hide_after 초가 지나면 Gone.
// 비어 있으면 Gone. now < at(시계 리셋)면 Gone.
NoticeAge notice_age(const Notice& n, double now, double fade_after = 10.0,
                     double hide_after = 60.0);

void notice_clear(Notice* n);

// 등급·시각을 찍고 본문을 담는다. 본문이 길면 글자(UTF-8) 경계에서 끝을 자른다.
void notice_put(Notice* n, NoticeLevel lv, double now, const char* text);

}  // namespace cdtb::render

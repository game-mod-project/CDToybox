#pragma once

#include <map>
#include <string>
#include <string_view>

namespace cdtb::render {

// 오버레이 UI 상태(창 열림·헤더 펼침)를 키로 들고 있다가 ini 로 오간다.
// ImGui 를 모른다 - 시험에서 그대로 쓴다.
class UiPersist {
public:
    // 헤더 펼침. 모르는 키는 접힘(false)이다.
    bool header_open(std::string_view key) const;
    void set_header_open(std::string_view key, bool open);

    // 창 열림. 모르는 키는 부르는 쪽의 기본값을 그대로 돌려준다.
    bool window_open(std::string_view key, bool def) const;
    void set_window_open(std::string_view key, bool open);

    // ini 로 오가는 형식. 한 줄이 한 항목이다 - `H=<키>=0|1`(헤더) ·
    // `W=<키>=0|1`(창). 키에 `=` 가 있을 수 있어(보관함 세트 이름) 값은
    // **마지막** `=` 뒤로 가른다.
    std::string serialize() const;
    void parse_line(std::string_view line);

private:
    // std::less<> 라야 string_view 로 찾을 때 임시 string 을 안 만든다.
    std::map<std::string, bool, std::less<>> headers_;
    std::map<std::string, bool, std::less<>> windows_;
};

}  // namespace cdtb::render

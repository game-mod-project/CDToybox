#include <windows.h>

#include <cctype>
#include <cstring>
#include <string>

#include "core/file_version.h"
#include "core/findlimit.h"
#include "core/vk_name.h"
#include "harness.h"

namespace {

bool is_dotted_quad(const std::string& s) {
    int dots = 0;
    int digits = 0;   // 이번 구간의 숫자 개수. 안 세면 "..." 도 통과한다
    if (s.empty()) return false;
    for (const char c : s) {
        if (c == '.') {
            if (digits == 0) return false;
            ++dots;
            digits = 0;
        } else if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        } else {
            ++digits;
        }
    }
    return dots == 3 && digits > 0;
}

}  // namespace

TEST(vk_name_known_keys) {
    char buf[16];
    CHECK(std::strcmp(cdtb::vk_name(0x2D, buf, sizeof(buf)), "Insert") == 0);
    CHECK(std::strcmp(cdtb::vk_name(0x79, buf, sizeof(buf)), "F10") == 0);
    CHECK(std::strcmp(cdtb::vk_name(0x23, buf, sizeof(buf)), "End") == 0);
    CHECK(std::strcmp(cdtb::vk_name(0x70, buf, sizeof(buf)), "F1") == 0);
    CHECK(std::strcmp(cdtb::vk_name(0x87, buf, sizeof(buf)), "F24") == 0);
    CHECK(std::strcmp(cdtb::vk_name(0x41, buf, sizeof(buf)), "A") == 0);
    CHECK(std::strcmp(cdtb::vk_name(0x35, buf, sizeof(buf)), "5") == 0);
    CHECK(std::strcmp(cdtb::vk_name(0x60, buf, sizeof(buf)), "Num0") == 0);
}

TEST(vk_name_unknown_is_hex) {
    char buf[16];
    CHECK(std::strcmp(cdtb::vk_name(0xE7, buf, sizeof(buf)), "0xE7") == 0);
    CHECK(std::strcmp(cdtb::vk_name(0x100, buf, sizeof(buf)), "0x100") == 0);
}

TEST(file_version_reads_kernel32) {
    wchar_t sys[MAX_PATH]{};
    ::GetSystemDirectoryW(sys, MAX_PATH);
    std::string v;
    CHECK(cdtb::file_version_string(std::wstring(sys) + L"\\kernel32.dll", &v));
    CHECK(is_dotted_quad(v));
}

TEST(file_version_missing_file_is_false) {
    std::string v = "untouched";
    CHECK(!cdtb::file_version_string(L"Z:\\no\\such\\file.exe", &v));
    CHECK_EQ(v, std::string("untouched"));
}

// ---------------------------------------- 못 찾는 탐색의 끝 (core/findlimit.h)
//
// 규칙은 `TROUBLESHOOTING.md` 2.10.1 이다 - **간격만 두는 것으로는 부족하다.**
// 간격 < 한 번 비용이면 쉬는 것이 아니라 쉬지 않고 도는 것이다(수배 컴포넌트는
// 15초 간격에 한 번 45초짜리라 로그의 3분의 1이 그 경고였다).
//
// 다만 끝은 **연속 실패**에만 걸어야 한다. 명부 컴포넌트는 동반자가 늘 때마다
// 게임이 새로 만들므로 성공한 재탐색까지 세면 **정당한 재탐색이 막힌다** -
// 그러면 종 바꾸기·획득 뒤처리가 조용히 죽는다.

TEST(find_limit_stops_after_consecutive_failures) {
    cdtb::FindLimit fl{3};
    CHECK(fl.may_try());
    CHECK(!fl.note_failure());   // 1/3
    CHECK(fl.may_try());
    CHECK(!fl.note_failure());   // 2/3
    CHECK(fl.may_try());
    CHECK(fl.note_failure());    // 3/3 - 이번이 마지막이라고 알려 준다
    CHECK(!fl.may_try());
    CHECK(fl.gave_up());
}

TEST(find_limit_forgets_failures_after_a_success) {
    cdtb::FindLimit fl{3};
    fl.note_failure();
    fl.note_failure();
    fl.note_success();
    CHECK(!fl.gave_up());
    CHECK(fl.may_try());
    // 성공 뒤에는 상한을 처음부터 다시 쓴다
    CHECK(!fl.note_failure());
    CHECK(!fl.note_failure());
    CHECK(fl.note_failure());
}

TEST(find_limit_rearm_reopens_the_search) {
    cdtb::FindLimit fl{1};
    CHECK(fl.note_failure());
    CHECK(fl.gave_up());
    fl.rearm();
    CHECK(!fl.gave_up());
    CHECK(fl.may_try());
}

TEST(find_limit_always_tries_at_least_once) {
    // 상한 0 은 "끝이 없다" 가 아니라 **설정 실수**다. 끝을 없애는 길을 두면
    // 이 규칙이 다시 새어 나간다 - 적어도 한 번은 해 보고 그만둔다.
    cdtb::FindLimit fl{0};
    CHECK(fl.may_try());
    CHECK(fl.note_failure());
    CHECK(fl.gave_up());
}

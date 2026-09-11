#pragma once

#include <algorithm>
#include <string>
#include <vector>

// 표 머리글 정렬을 뷰 벡터에 건다. ImGui 를 모르므로 테스트한다.
//
// 아이템 목록·인벤토리만 정렬이 되고 나머지 표 9개는 안 됐다(사용자 요청
// 2026-09-11). 표마다 비교 함수 하나만 쓰면 되게 열 번호를 그대로 넘긴다.
namespace cdtb::render {

struct SortSpec {
    int column = -1;        // 음수면 정렬 없음(원래 순서)
    bool ascending = true;
    bool operator==(const SortSpec&) const = default;
};

inline int cmp3(long long a, long long b) { return a < b ? -1 : (a > b ? 1 : 0); }
inline int cmp3(const std::string& a, const std::string& b) {
    const int c = a.compare(b);
    return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

// cmp(a, b, column) 이 <0 / 0 / >0 을 낸다. 안정 정렬이라 같은 값은 원래 순서.
template <class T, class Cmp>
void sort_view(std::vector<T>& v, const SortSpec& s, Cmp cmp) {
    if (s.column < 0) return;
    std::stable_sort(v.begin(), v.end(), [&](const T& a, const T& b) {
        const int c = cmp(a, b, s.column);
        return s.ascending ? c < 0 : c > 0;
    });
}

}  // namespace cdtb::render

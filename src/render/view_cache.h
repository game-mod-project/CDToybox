#pragma once

#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

#include "render/table_sort.h"

// 걸러서 정렬한 뷰를 키가 바뀔 때만 다시 만든다. 로스터 캐릭터 탭은 7,250행이라
// 매 프레임 거르고 정렬하면 렌더 스레드에 그대로 얹힌다. ImGui 없음 - 테스트한다.
namespace cdtb::render {

struct ViewKey {
    int tab = 0;                        // 창 안의 탭(같은 캐시를 여러 탭이 쓸 때)
    std::string query;                  // 검색어
    int type = -1;                      // 타입 필터(-1 전체)
    unsigned flags = 0;                 // 체크박스 비트 (flag_bits)
    SortSpec sort;                      // 머리글 정렬
    const void* generation = nullptr;   // 원본의 세대(갱신 카운터) 또는 data()
    std::size_t count = 0;              // 원본 크기
    // 원본이 마지막으로 갱신된 시각. 같은 버퍼에 같은 크기로 다시 채워지는
    // 목록(근처·내 동반자, 2초 갱신)은 data()/size() 로 못 가린다.
    double stamp = 0.0;
    bool operator==(const ViewKey&) const = default;
};

// 체크박스 여럿을 비트로 접는다. 순서대로 1, 2, 4, ...
inline unsigned flag_bits(std::initializer_list<bool> fs) {
    unsigned b = 0;
    unsigned i = 0;
    for (bool f : fs) {
        if (f) b |= 1u << i;
        ++i;
    }
    return b;
}

template <class T>
struct CachedView {
    ViewKey key;
    std::vector<const T*> rows;
    std::size_t total = 0;   // 창이 함께 세는 값(거르기 전 개수 등)
    bool valid = false;

    // 키가 바뀌었으면 rows/total 을 비우고 true - 부르는 쪽이 다시 채운다.
    bool begin(const ViewKey& k) {
        if (valid && key == k) return false;
        key = k;
        valid = true;
        rows.clear();
        total = 0;
        return true;
    }
};

}  // namespace cdtb::render

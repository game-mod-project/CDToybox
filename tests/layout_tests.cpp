#include <set>
#include <string>

#include "harness.h"
#include "render/layout_table.h"

using cdtb::render::kWinCount;
using cdtb::render::Win;
using cdtb::render::WindowSpec;

TEST(layout_table_has_every_window_in_order) {
    const auto specs = cdtb::render::window_specs();
    CHECK_EQ(static_cast<int>(specs.size()), kWinCount);
    for (int i = 0; i < kWinCount; ++i) {
        CHECK(static_cast<int>(specs[static_cast<std::size_t>(i)].id) == i);
        CHECK(&cdtb::render::window_spec(static_cast<Win>(i)) ==
              &specs[static_cast<std::size_t>(i)]);
    }
}

TEST(layout_titles_unique_labels_present) {
    std::set<std::string> titles;
    std::set<std::string> labels;
    for (const auto& s : cdtb::render::window_specs()) {
        CHECK(s.title != nullptr && s.title[0] != 0);
        CHECK(titles.insert(s.title).second);
        if (s.id != Win::Main) {
            CHECK(s.label != nullptr && s.label[0] != 0);
            // 라벨은 본창 체크박스의 ImGui ID 다. 겹치면 뒤쪽 하나가 먹통이 된다.
            CHECK(labels.insert(s.label).second);
        }
        CHECK(s.min_w <= s.w && s.min_h <= s.h);
    }
}

TEST(layout_everything_fits_1080p) {
    for (const auto& s : cdtb::render::window_specs()) {
        CHECK(s.x >= 0 && s.y >= 0);
        CHECK(s.x + s.w <= 1920.0f);
        CHECK(s.y + s.h <= 1080.0f);
    }
}

TEST(layout_default_open_windows_do_not_overlap) {
    const auto specs = cdtb::render::window_specs();
    for (const auto& a : specs) {
        if (!a.default_open && a.id != Win::Main) continue;
        for (const auto& b : specs) {
            if (&a == &b) continue;
            if (!b.default_open && b.id != Win::Main) continue;
            CHECK(!cdtb::render::specs_overlap(a, b));
        }
    }
}

TEST(layout_on_demand_windows_keep_main_visible_and_apart) {
    const auto specs = cdtb::render::window_specs();
    const WindowSpec& main = cdtb::render::window_spec(Win::Main);
    for (const auto& a : specs) {
        if (a.default_open || a.id == Win::Main) continue;
        CHECK(!cdtb::render::specs_overlap(a, main));
        for (const auto& b : specs) {
            if (&a == &b || b.default_open || b.id == Win::Main) continue;
            CHECK(!cdtb::render::specs_overlap(a, b));
        }
    }
}

TEST(layout_overlap_is_strict) {
    WindowSpec a{Win::Main, "a", "", true, 0, 0, 100, 100, 10, 10};
    WindowSpec b{Win::Items, "b", "", true, 100, 0, 100, 100, 10, 10};   // 맞닿음
    WindowSpec c{Win::Grant, "c", "", true, 99, 99, 10, 10, 1, 1};       // 1px 겹침
    CHECK(!cdtb::render::specs_overlap(a, b));
    CHECK(cdtb::render::specs_overlap(a, c));
}

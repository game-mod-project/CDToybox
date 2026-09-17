#include "harness.h"
#include "core/config.h"
#include "core/guard.h"
#include "core/log.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

static std::wstring temp_path(const wchar_t* leaf) {
    return (fs::temp_directory_path() / leaf).wstring();
}

TEST(log_writes_lines_to_file) {
    const std::wstring p = temp_path(L"cdtb_log_test.log");
    fs::remove(p);

    cdtb::log::init(p);
    cdtb::log::write(cdtb::log::Level::Info, "hello");
    cdtb::log::infof("value={}", 42);
    cdtb::log::shutdown();

    std::ifstream in(p);
    CHECK(in.good());
    const std::string all((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());
    CHECK(all.find("hello") != std::string::npos);
    CHECK(all.find("value=42") != std::string::npos);
    CHECK(all.find("INFO") != std::string::npos);
    in.close();
    fs::remove(p);
}

TEST(config_returns_defaults_when_file_missing) {
    const std::wstring p = temp_path(L"cdtb_cfg_missing.ini");
    fs::remove(p);
    const cdtb::Config c = cdtb::config::load(p);
    CHECK_EQ(c.toggle_key, 0x2D);
    CHECK_EQ(c.unload_key, 0x79);   // F10. 이동 키에서 멀리 뒀다
    CHECK_EQ(c.show_diagnostics, true);
    CHECK_EQ(c.equip_character_row, -1);   // 장비 창 캐릭터: 자동
}

TEST(config_round_trips) {
    const std::wstring p = temp_path(L"cdtb_cfg_roundtrip.ini");
    fs::remove(p);

    cdtb::Config w;
    w.toggle_key = 0x77;        // F8
    w.unload_key = 0x78;        // F9
    w.show_diagnostics = false;
    w.equip_character_row = 5;   // 웅카
    CHECK(cdtb::config::save(p, w));

    const cdtb::Config r = cdtb::config::load(p);
    CHECK_EQ(r.toggle_key, 0x77);
    CHECK_EQ(r.unload_key, 0x78);
    CHECK_EQ(r.show_diagnostics, false);
    CHECK_EQ(r.equip_character_row, 5);
    fs::remove(p);
}

TEST(config_equip_character_row_out_of_range_is_auto) {
    const std::wstring p = temp_path(L"cdtb_cfg_equip_row.ini");
    {
        std::ofstream out(p);
        out << "[CDToybox]\n";
        out << "equip_character_row = 70000\n";   // u16 밖 - 자동으로
    }
    CHECK_EQ(cdtb::config::load(p).equip_character_row, -1);
    {
        std::ofstream out(p);
        out << "equip_character_row = -3\n";
    }
    CHECK_EQ(cdtb::config::load(p).equip_character_row, -1);
    {
        std::ofstream out(p);
        out << "equip_character_row = 0\n";   // 클리프
    }
    CHECK_EQ(cdtb::config::load(p).equip_character_row, 0);
    fs::remove(p);
}

TEST(config_ignores_garbage_lines) {
    const std::wstring p = temp_path(L"cdtb_cfg_garbage.ini");
    {
        std::ofstream out(p);
        out << "[CDToybox]\n";
        out << "this line has no equals sign\n";
        out << "toggle_key = 0x50\n";
        out << "unknown_key = 999\n";
        out << "; comment\n";
    }
    const cdtb::Config c = cdtb::config::load(p);
    CHECK_EQ(c.toggle_key, 0x50);
    CHECK_EQ(c.unload_key, 0x79);   // 기본값 유지
    fs::remove(p);
}

// 1단계에서 관문을 열었다. 이 게임은 출시부터 현재 빌드까지
// co-op과 PvP가 전무한 순수 싱글플레이임을 확인했다.
// 관문 자체는 남아 있어, 멀티플레이가 출시되면 이 함수 하나로
// 모든 쓰기 기능을 차단할 수 있다.
TEST(guard_allows_modification_in_single_player) {
    CHECK_EQ(cdtb::guard::is_safe_to_modify(), true);
}

// ------------------------------------------------- 가방 확장을 설정에 남긴다
// 게임을 껐다 켜면 가방 확장이 사라졌다. 자동 재적용(`bag_auto_*`)은 **프로세스
// 메모리**에만 사는데, 세이브에 남는 칸(+0x16)은 새 산술이 안 읽어 더는 쓰지
// 않기 때문이다. 그래서 `knowledge_keep` 과 같은 방식으로 ini 에 남긴다.
// 표기: `bag_keep = <종류>:<목표>,...` - 종류는 게임의 u16 이라 표 순서가 바뀌어도
// 안전하다(색인을 적으면 표를 고칠 때 조용히 엉뚱한 가방에 걸린다).
TEST(bag_keep_parses_kind_and_target_pairs) {
    const auto v = cdtb::config::parse_bag_keep("1:240, 7:440");
    CHECK_EQ(v.size(), static_cast<std::size_t>(2));
    if (v.size() != 2) return;   // 없는 것을 읽으면 시험이 죽는다
    CHECK_EQ(v[0].kind, 1);
    CHECK_EQ(v[0].target, 240);
    CHECK_EQ(v[1].kind, 7);
    CHECK_EQ(v[1].target, 440);
}

TEST(bag_keep_drops_garbage_quietly) {
    // ini 는 사람이 고친다. 못 읽는 항목은 버리고 나머지는 살린다.
    const auto v = cdtb::config::parse_bag_keep("nonsense, 1:240, 7:, :440, 9:-5, 3:99999, 2:0");
    CHECK_EQ(v.size(), static_cast<std::size_t>(2));
    if (v.size() != 2) return;
    CHECK_EQ(v[0].kind, 1);
    CHECK_EQ(v[1].kind, 2);   // 0 은 "안 건드린다" 라 유효한 값이다
    CHECK_EQ(v[1].target, 0);
}

TEST(bag_keep_empty_is_empty) {
    CHECK(cdtb::config::parse_bag_keep("").empty());
    CHECK(cdtb::config::parse_bag_keep("   ").empty());
}

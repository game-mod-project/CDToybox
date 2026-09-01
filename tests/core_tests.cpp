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
}

TEST(config_round_trips) {
    const std::wstring p = temp_path(L"cdtb_cfg_roundtrip.ini");
    fs::remove(p);

    cdtb::Config w;
    w.toggle_key = 0x77;        // F8
    w.unload_key = 0x78;        // F9
    w.show_diagnostics = false;
    CHECK(cdtb::config::save(p, w));

    const cdtb::Config r = cdtb::config::load(p);
    CHECK_EQ(r.toggle_key, 0x77);
    CHECK_EQ(r.unload_key, 0x78);
    CHECK_EQ(r.show_diagnostics, false);
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

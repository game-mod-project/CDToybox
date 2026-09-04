cmake_minimum_required(VERSION 3.20)
project(CDToybox CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# 소스는 BOM 없는 UTF-8이다. 지정하지 않으면 MSVC가 시스템 코드
# 페이지(한국어 949)로 해석해 주석과 문자열 리터럴이 깨진다.
add_compile_options(/utf-8)

# ---------------------------------------------------------------- MinHook
add_library(minhook STATIC
    external/minhook/src/buffer.c
    external/minhook/src/hook.c
    external/minhook/src/trampoline.c
    external/minhook/src/hde/hde64.c
)
target_include_directories(minhook PUBLIC external/minhook/include)

# ------------------------------------------------------------------ ImGui
add_library(imgui STATIC
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/backends/imgui_impl_win32.cpp
    external/imgui/backends/imgui_impl_dx12.cpp
)
target_include_directories(imgui PUBLIC
    external/imgui
    external/imgui/backends
)

# ------------------------------------------------------------------- Core
add_library(cdtb_core STATIC
    src/mem/scanner.cpp
    src/mem/module.cpp
    src/mem/hook.cpp
    src/mem/safe_read.cpp
    src/mem/regions.cpp
    src/mem/value_scanner.cpp
    src/mem/reader.cpp
    src/mem/rtti.cpp
    src/mem/image_dump.cpp
    src/mem/watchpoint.cpp
    src/core/log.cpp
    src/core/config.cpp
    src/core/guard.cpp
)
target_include_directories(cdtb_core PUBLIC src)
target_link_libraries(cdtb_core PUBLIC minhook)

# -------------------------------------------------------------------- DLL
add_library(cdtoybox SHARED
    src/dllmain.cpp
    src/proxy/xinput_proxy.cpp
    src/render/d3d12_hook.cpp
    src/render/overlay.cpp
    src/render/diagnostics.cpp
    src/render/scan_panel.cpp
    src/render/item_panel.cpp
    src/render/icon_atlas.cpp
    src/render/grant_panel.cpp
    src/render/item_style.cpp
    src/render/stash_panel.cpp
    src/render/inventory_panel.cpp
    src/render/roster_panel.cpp
    src/game/camera.cpp
    src/game/analysis.cpp
    src/game/freecam.cpp
    src/game/localization.cpp
    src/game/items.cpp
    src/game/roster.cpp
    src/game/inventory.cpp
    src/game/item_view.cpp
    src/game/icon_map.cpp
    src/game/grant.cpp
    src/game/stash.cpp
    src/input/wndproc.cpp
    src/input/cursor.cpp
    src/proxy/exports.def
)
set_target_properties(cdtoybox PROPERTIES
    OUTPUT_NAME "xinput1_4"
    PREFIX ""
)
target_link_libraries(cdtoybox PRIVATE cdtb_core imgui d3d12 dxgi)

# ------------------------------------------------------------------ Tests
add_executable(cdtb_tests
    tests/main.cpp
    tests/smoke_tests.cpp
    tests/scanner_tests.cpp
    tests/module_tests.cpp
    tests/core_tests.cpp
    tests/hook_tests.cpp
    tests/safe_read_tests.cpp
    tests/regions_tests.cpp
    tests/value_scanner_tests.cpp
    tests/watchpoint_tests.cpp
    tests/localization_tests.cpp
    tests/items_tests.cpp
    tests/item_key_map_tests.cpp
    tests/inventory_tests.cpp
    tests/item_view_tests.cpp
    tests/icon_map_tests.cpp
    tests/cursor_tests.cpp
    tests/grant_tests.cpp
    tests/stash_tests.cpp
    tests/image_dump_tests.cpp
    src/input/cursor.cpp
    src/game/grant.cpp
    src/game/stash.cpp
    src/game/localization.cpp
    src/game/items.cpp
    src/game/inventory.cpp
    src/game/item_view.cpp
    src/game/icon_map.cpp
)
target_include_directories(cdtb_tests PRIVATE tests)
target_link_libraries(cdtb_tests PRIVATE cdtb_core)
# 후킹 테스트에서 대상 함수가 인라인/폴딩되지 않도록 최적화를 끈다.
target_compile_options(cdtb_tests PRIVATE /Od)
target_link_options(cdtb_tests PRIVATE /OPT:NOICF)

# -------------------------------------------------------------- Benchmark
# 테스트와 달리 최적화를 켜고 빌드한다. 스캐너 처리량을 재는 것이
# 목적이므로 /Od로 재면 의미가 없다.
add_executable(cdtb_bench tests/bench_scanner.cpp)
target_link_libraries(cdtb_bench PRIVATE cdtb_core)

# ------------------------------------------------------------------ Probe
# 게임에 외부에서 붙는 분석 도구. 인게임 UI가 아니라 명령줄 도구인
# 이유는 이 작업의 조작자가 사람이 아니라 에이전트이기 때문이다.
add_executable(cdtb_probe
    tools/probe/main.cpp
    tools/probe/remote.cpp
    tools/probe/findquat.cpp
    src/game/camera.cpp
    src/game/analysis.cpp
    src/game/localization.cpp
    src/game/items.cpp
    src/game/roster.cpp
    src/game/inventory.cpp
    src/game/item_view.cpp
    src/game/icon_map.cpp
    src/game/grant.cpp
    src/game/stash.cpp
)
target_include_directories(cdtb_probe PRIVATE tools/probe)
target_link_libraries(cdtb_probe PRIVATE cdtb_core psapi)

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
)
target_include_directories(cdtb_core PUBLIC src)

# ------------------------------------------------------------------ Tests
add_executable(cdtb_tests
    tests/main.cpp
    tests/smoke_tests.cpp
    tests/scanner_tests.cpp
    tests/module_tests.cpp
)
target_include_directories(cdtb_tests PRIVATE tests)
target_link_libraries(cdtb_tests PRIVATE cdtb_core)
# 후킹 테스트에서 대상 함수가 인라인/폴딩되지 않도록 최적화를 끈다.
target_compile_options(cdtb_tests PRIVATE /Od)
target_link_options(cdtb_tests PRIVATE /OPT:NOICF)

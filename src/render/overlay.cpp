#include "render/overlay.h"

#include <windows.h>

#include <d3d12.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/file_version.h"
#include "core/guard.h"
#include "core/log.h"
#include "core/slowlog.h"
#include "core/vk_name.h"
#include "input/cursor.h"
#include "input/mouse.h"
#include "input/wndproc.h"
#include "render/colors.h"
#include "render/d3d12_hook.h"
#include "render/diagnostics.h"
#include "render/icon_atlas.h"
#include "render/layout.h"
#include "render/grant_panel.h"
#include "render/inventory_panel.h"
#include "render/item_panel.h"
#include "render/log_panel.h"
#include "render/roster_panel.h"
#include "render/equip_panel.h"
#include "render/player_panel.h"
#include "render/notice.h"
#include "render/stash_panel.h"
#include "render/scan_panel.h"
#include "game/actors.h"
#include "game/clan.h"
#include "game/equip.h"
#include "game/freecam.h"
#include "game/items.h"
#include "game/nofall.h"
#include "game/player.h"
#include "game/specguard.h"
#include "game/spawnguard.h"
#include "mem/reader.h"

// 상태와 헬퍼는 detail에 둔다. cdtb::render::on_frame 이 이 상태에
// 접근해야 하므로 익명 네임스페이스를 쓸 수 없다.
namespace cdtb::overlay::detail {

// 공식 DX12 예제의 FrameContext와 같은 역할이다. fence_value가 핵심이다.
// 이것 없이 allocator를 Reset 하면 GPU가 아직 읽는 중일 수 있고, 그때
// 런타임은 호출을 버리고 디바이스를 제거한다(Microsoft 문서).
struct FrameCtx {
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12Resource* back_buffer = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    UINT64 fence_value = 0;
};

// DLL 이 있는 폴더. ImGui 설정 파일을 여기 둔다.
std::string self_dir_utf8() {
    HMODULE self = nullptr;
    if (!::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&self_dir_utf8), &self)) {
        return {};
    }
    wchar_t path[MAX_PATH]{};
    const DWORD n = ::GetModuleFileNameW(self, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::wstring w(path, n);
    const auto slash = w.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    w = w.substr(0, slash + 1);

    const int need = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr,
                                           0, nullptr, nullptr);
    if (need <= 1) return {};
    std::string out(static_cast<std::size_t>(need - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), need, nullptr,
                          nullptr);
    return out;
}

Config g_cfg;
std::wstring g_ini_path;
bool g_visible = false;
bool g_ready = false;

// 핫키는 게임의 메시지 스레드에서 온다. ImGui와 D3D12 리소스의 수명
// 조작을 거기서 하면 렌더 스레드의 Present와 경합해 크래시한다.
// 요청만 세워두고 실제 해체는 on_frame(렌더 스레드)에서 처리한다.
volatile bool g_teardown_requested = false;

// 사용자가 언로드 키로 끈 상태. 이것이 없으면 해체 직후 다음 프레임이
// 곧바로 재초기화해 버려 "비활성화"가 아무 효과도 없다.
// 토글 키로 다시 켤 때 해제된다.
volatile bool g_user_disabled = false;

// ImGui 초기화는 세 단계이고 각각 따로 되돌려야 한다. 중간에서 실패했을
// 때 이미 만든 것만 정확히 해체하기 위해 단계별로 기록한다.
bool g_ctx_created = false;
bool g_win32_ready = false;
bool g_dx12_ready = false;

ID3D12Device* g_device = nullptr;
ID3D12DescriptorHeap* g_rtv_heap = nullptr;
ID3D12DescriptorHeap* g_srv_heap = nullptr;
ID3D12GraphicsCommandList* g_cmd_list = nullptr;
std::vector<FrameCtx> g_frames;

ID3D12Fence* g_fence = nullptr;
HANDLE g_fence_event = nullptr;
UINT64 g_fence_last = 0;

// ImGui 1.92부터 백엔드는 폰트 아틀라스 외 텍스처에도 SRV 디스크립터를
// 요구한다. 단일 디스크립터로는 부족하므로 작은 프리리스트를 둔다.
constexpr UINT kSrvHeapSize = 64;
std::vector<UINT> g_srv_free;
UINT g_srv_increment = 0;

void srv_alloc(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
               D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
    // RelWithDebInfo는 NDEBUG라 IM_ASSERT가 no-op이 된다. 고갈되면
    // back()/pop_back()이 UB이므로 직접 확인한다.
    if (g_srv_free.empty()) {
        log::errorf("SRV 디스크립터 고갈 - 힙 크기 {}개로는 부족하다",
                    kSrvHeapSize);
        cpu->ptr = g_srv_heap->GetCPUDescriptorHandleForHeapStart().ptr;
        gpu->ptr = g_srv_heap->GetGPUDescriptorHandleForHeapStart().ptr;
        return;
    }
    const UINT idx = g_srv_free.back();
    g_srv_free.pop_back();
    cpu->ptr = g_srv_heap->GetCPUDescriptorHandleForHeapStart().ptr +
               static_cast<SIZE_T>(idx) * g_srv_increment;
    gpu->ptr = g_srv_heap->GetGPUDescriptorHandleForHeapStart().ptr +
               static_cast<UINT64>(idx) * g_srv_increment;
}

void srv_free(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu,
              D3D12_GPU_DESCRIPTOR_HANDLE) {
    const SIZE_T base = g_srv_heap->GetCPUDescriptorHandleForHeapStart().ptr;
    g_srv_free.push_back(
        static_cast<UINT>((cpu.ptr - base) / g_srv_increment));
}

// 우리가 제출한 모든 작업이 끝날 때까지 기다린다.
// 백버퍼나 힙을 해제하기 전에 반드시 부른다.
void wait_for_pending(ID3D12CommandQueue* queue) {
    if (g_fence == nullptr || g_fence_event == nullptr || queue == nullptr) {
        return;
    }
    const UINT64 target = ++g_fence_last;
    if (FAILED(queue->Signal(g_fence, target))) return;
    if (g_fence->GetCompletedValue() < target) {
        if (SUCCEEDED(g_fence->SetEventOnCompletion(target, g_fence_event))) {
            ::WaitForSingleObject(g_fence_event, 2000);
        }
    }
}

// 이 프레임 컨텍스트의 GPU 작업이 끝났는지 확인하고, 아니면 기다린다.
void wait_for_frame(const FrameCtx& f) {
    if (g_fence == nullptr || f.fence_value == 0) return;
    if (g_fence->GetCompletedValue() >= f.fence_value) return;
    if (SUCCEEDED(g_fence->SetEventOnCompletion(f.fence_value,
                                                g_fence_event))) {
        ::WaitForSingleObject(g_fence_event, 2000);
    }
}

void release_resources() {
    g_srv_free.clear();
    for (auto& f : g_frames) {
        if (f.allocator != nullptr) f.allocator->Release();
        if (f.back_buffer != nullptr) f.back_buffer->Release();
    }
    g_frames.clear();

    if (g_cmd_list != nullptr) { g_cmd_list->Release(); g_cmd_list = nullptr; }
    if (g_srv_heap != nullptr) { g_srv_heap->Release(); g_srv_heap = nullptr; }
    if (g_rtv_heap != nullptr) { g_rtv_heap->Release(); g_rtv_heap = nullptr; }
    if (g_fence != nullptr) { g_fence->Release(); g_fence = nullptr; }
    if (g_fence_event != nullptr) {
        ::CloseHandle(g_fence_event);
        g_fence_event = nullptr;
    }
    if (g_device != nullptr) { g_device->Release(); g_device = nullptr; }
    g_fence_last = 0;
}

// ImGui와 D3D12 리소스를 전부 해체한다. g_visible은 건드리지 않으므로
// 해상도 변경 후 재초기화해도 사용자가 열어둔 상태가 유지된다.
void teardown(ID3D12CommandQueue* queue) {
    // 단계마다 남긴다. 여기서 멈추면 어느 줄에서 멈췄는지
    // 로그가 지목해야 한다. 2026-09-01 정지 때는 요청 줄만 남고
    // 이 함수의 흔적이 전혀 없어, 렌더 스레드가 여기 오기도 전에
    // 멈춰 있었음을 알 수 있었다.
    log::infof("해체 1: 스캔 패널 정리");
    cdtb::render::shutdown_scan_panel();   // 워커 스레드를 먼저 정리한다
    log::infof("해체 2: GPU 대기");
    wait_for_pending(queue);   // GPU가 우리 리소스를 놓을 때까지
    if (g_dx12_ready) { ImGui_ImplDX12_Shutdown(); g_dx12_ready = false; }
    if (g_win32_ready) { ImGui_ImplWin32_Shutdown(); g_win32_ready = false; }
    if (g_ctx_created) { ImGui::DestroyContext(); g_ctx_created = false; }
    // GPU 자원은 위 DX12 Shutdown 이 정리했다. 우리 객체만 지운다.
    cdtb::render::unload_icon_atlas();
    log::infof("해체 3: ImGui 정리 완료");
    input::remove();
    release_resources();
    log::infof("해체 4: 리소스 해제 완료");
    g_ready = false;
}

bool initialize(IDXGISwapChain3* sc, ID3D12CommandQueue* queue) {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(sc->GetDesc(&desc))) {
        log::errorf("SwapChain GetDesc 실패");
        return false;
    }
    if (FAILED(sc->GetDevice(IID_PPV_ARGS(&g_device)))) {
        log::errorf("SwapChain GetDevice 실패");
        return false;
    }

    const UINT count = desc.BufferCount;

    if (FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                     IID_PPV_ARGS(&g_fence)))) {
        log::errorf("CreateFence 실패");
        return false;
    }
    g_fence_event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (g_fence_event == nullptr) {
        log::errorf("CreateEvent 실패: {}", ::GetLastError());
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
    rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_desc.NumDescriptors = count;
    rtv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(g_device->CreateDescriptorHeap(&rtv_desc,
                                              IID_PPV_ARGS(&g_rtv_heap)))) {
        log::errorf("RTV 힙 생성 실패");
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};
    srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_desc.NumDescriptors = kSrvHeapSize;
    srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(g_device->CreateDescriptorHeap(&srv_desc,
                                              IID_PPV_ARGS(&g_srv_heap)))) {
        log::errorf("SRV 힙 생성 실패");
        return false;
    }
    g_srv_increment = g_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    g_srv_free.clear();
    g_srv_free.reserve(kSrvHeapSize);
    for (UINT i = kSrvHeapSize; i > 0; --i) g_srv_free.push_back(i - 1);

    const UINT rtv_size = g_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE handle =
        g_rtv_heap->GetCPUDescriptorHandleForHeapStart();

    g_frames.resize(count);
    for (UINT i = 0; i < count; ++i) {
        if (FAILED(g_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&g_frames[i].allocator)))) {
            log::errorf("CommandAllocator {} 생성 실패", i);
            return false;
        }
        if (FAILED(sc->GetBuffer(i, IID_PPV_ARGS(&g_frames[i].back_buffer)))) {
            log::errorf("백버퍼 {} 획득 실패", i);
            return false;
        }
        g_device->CreateRenderTargetView(g_frames[i].back_buffer, nullptr,
                                         handle);
        g_frames[i].rtv = handle;
        g_frames[i].fence_value = 0;
        handle.ptr += rtv_size;
    }

    if (FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           g_frames[0].allocator, nullptr,
                                           IID_PPV_ARGS(&g_cmd_list)))) {
        log::errorf("CommandList 생성 실패");
        return false;
    }
    g_cmd_list->Close();

    ImGui::CreateContext();
    g_ctx_created = true;
    ImGuiIO& io = ImGui::GetIO();

    // 창 위치와 크기를 기억한다. ImGui 가 알아서 저장·복원하므로
    // 경로만 잡아 주면 된다. 게임의 작업 디렉터리는 어디가 될지
    // 모르므로 DLL 옆에 둔다.
    //
    // 문자열 수명은 우리가 진다 - ImGui 는 포인터만 들고 있는다.
    static std::string ini_path = detail::self_dir_utf8() + "cdtoybox_ui.ini";
    io.IniFilename = ini_path.empty() ? nullptr : ini_path.c_str();

    // 기본 폰트(ProggyClean)에는 한글 글리프가 없어 ??로 표시된다.
    // 1.92부터 글리프는 필요할 때 동적으로 래스터화되므로 범위를 지정할
    // 필요 없이 한글이 든 폰트를 얹기만 하면 된다. 파일이 없을 때
    // AddFontFromFileTTF가 단언에 걸리므로 존재를 먼저 확인한다.
    constexpr const char* kKoreanFont = "C:\\Windows\\Fonts\\malgun.ttf";
    if (::GetFileAttributesA(kKoreanFont) != INVALID_FILE_ATTRIBUTES) {
        io.Fonts->AddFontFromFileTTF(kKoreanFont, 18.0f);
        log::infof("한글 폰트 로드: {}", kKoreanFont);
    } else {
        log::warnf("맑은 고딕을 찾지 못했다 - 기본 폰트를 쓴다 (한글 깨짐)");
    }

    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(desc.OutputWindow)) {
        log::errorf("ImGui_ImplWin32_Init 실패");
        return false;
    }
    g_win32_ready = true;

    // 1.91.5에서 위치인자 초기화가 obsolete 되었다. InitInfo를 쓴다.
    // CommandQueue는 백엔드가 텍스처 업로드에 직접 쓴다. 반드시 이
    // 스왑체인과 짝지어진 큐여야 한다.
    // RTVFormat은 게임의 실제 스왑체인 포맷을 그대로 넘긴다. 상수로
    // 박으면 HDR이나 sRGB 스왑체인에서 렌더가 깨진다.
    ImGui_ImplDX12_InitInfo info{};
    info.Device = g_device;
    info.CommandQueue = queue;
    info.NumFramesInFlight = static_cast<int>(count);
    info.RTVFormat = desc.BufferDesc.Format;
    info.SrvDescriptorHeap = g_srv_heap;
    info.SrvDescriptorAllocFn = srv_alloc;
    info.SrvDescriptorFreeFn = srv_free;
    if (!ImGui_ImplDX12_Init(&info)) {
        log::errorf("ImGui_ImplDX12_Init 실패");
        return false;
    }
    g_dx12_ready = true;

    // 아이콘은 있으면 좋은 것이다. 실패해도 오버레이는 그대로 뜬다.
    cdtb::render::load_icon_atlas();

    input::install(desc.OutputWindow);
    log::infof("오버레이 초기화 완료: 백버퍼 {}개, 포맷 {}, hwnd={}, queue={}",
               count, static_cast<int>(desc.BufferDesc.Format),
               static_cast<void*>(desc.OutputWindow),
               static_cast<void*>(queue));
    return true;
}

// 어떤 창을 띄울지. 본창에서 켜고 끈다.
//
// 창마다 ✕ 를 달아 치울 수 있게 했으면, **다시 여는 자리**가 반드시
// 있어야 한다. 없으면 한 번 닫은 창은 영영 못 본다. 그 자리가 본창
// 이고, 그래서 본창은 닫히지 않는다.
// 켜 둔 창. 초기값은 배치 표의 default_open 이다.
bool g_show[cdtb::render::kWinCount] = {};
bool g_show_inited = false;

bool& shown(cdtb::render::Win w) {
    return g_show[static_cast<int>(w)];
}

void init_show_flags() {
    if (g_show_inited) return;
    for (const auto& s : cdtb::render::window_specs()) {
        shown(s.id) = s.default_open;
    }
    g_show_inited = true;
}

// 켜 둔 창만 그린다. ✕ 를 누르면 ImGui 가 플래그를 내려 주므로
// 다음 프레임부터 안 그린다.
void draw_windows() {
    using cdtb::render::Win;
    if (shown(Win::Items)) cdtb::render::draw_item_panel(&shown(Win::Items));
    if (shown(Win::Grant)) cdtb::render::draw_grant_panel(&shown(Win::Grant));
    if (shown(Win::Stash)) cdtb::render::draw_stash_panel(&shown(Win::Stash));
    if (shown(Win::Inventory)) {
        cdtb::render::draw_inventory_panel(&shown(Win::Inventory));
    }
    if (shown(Win::Roster)) cdtb::render::draw_roster_panel(&shown(Win::Roster));
    if (shown(Win::Equip)) cdtb::render::draw_equip_panel(&shown(Win::Equip));
    if (shown(Win::Player)) cdtb::render::draw_player_panel(&shown(Win::Player));
    if (shown(Win::Camera)) cdtb::render::draw_camera_panel(&shown(Win::Camera));
    if (shown(Win::Log)) cdtb::render::draw_log_panel(&shown(Win::Log));
}

// 게임 exe 의 버전. 한 번 읽어 둔다 - 매 프레임 자원을 뒤질 이유가 없다.
const std::string& game_version_line() {
    static std::string line = [] {
        wchar_t path[MAX_PATH]{};
        // 실패하거나 잘리면 경로가 온전하지 않다. 빈 경로로 두면 아래가
        // 실패해 "(버전 확인 불가)" 로 떨어진다.
        const DWORD n = ::GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) path[0] = 0;
        std::string v;
        return cdtb::file_version_string(path, &v)
                   ? "Crimson Desert " + v
                   : std::string("Crimson Desert (버전 확인 불가)");
    }();
    return line;
}

void draw_ui() {
    init_show_flags();
    cdtb::render::begin_window(cdtb::render::Win::Main, nullptr);

    // 창 목록이 먼저다. 예전에는 FPS 와 개발 진단이 본창의 전부라,
    // 무슨 창이 있는지 알 방법이 아예 없었다. 글자는 배치 표의 label 이라
    // 창 제목과 어긋날 수 없다. 2열 격자.
    ImGui::TextUnformatted("창");
    int n = 0;
    for (const auto& s : cdtb::render::window_specs()) {
        if (s.id == cdtb::render::Win::Main) continue;
        if ((n & 1) == 1) ImGui::SameLine(200.0f);
        ImGui::Checkbox(s.label, &shown(s.id));
        // 게임 메모리에 쓰는 창은 표식을 단다. "(쓰기)" 는 200px 열을 넘어 옆 열과
        // 겹쳤다 - 한 글자로 줄이고 뜻은 툴팁과 아래 범례가 말한다.
        const bool writes = s.id == cdtb::render::Win::Roster ||
                            s.id == cdtb::render::Win::Equip ||
                            s.id == cdtb::render::Win::Player ||
                            s.id == cdtb::render::Win::Inventory;
        if (writes) {
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("이 창은 게임 메모리를 바꿉니다");
            }
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextDisabled("*");
        }
        ++n;
    }
    ImGui::TextDisabled("* 게임 메모리를 바꾸는 창");

    // 보관함 창을 닫아도 일괄 지급은 stash_tick 이 이어 간다 - 진행을 여기서 보인다.
    std::size_t q_done = 0, q_total = 0;
    if (!cdtb::render::stash_body_visible() &&
        cdtb::render::stash_queue_progress(&q_done, &q_total)) {
        ImGui::TextColored(cdtb::render::col::kBusy, "보관함 지급 중 %zu / %zu",
                           q_done, q_total);
        ImGui::SameLine();
        if (ImGui::SmallButton("중단")) cdtb::render::stash_queue_cancel();
    }
    // 창이 닫혀 있거나 접혀 있으면 완료·세션 없음·중단 알림도 여기서만 볼 수 있다.
    if (!cdtb::render::stash_body_visible()) {
        cdtb::render::notice_draw(cdtb::render::stash_notice());
    }

    ImGui::Separator();
    // 안내는 설정값에서 만든다. 키를 옮기고 안내를 안 고쳐 거짓이 된 적이 있다.
    char kb1[16], kb2[16];
    ImGui::TextDisabled("%s 토글 · %s 비활성화",
                        cdtb::vk_name(g_cfg.toggle_key, kb1, sizeof(kb1)),
                        cdtb::vk_name(g_cfg.unload_key, kb2, sizeof(kb2)));
    ImGui::Text("%s / %.1f FPS", game_version_line().c_str(),
                ImGui::GetIO().Framerate);
    if (!cdtb::guard::is_safe_to_modify()) {
        ImGui::TextColored(cdtb::render::col::kWarn, "쓰기 기능이 잠겨 있습니다");
    }

    if (!g_cfg.show_diagnostics) {
        ImGui::End();
        draw_windows();
        return;
    }

    const auto& d = cdtb::render::diagnostics();
    ImGui::Separator();

    if (!d.error.empty()) {
        // 진단이 죽었다고 창 8개까지 숨기면 안 된다(그랬다 - 진단은 기본 켜짐).
        ImGui::TextColored(cdtb::render::col::kBad, "오류: %s", d.error.c_str());
        ImGui::End();
        draw_windows();
        return;
    }

    if (ImGui::CollapsingHeader("모듈")) {
        ImGui::Text("베이스    0x%llX",
                    static_cast<unsigned long long>(d.game_base));
        ImGui::Text("이미지    %.1f MB",
                    static_cast<double>(d.game_size) / (1024.0 * 1024.0));
        ImGui::Text("실행 섹션 %zu개", d.game_exec.size());
        ImGui::Indent();
        for (const auto& r : d.game_exec) {
            ImGui::Text("0x%llX  %.1f MB",
                        static_cast<unsigned long long>(r.begin),
                        static_cast<double>(r.size) / (1024.0 * 1024.0));
        }
        ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("스캐너 진단")) {
        const bool exact = d.self_marker_found &&
                           d.self_marker_found_at == d.self_marker_expected;
        ImGui::TextColored(
            exact ? cdtb::render::col::kOk : cdtb::render::col::kBad,
            "자기 모듈 마커: %s", exact ? "일치" : "불일치");
        ImGui::Text("  기대 0x%llX / 발견 0x%llX",
                    static_cast<unsigned long long>(d.self_marker_expected),
                    static_cast<unsigned long long>(d.self_marker_found_at));
        ImGui::Text("게임 프롤로그: %zu회 / %.1f ms", d.prologue_hits,
                    d.prologue_ms);
    }

    ImGui::End();
    draw_windows();
}

}  // namespace cdtb::overlay::detail

namespace cdtb::overlay {

using namespace detail;

void set_config(const Config& cfg, const std::wstring& ini_path) {
    g_cfg = cfg;
    g_ini_path = ini_path;
    // 장비 창의 캐릭터 선택을 되살린다. 발견은 분석 스레드가 한다.
    cdtb::game::equip_select_character(
        cfg.equip_character_row < 0
            ? cdtb::game::kEquipAutoCharacter
            : static_cast<std::uint16_t>(cfg.equip_character_row));
}

void show_window(cdtb::render::Win w) {
    init_show_flags();
    shown(w) = true;
}

std::vector<Config::SocketCapPart> socket_cap_setting() {
    return g_cfg.socket_cap_parts;
}

bool set_socket_cap_setting(const std::vector<Config::SocketCapPart>& parts) {
    for (const auto& p : parts) {
        if (p.want < 0 || p.want > static_cast<int>(cdtb::game::kSocketSlotMax)) {
            return false;
        }
    }
    g_cfg.socket_cap_parts = parts;
    // 부위 목록을 한 번이라도 저장하면 (구) 일괄 설정은 뜻을 잃는다.
    // 둘이 남아 있으면 다음 실행에 어느 쪽이 걸릴지 헷갈린다.
    g_cfg.socket_cap = 0;
    if (g_ini_path.empty()) return false;
    return cdtb::config::save(g_ini_path, g_cfg);
}

int equip_character_setting() { return g_cfg.equip_character_row; }

bool set_equip_character_setting(int row) {
    g_cfg.equip_character_row = row < 0 ? -1 : row;
    if (g_ini_path.empty()) return false;
    return cdtb::config::save(g_ini_path, g_cfg);
}

bool is_visible() {
    // 그리기가 꺼졌으면 열려 있다고 하지 않는다. 그래야 wndproc 이
    // 입력을 가로채지 않는다 - 오버레이가 죽은 뒤 키보드까지 막혔다.
    return g_visible && g_ready && !render::render_disabled();
}

void toggle() {
    g_visible = !g_visible;
    if (g_visible) g_user_disabled = false;   // 켜면 비활성화를 해제한다
    log::infof("오버레이 {}", g_visible ? "표시" : "숨김");
}

bool handle_hotkey(int vk) {
    if (vk == g_cfg.toggle_key) { toggle(); return true; }
    if (vk == g_cfg.unload_key) { shutdown(); return true; }
    return false;
}

// 메시지 스레드에서 불릴 수 있으므로 요청만 남긴다.
// 실제 해체는 on_frame이 렌더 스레드에서 수행한다.
void shutdown() {
    g_user_disabled = true;
    if (!g_ready && !g_ctx_created) return;
    g_teardown_requested = true;
    log::infof("오버레이 비활성화 요청 - 다음 프레임에 해체한다");
}

}  // namespace cdtb::overlay

// ------------------------------------------------ d3d12_hook.h 의 콜백 구현
namespace cdtb::render {

void on_frame(IDXGISwapChain3* sc, ID3D12CommandQueue* queue) {
    using namespace cdtb::overlay::detail;

    // 오버레이 상태와 무관하게 매 프레임 돈다. 프리카메라는
    // 오버레이를 꺼 둔 채로도 써야 하고, 아래의 조기 반환들에
    // 걸리면 안 된다.
    cdtb::game::freecam_tick();

    // 해체는 반드시 이 스레드에서 한다 (overlay::shutdown 주석 참조).
    if (g_teardown_requested) {
        g_frame_stage = kStageTeardown;
        g_teardown_requested = false;
        g_visible = false;
        // 훅을 떼기 전에 OS 커서를 원래 상태로 돌린다. 순서가 바뀌면
        // 되돌릴 원본 함수가 없다.
        // 해체 뒤 다시 켤 때 옛 큐가 그 자리에서 이어지지 않게 접는다(로그 한 줄).
        if (cdtb::render::stash_queue_progress(nullptr, nullptr)) {
            cdtb::render::stash_queue_cancel();
        }
        cdtb::render::stash_flush();   // 해체 전에 저장 대기 중인 보관함 변경을 쓴다
        input::cursor_guard_sync(false);
        input::mouse_sync(false);
        input::cursor_guard_remove();
        teardown(queue);
        log::infof("오버레이 비활성화 완료 - 토글 키로 재초기화 가능");
        g_frame_stage = kStageIdle;
        return;
    }

    // 사용자가 껐으면 토글 키로 다시 켜기 전까지 아무것도 만들지 않는다.
    if (g_user_disabled) { g_frame_stage = kStageIdle; return; }

    if (!g_ready) {
        g_frame_stage = kStageInitialize;
        if (!initialize(sc, queue)) {
            log::errorf("오버레이 초기화 실패 - 만든 것만 해체하고 중단한다");
            // 커서 가드가 "열림" 에 갇히지 않게 - 이 반환은 아래 cursor_guard_sync 에
            // 못 미치므로, 열려 있던 가드는 여기서 닫는다(리뷰 H4). is_visible 은
            // g_ready 가 꺼져 이미 false 라 디투어는 통과 중이다.
            input::cursor_guard_sync(false);
            input::mouse_sync(false);
            teardown(queue);
            g_frame_stage = kStageIdle;
            return;
        }
        g_ready = true;
    }
    // 커서 가드. 게임은 카메라를 돌리려고 매 프레임 커서를 화면
    // 중앙으로 되돌리고 창 안에 가둔다. 그대로 두면 오버레이를 열어도
    // 커서가 한 점에 붙박여 안 움직이고, 그 자리에 OS 커서가 남아
    // 두 개로 보인다. 열려 있는 동안만 그 호출들을 막는다.
    if (!input::cursor_guard_installed()) {
        input::cursor_guard_install(&cdtb::overlay::is_visible);
    }
    input::cursor_guard_sync(cdtb::overlay::is_visible());
    input::mouse_sync(cdtb::overlay::is_visible());

    // 플레이어 치트 freeze 는 **가시성과 무관하게** 매 Present(~16ms) 적용한다.
    // 예전엔 아래 !g_visible return 뒤에 있어, 게임하려 오버레이를 숨기면
    // freeze 가 멈춰 무적이 안 먹었다. 발견/게이지 캐시는 분석 루프가 담당.
    {
        const mem::LocalReader reader;
        cdtb::game::player_apply(reader);
        // 낙사 방지가 비교할 내 root. 캐릭터 교체·지역 이동으로 바뀌므로
        // 캐시하지 않고 여기서 매번 다시 계산한다(분석 통과는 너무 느리다).
        cdtb::game::nofall_refresh(reader);
        // 명령 파일 스레드가 부탁한 근처 액터 갱신은 여기(렌더 스레드)서 한다.
        cdtb::game::live_actors_tick(reader);
        // 특수아이템 크래시 가드를 첫 프레임에 설치(모듈 베이스만 필요).
        // 분석 루프의 늦은 지점에서 설치하면 그 전에 지급/가방 열기로 크래시.
        cdtb::game::specguard_install(reader);
        cdtb::game::spawnguard_install(reader);
        // 보관함 자동 저장·일괄 지급 큐. 오버레이를 숨겨도, 창을 닫아도 돈다 -
        // ImGui 프레임 밖이지만 그리지 않고, 시각은 ImGui 시계(NewFrame 안에서만
        // 흐른다)가 아니라 단조 시계를 쓴다(3단계 리뷰). draw_windows 에 두면 숨김
        // 중 큐·저장이 멈췄다. 가드 설치 뒤에 둔다 - 큐의 지급이 가드보다 먼저 돌지
        // 않게(위 주석의 규약).
        cdtb::render::stash_tick();

        // 획득 뒤처리. 2338 은 명부 레코드에 그 순간의 야생 액터
        // 핸들을 박아 두는데, 그 액터가 사라져도 값은 남아 게임이
        // "이미 소환됨" 으로 오판한다 - 그러면 그 개체는 소환도
        // 해제도 안 된다(사용자 증상, 실측 2026-09-09). 지금까지
        // 지역 이동·세이브 로드로만 풀리던 그것이다. 가시성과
        // 무관하게 돌아야 오버레이를 닫아 둠 때도 풀린다.
        if (const mem::Rtti* rtti = cdtb::game::clan_rtti()) {
            cdtb::log::Slow slow_c("획득 뒤처리", 8.0);
            cdtb::game::tick_hire_cleanup(*rtti, reader);
        }

        // 소켓 상한은 아이템표에 거는 것이라 **매 실행 다시 걸어야 한다**
        // (표는 exe 에서 새로 읽힌다). 설정이 켜져 있으면 표가 올라온
        // 뒤 한 번만 건다. 여기서 하는 이유는 specguard 와 같다 - 분석
        // 루프는 목적을 이루면 빠져나가므로 걸 자리가 없다.
        // 표가 올라온 그 순간 한 번만 본다. 설정이 꺼져 있어도 그때
        // 끝낸다 - 안 그러면 나중에 화면에서 체크박스를 켜는 순간
        // 여기서도 걸려, "다음 실행부터" 라는 문구와 어긋난다.
        static bool s_cap_done = false;
        if (!s_cap_done && cdtb::game::items_ready()) {
            s_cap_done = true;
            std::vector<cdtb::game::SocketCapRule> rules;
            for (const auto& p : g_cfg.socket_cap_parts) {
                rules.push_back(cdtb::game::SocketCapRule{
                    cdtb::game::SocketPart{
                        static_cast<std::uint8_t>(p.category),
                        static_cast<std::uint16_t>(p.equip_type)},
                    static_cast<std::uint32_t>(p.want)});
            }
            // (구) 일괄 설정: 부위 목록이 없을 때만, 원래 소켓이 있는
            // 부위에만 건다. 예전 ini 를 그대로 읽어 주기 위한 것이다.
            if (rules.empty() && g_cfg.socket_cap > 0) {
                for (const auto& info : cdtb::game::socket_parts()) {
                    if (info.with_socket == 0) continue;
                    rules.push_back(cdtb::game::SocketCapRule{
                        info.part,
                        static_cast<std::uint32_t>(g_cfg.socket_cap)});
                }
            }
            if (!rules.empty()) {
                cdtb::game::socket_cap_apply(reader, rules);
            }
        }
    }

    if (!g_visible) { g_frame_stage = kStageIdle; return; }

    const UINT idx = sc->GetCurrentBackBufferIndex();
    if (idx >= g_frames.size()) { g_frame_stage = kStageIdle; return; }
    FrameCtx& f = g_frames[idx];

    // 이 얼로케이터의 이전 제출이 끝났는지 확인한다. 이 대기가 없으면
    // 런타임이 호출을 버리고 디바이스를 제거한다(Microsoft 문서).
    g_frame_stage = kStageWaitFence;
    wait_for_frame(f);

    g_frame_stage = kStageNewFrame;

    // 커서는 ImGui 가 직접 그린다. OS 것만 쓰게 했더니 오버레이
    // 위에서 아예 안 보였다 - 백엔드가 창 위에서 OS 커서를 끈다.
    // 대신 wndproc 에서 오버레이 위의 OS 커서를 확실히 끈다.
    //
    // 백엔드의 NewFrame 이 이 값을 보고 OS 커서를 처리하므로 그보다
    // 먼저 세운다.
    ImGui::GetIO().MouseDrawCursor = true;

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    // 마우스 좌표(와 창 메시지가 끊겼을 때의 버튼·휠)를 직접 넣는다. 백엔드의 대체
    // 경로는 WM_MOUSEMOVE 를 한 번 받으면 꺼지는데 게임의 마우스룩은 그 메시지를 안
    // 준다(굳음의 원인, mouse.h). 큐에 얹히므로 ImGui::NewFrame 이 함께 처리한다.
    input::mouse_feed_frame();
    ImGui::NewFrame();

    // 게임의 키 상태 조회를 거를지 - 글자 입력칸에 포커스가 있을 때만.
    input::cursor_guard_set_want_keyboard(ImGui::GetIO().WantCaptureKeyboard);

    g_frame_stage = kStageDrawUi;
    draw_ui();

    g_frame_stage = kStageImGuiRender;
    ImGui::Render();

    g_frame_stage = kStageAllocatorReset;
    f.allocator->Reset();
    g_cmd_list->Reset(f.allocator, nullptr);

    g_frame_stage = kStageBarrierToRT;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = f.back_buffer;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g_cmd_list->ResourceBarrier(1, &barrier);

    g_frame_stage = kStageOMSetRenderTargets;
    g_cmd_list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);

    g_frame_stage = kStageSetDescriptorHeaps;
    g_cmd_list->SetDescriptorHeaps(1, &g_srv_heap);

    g_frame_stage = kStageRenderDrawData;
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_cmd_list);

    g_frame_stage = kStageBarrierToPresent;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_cmd_list->ResourceBarrier(1, &barrier);

    g_frame_stage = kStageCloseList;
    g_cmd_list->Close();

    g_frame_stage = kStageExecute;
    ID3D12CommandList* lists[] = {g_cmd_list};
    queue->ExecuteCommandLists(1, lists);

    // 이 프레임 컨텍스트를 다시 쓰기 전에 기다릴 지점을 기록한다.
    g_frame_stage = kStageSignal;
    if (SUCCEEDED(queue->Signal(g_fence, g_fence_last + 1))) {
        ++g_fence_last;
        f.fence_value = g_fence_last;
    }

    g_frame_stage = kStageIdle;
}

void on_resize(IDXGISwapChain3*, ID3D12CommandQueue* queue) {
    using namespace cdtb::overlay::detail;
    if (!g_ready) return;

    // 백버퍼 참조를 쥔 채로는 ResizeBuffers가 실패한다. 전체를 해체하고
    // 다음 Present에서 새 크기로 다시 만든다. g_visible은 유지된다.
    teardown(queue);
    log::infof("ResizeBuffers 감지 - 다음 프레임에 재초기화한다");
}

}  // namespace cdtb::render

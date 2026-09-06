#include "render/icon_atlas.h"

#include <windows.h>

#include <imgui.h>
#include <imgui_internal.h>   // RegisterUserTexture (EXPERIMENTAL)

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/log.h"
#include "game/icon_map.h"

namespace cdtb::render {
namespace {

game::IconAtlas g_atlas;
ImTextureData* g_tex = nullptr;

// DLL 이 놓인 폴더. 아틀라스는 그 옆에 둔다.
std::wstring module_dir() {
    HMODULE self = nullptr;
    if (!::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&module_dir), &self)) {
        return {};
    }
    wchar_t path[MAX_PATH]{};
    const DWORD n = ::GetModuleFileNameW(self, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::wstring s(path, n);
    const auto slash = s.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? std::wstring{} : s.substr(0, slash + 1);
}

bool read_file(const std::wstring& path, std::vector<std::uint8_t>* out) {
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(h, &size) || size.QuadPart <= 0 ||
        size.QuadPart > (256ll << 20)) {
        ::CloseHandle(h);
        return false;
    }
    out->resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t done = 0;
    while (done < out->size()) {
        DWORD got = 0;
        const DWORD want = static_cast<DWORD>(
            (out->size() - done > 0x10000000u) ? 0x10000000u
                                               : (out->size() - done));
        if (!::ReadFile(h, out->data() + done, want, &got, nullptr) ||
            got == 0) {
            ::CloseHandle(h);
            return false;
        }
        done += got;
    }
    ::CloseHandle(h);
    return true;
}

}  // namespace

bool load_icon_atlas() {
    if (g_tex != nullptr) return true;

    const std::wstring dir = module_dir();
    if (dir.empty()) return false;
    const std::wstring path = dir + L"cdtoybox_icons.bin";

    std::vector<std::uint8_t> buf;
    if (!read_file(path, &buf)) {
        // 아이콘은 있으면 좋은 것이다. 없다고 시끄럽게 굴지 않는다.
        log::infof("아이콘 아틀라스 없음 - 아이콘 없이 갑니다 "
                   "(tools/icons/build_icons.py 로 만듭니다)");
        return false;
    }
    game::IconAtlas atlas;
    if (!game::parse_icon_atlas(buf.data(), buf.size(), &atlas)) {
        log::warnf("아이콘 아틀라스가 깨졌습니다 ({} 바이트)", buf.size());
        return false;
    }

    ImTextureData* tex = IM_NEW(ImTextureData)();
    tex->Create(ImTextureFormat_RGBA32, static_cast<int>(atlas.width()),
                static_cast<int>(atlas.height()));
    if (tex->Pixels == nullptr) {
        IM_DELETE(tex);
        log::errorf("아이콘 텍스처 할당 실패 ({}x{})", atlas.width(),
                    atlas.height());
        return false;
    }
    std::memcpy(tex->Pixels, buf.data() + atlas.pixels_offset,
                atlas.pixels_bytes());
    tex->UseColors = true;
    tex->Status = ImTextureStatus_WantCreate;
    ImGui::RegisterUserTexture(tex);

    g_tex = tex;
    g_atlas = std::move(atlas);
    log::infof("아이콘 아틀라스: {}x{}, 아이템 {}개", g_atlas.width(),
               g_atlas.height(), g_atlas.mapping.size());
    return true;
}

void unload_icon_atlas() {
    // GPU 자원은 ImGui_ImplDX12_Shutdown 이 이미 정리했다. 여기서는
    // 우리가 만든 객체만 지운다.
    if (g_tex != nullptr) {
        IM_DELETE(g_tex);
        g_tex = nullptr;
    }
    g_atlas = game::IconAtlas{};
}

bool icons_ready() { return g_tex != nullptr && g_atlas.valid(); }

int icon_count() { return static_cast<int>(g_atlas.mapping.size()); }

IconRef icon_for(std::uint32_t key) {
    IconRef r;
    if (!icons_ready()) return r;
    std::uint32_t cell = 0;
    if (!game::find_icon_cell(g_atlas, key, &cell)) return r;
    if (cell >= g_atlas.cols * g_atlas.rows) return r;

    const float w = static_cast<float>(g_atlas.width());
    const float h = static_cast<float>(g_atlas.height());
    const float cx = static_cast<float>((cell % g_atlas.cols) * g_atlas.cell);
    const float cy = static_cast<float>((cell / g_atlas.cols) * g_atlas.cell);
    const float s = static_cast<float>(g_atlas.cell);

    r.tex = g_tex->GetTexRef();
    r.uv0 = ImVec2(cx / w, cy / h);
    r.uv1 = ImVec2((cx + s) / w, (cy + s) / h);
    r.valid = true;
    return r;
}

}  // namespace cdtb::render

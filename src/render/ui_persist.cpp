#include "render/ui_persist.h"

#include <string>

#include "imgui.h"
#include "imgui_internal.h"
#include "render/ui_persist_state.h"

namespace cdtb::render {
namespace {

UiPersist g_store;

constexpr const char* kTypeName = "CDToyboxUI";

void* read_open(ImGuiContext*, ImGuiSettingsHandler*, const char*) {
    // 구역이 하나뿐이라 구분할 것이 없다. 널이 아니기만 하면 된다.
    return reinterpret_cast<void*>(1);
}

void read_line(ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line) {
    g_store.parse_line(line);
}

void write_all(ImGuiContext*, ImGuiSettingsHandler* handler,
               ImGuiTextBuffer* buf) {
    buf->appendf("[%s][State]\n", handler->TypeName);
    const std::string text = g_store.serialize();
    buf->append(text.c_str(), text.c_str() + text.size());
    buf->append("\n");
}

}  // namespace

void ui_persist_install() {
    ImGuiSettingsHandler h;
    h.TypeName = kTypeName;
    h.TypeHash = ImHashStr(kTypeName);
    h.ReadOpenFn = read_open;
    h.ReadLineFn = read_line;
    h.WriteAllFn = write_all;
    ImGui::AddSettingsHandler(&h);
}

bool ui_persist_loaded() {
    const ImGuiContext* g = ImGui::GetCurrentContext();
    return g != nullptr && g->SettingsLoaded;
}

bool ui_window_open(Win w, bool def) {
    return g_store.window_open(window_spec(w).title, def);
}

void ui_set_window_open(Win w, bool open) {
    const char* key = window_spec(w).title;
    if (g_store.window_open(key, !open) == open) return;  // 안 바뀌었다
    g_store.set_window_open(key, open);
    ImGui::MarkIniSettingsDirty();
}

bool collapsing_header(const char* key, const char* label, bool force_open) {
    if (force_open) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    } else {
        ImGui::SetNextItemOpen(g_store.header_open(key), ImGuiCond_Once);
    }
    const bool open = ImGui::CollapsingHeader(label);
    if (g_store.header_open(key) != open) {
        g_store.set_header_open(key, open);
        ImGui::MarkIniSettingsDirty();
    }
    return open;
}

}  // namespace cdtb::render

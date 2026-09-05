#include "render/stash_panel.h"

#include <windows.h>

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/log.h"
#include "game/grant.h"
#include "game/items.h"
#include "game/stash.h"
#include "render/grant_panel.h"
#include "render/icon_atlas.h"
#include "render/item_style.h"

namespace cdtb::render {
namespace {

game::Stash g_stash;
bool g_loaded = false;
bool g_dirty = false;
int g_open_set = -1;
char g_new_name[64] = "";

// 일괄 지급은 쿨다운(2초) 때문에 한 번에 다 못 보낸다. 큐에 넣고
// 한 개씩 흘려보낸다.
std::vector<game::StashEntry> g_queue;
std::size_t g_queue_at = 0;

constexpr float kIconSize = 22.0f;

// DLL 옆에 둔다. 아이콘 아틀라스와 같은 자리다.
std::wstring stash_path() {
    HMODULE self = nullptr;
    if (!::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&stash_path), &self)) {
        return {};
    }
    wchar_t path[MAX_PATH]{};
    const DWORD n = ::GetModuleFileNameW(self, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::wstring s(path, n);
    const auto slash = s.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    return s.substr(0, slash + 1) + L"cdtoybox_stash.txt";
}

void load() {
    g_loaded = true;
    const std::wstring p = stash_path();
    if (p.empty()) return;
    HANDLE h = ::CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;      // 아직 없는 것은 정상이다
    LARGE_INTEGER size{};
    std::string text;
    if (::GetFileSizeEx(h, &size) && size.QuadPart > 0 &&
        size.QuadPart < (4 << 20)) {
        text.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD got = 0;
        if (!::ReadFile(h, text.data(), static_cast<DWORD>(text.size()), &got,
                        nullptr)) {
            text.clear();
        } else {
            text.resize(got);
        }
    }
    ::CloseHandle(h);
    if (!text.empty()) {
        g_stash.parse(text);
        log::infof("보관함 읽음: 즐겨찾기 {}개, 세트 {}개",
                   g_stash.favorites().size(), g_stash.set_count());
    }
}

void save() {
    const std::wstring p = stash_path();
    if (p.empty()) return;
    const std::string text = g_stash.serialize();
    HANDLE h = ::CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        log::warnf("보관함을 저장하지 못했다");
        return;
    }
    DWORD wrote = 0;
    ::WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &wrote,
                nullptr);
    ::CloseHandle(h);
    g_dirty = false;
}

const game::ItemCatalogEntry* find_item(std::uint32_t key) {
    if (!game::items_ready()) return nullptr;
    for (const auto& e : game::item_catalog()) {
        if (e.key == key) return &e;
    }
    return nullptr;
}

// 아이콘 + 이름을 한 줄로. 목록과 같은 색을 쓴다.
void draw_item_line(std::uint32_t key) {
    const IconRef ico = icon_for(key);
    if (ico.valid) {
        ImGui::Image(ico.tex, ImVec2(kIconSize, kIconSize), ico.uv0, ico.uv1);
    } else {
        ImGui::Dummy(ImVec2(kIconSize, kIconSize));
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    const game::ItemCatalogEntry* it = find_item(key);
    if (it == nullptr || it->name.empty()) {
        ImGui::TextDisabled("키 %u", key);
    } else {
        ImGui::TextColored(grade_color(it->grade), "%s", it->name.c_str());
    }
}

}  // namespace

namespace {

// 이 아이템이 한 칸에 몇 개까지 쌓이는가. 표에 없으면 0 - 그때는
// 개수를 고칠 근거가 없으므로 손대지 않는다.
std::uint32_t max_stack_of(std::uint32_t key) {
    if (!game::items_ready()) return 0;
    for (const auto& c : game::item_catalog()) {
        if (c.key == key) return c.max_stack;
    }
    return 0;
}

}  // namespace

int stash_open_set() {
    if (!g_loaded) load();
    return (g_open_set >= 0 && g_open_set < g_stash.set_count()) ? g_open_set
                                                                 : -1;
}

bool stash_add_entry(int set, const game::StashEntry& entry) {
    if (!g_loaded) load();
    if (set < 0 || set >= g_stash.set_count()) return false;
    game::StashSet* s = g_stash.set_at(set);
    if (s == nullptr) return false;
    s->items.push_back(entry);
    g_dirty = true;
    return true;
}

void stash_toggle_favorite(unsigned int key) {
    if (!g_loaded) load();
    g_stash.toggle_favorite(key);
    g_dirty = true;
}

bool stash_is_favorite(unsigned int key) {
    if (!g_loaded) load();
    return g_stash.is_favorite(key);
}

void draw_stash_panel(bool* open) {
    if (!g_loaded) load();

    // 큐가 남아 있으면 한 개씩 흘려보낸다. request_give 가 쿨다운에
    // 걸리면 false 를 주므로 다음 프레임에 다시 시도한다.
    if (g_queue_at < g_queue.size() && !game::spawn_pending()) {
        std::uintptr_t seen[16]{};
        std::uint32_t hits[16]{};
        const int n = game::seen_sessions(seen, hits, 16);
        bool server[16]{};
        for (int i = 0; i < n; ++i) server[i] = game::session_is_server(i);
        const int pick = game::best_actor_index(hits, server, n);
        if (pick >= 0) {
            const auto& e = g_queue[g_queue_at];
            // 담금질은 아이템마다 상한이 있고 넘으면 게임이 조용히
            // 거절한다 - 그러면 큐가 그 자리에서 영영 멈춘다. 파일에
            // 큰 값이 있으면 깎아서 보낸다.
            std::uint32_t cap = 0;
            std::uint32_t socket_cap = 0;
            for (const auto& c : game::item_catalog()) {
                if (c.key == e.key) {
                    cap = c.max_temper;
                    socket_cap = c.max_sockets;
                    break;
                }
            }

            game::GiveExtras extras;
            extras.temper =
                static_cast<std::uint16_t>(e.temper > cap ? cap : e.temper);
            // 안 채우면 내구도 0 짜리가 나온다. 이 게임은 아무
            // 아이템도 수리 데이터가 없어 되돌릴 수 없다.
            //
            // 파일에 적힌 값이 있으면 그것을 쓴다 - 닳은 상태까지
            // 그대로 되살린다. 없으면(옛 파일) 최대치다.
            // 장비 연마도 표의 상한으로 자른다.
            const std::int16_t sharp_cap = game::max_sharpness_for(e.key);
            extras.sharpness = static_cast<std::uint16_t>(
                (sharp_cap > 0 && e.sharpness >
                     static_cast<std::uint32_t>(sharp_cap))
                    ? sharp_cap
                    : e.sharpness);

            const std::uint16_t full = game::full_endurance_for(e.key);
            if (e.endurance == game::kStashNoEndurance) {
                extras.endurance = full;
            } else {
                extras.endurance = static_cast<std::uint16_t>(
                    e.endurance > full ? full : e.endurance);
            }

            // 소켓도 같다. 아이템 표의 칸 수를 넘기면 게임이 조용히
            // 거절한다. 배열 자체도 다섯 칸이다.
            std::size_t room = socket_cap;
            if (room > game::kGiveMaxSockets) room = game::kGiveMaxSockets;
            for (const auto& sk : e.sockets) {
                if (extras.socket_count >= room) break;
                auto& dst = extras.sockets[extras.socket_count];

                // 6바이트를 **보석 키에서 다시 조립한다.** 파일의
                // 원본 바이트를 그대로 쓰면 첫 u16(순번)이 그 파일을
                // 만든 빌드에 묶이고, 실측에서 여섯 번째 바이트가
                // 원본(FF)과 갈리는 일도 있었다. 지급 패널과 같은
                // 조립기를 쓰면 두 경로가 같은 바이트를 낸다.
                //
                // 대응표를 아직 못 읽었으면 파일의 바이트로 물러선다 -
                // 같은 빌드라면 그것이 맞는 값이다.
                if (!game::socket_bytes_for_key(sk.key, dst.raw)) {
                    std::memcpy(dst.raw, sk.raw, game::kGiveSocketBytes);
                }
                ++extras.socket_count;
            }

            if (game::request_give(seen[pick], e.key, e.count, extras)) {
                ++g_queue_at;
            }
        } else {
            g_queue.clear();     // 세션이 없으면 접는다
            g_queue_at = 0;
        }
    }

    ImGui::SetNextWindowPos(ImVec2(1180, 340), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 400.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("보관함", open)) {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("게임 밖에 두는 목록입니다. 슬롯 제한과 무관합니다.");
    if (g_queue_at < g_queue.size()) {
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f),
                           "지급 중 %zu / %zu (2초 간격)", g_queue_at,
                           g_queue.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("중단")) {
            g_queue.clear();
            g_queue_at = 0;
        }
    }
    ImGui::Separator();

    // --- 즐겨찾기 ---------------------------------------------------
    if (ImGui::CollapsingHeader("즐겨찾기", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto favs = g_stash.favorites();   // 지우면서 도니 복사한다
        if (favs.empty()) {
            ImGui::TextDisabled("아이템 목록에서 별표를 눌러 담으세요");
        }
        for (const auto key : favs) {
            ImGui::PushID(static_cast<int>(key));
            if (ImGui::SmallButton("빼기")) {
                g_stash.toggle_favorite(key);
                g_dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("지급")) {
                set_grant_item_key(key);
                g_queue.assign(1, game::StashEntry{key, 1});
                g_queue_at = 0;
            }
            ImGui::SameLine();
            draw_item_line(key);
            ImGui::PopID();
        }
    }

    // --- 세트 -------------------------------------------------------
    ImGui::Separator();
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputTextWithHint("##newset", "새 세트 이름", g_new_name,
                             sizeof(g_new_name));
    ImGui::SameLine();
    if (ImGui::Button("세트 만들기") && g_new_name[0] != 0) {
        g_open_set = g_stash.add_set(g_new_name);
        g_new_name[0] = 0;
        g_dirty = true;
    }

    for (int i = 0; i < g_stash.set_count(); ++i) {
        game::StashSet* set = g_stash.set_at(i);
        ImGui::PushID(1000 + i);
        char label[96];
        std::snprintf(label, sizeof(label), "%s (%zu개)", set->name.c_str(),
                      set->items.size());
        if (ImGui::CollapsingHeader(label)) {
            // 인벤토리 창이 "어디에 담을지" 를 이걸로 안다.
            g_open_set = i;
            if (ImGui::SmallButton("전부 지급")) {
                g_queue = set->items;
                g_queue_at = 0;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("세트 지우기")) {
                g_stash.remove_set(i);
                g_dirty = true;
                ImGui::PopID();
                break;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("지급 칸의 아이템을 담으려면 ↓");
            if (ImGui::SmallButton("지금 고른 아이템 담기")) {
                const unsigned int k = grant_item_key();
                if (k != 0) {
                    // 키만 담으면 꺼낼 때 맨 아이템이 나온다. 지급
                    // 칸에서 고른 담금질과 소켓도 함께 담는다.
                    game::StashEntry e{k, grant_item_count()};
                    e.temper = grant_temper();
                    e.sharpness = grant_sharpness();
                    unsigned int gems[game::kGiveMaxSockets]{};
                    const int gn =
                        grant_socket_keys(gems, game::kGiveMaxSockets);
                    for (int gi = 0; gi < gn; ++gi) {
                        game::StashSocket ss;
                        ss.slot = static_cast<std::uint32_t>(gi);
                        ss.key = gems[gi];
                        // 파일에 남길 원본 바이트도 같은 조립기로
                        // 만든다. 대응표가 없으면 키만 남는다 -
                        // 꺼낼 때 다시 조립하므로 그래도 된다.
                        game::socket_bytes_for_key(ss.key, ss.raw);
                        e.sockets.push_back(ss);
                    }
                    set->items.push_back(std::move(e));
                    g_dirty = true;
                }
            }
            for (std::size_t j = 0; j < set->items.size(); ++j) {
                ImGui::PushID(static_cast<int>(j));
                if (ImGui::SmallButton("빼기")) {
                    set->items.erase(set->items.begin() +
                                     static_cast<std::ptrdiff_t>(j));
                    g_dirty = true;
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                // 한 줄만 꺼낸다. 큐에 하나만 넣으면 "전부 지급" 과
                // 같은 길을 타므로 상한 자르기 · 소켓 조립이 그대로다.
                if (ImGui::SmallButton("지급")) {
                    g_queue.assign(1, set->items[j]);
                    g_queue_at = 0;
                }
                ImGui::SameLine();
                draw_item_line(set->items[j].key);
                ImGui::SameLine();

                // 겹쳐 쌓이는 아이템은 개수를 고칠 수 있어야 한다.
                // 지금까지는 담을 때의 값이 그대로 굳어 있었다.
                // 겹치지 않는 것(장비)은 1 뿐이라 글자로만 낸다.
                auto& item = set->items[j];
                const std::uint32_t cap = max_stack_of(item.key);
                if (cap > 1) {
                    int n = (item.count > 0x7FFFFFFF)
                                ? 0x7FFFFFFF
                                : static_cast<int>(item.count);
                    ImGui::SetNextItemWidth(110.0f);
                    if (ImGui::InputInt("##cnt", &n, 1, 10)) {
                        n = game::clamp_count_to_stack(n, cap);
                        if (n != item.count) {
                            item.count = n;
                            g_dirty = true;
                        }
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("/ %u", cap);
                } else {
                    ImGui::TextDisabled("x%lld",
                                        static_cast<long long>(item.count));
                }
                ImGui::PopID();
            }
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    if (g_dirty) {
        if (ImGui::Button("저장")) save();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "저장하지 않은 변경");
    } else {
        ImGui::TextDisabled("cdtoybox_stash.txt 에 저장됩니다");
    }
    // 파일을 손으로 고친 뒤 게임을 다시 켜지 않고 반영한다. 저장하지
    // 않은 변경은 버려진다 - 파일이 진실이다.
    ImGui::SameLine();
    if (ImGui::SmallButton("다시 읽기")) {
        g_stash = game::Stash{};
        g_open_set = -1;
        g_dirty = false;
        load();
    }
    ImGui::End();
}

}  // namespace cdtb::render

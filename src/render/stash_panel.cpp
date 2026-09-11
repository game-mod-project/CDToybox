#include "render/stash_panel.h"

#include <windows.h>

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "core/log.h"
#include "game/grant.h"
#include "game/items.h"
#include "game/stash.h"
#include "render/colors.h"
#include "render/confirm.h"
#include "render/gates.h"
#include "render/grant_panel.h"
#include "render/icon_atlas.h"
#include "render/item_style.h"
#include "render/layout.h"
#include "render/notice.h"
#include "render/stash_queue.h"

namespace cdtb::render {
namespace {

game::Stash g_stash;
bool g_loaded = false;
// 마지막 변경 시각(초). 음수면 저장할 것이 없다. 1초 뒤 stash_tick 이 저장한다 -
// 저장 버튼은 없다(★ 을 누르고 창을 안 열면 조용히 유실되던 것).
double g_dirty_at = -1.0;
char g_saved_clock[16] = "";        // 마지막 저장 HH:MM:SS. 비면 아직 없음
// 펼쳐 둔 세트. 번호로 들면 세트를 지운 뒤 엉뚱한 세트에 담긴다 - 이름으로 든다.
std::string g_open_set_name;
std::string g_open_new_set;         // 방금 만든 세트 - 다음 프레임에 머리글을 펼친다
std::uint64_t g_sets_generation = 0;   // 세트 추가·삭제·읽기마다 1 오른다(stash_open_set 캐시 키)
bool g_drawn_this_frame = false;    // draw_stash_panel 이 본문을 그렸는가
bool g_drawn_last_frame = false;    // 지난 프레임의 그 값 - 본창이 대신 그릴지 정한다
char g_new_name[64] = "";
Notice g_notice;

// 일괄 지급은 쿨다운(2초) 때문에 한 번에 다 못 보낸다. 큐에 넣고
// 한 개씩 흘려보낸다.
StashQueue g_queue;
std::size_t g_queue_total = 0;   // 시작할 때의 개수. 진행 줄·완료 문구에 쓴다

constexpr float kIconSize = 22.0f;

// 보관함의 시계. ImGui::GetTime() 은 NewFrame 안에서만 흐르므로 오버레이를 숨기면
// 멈춘다 - 숨긴 채로도 저장·큐가 돌아야 하니 단조 시계(GetTickCount64, 리셋 없음)를
// 쓴다. g_dirty_at·큐의 next_at·stash_tick 의 now 는 전부 이 시계다. 알림 시각만
// ImGui 시계다(notice_draw 가 그 시계로 나이를 잰다 - 숨긴 동안 찍힌 알림은 다시
// 켜는 순간부터 나이를 먹는다).
double stash_clock() { return static_cast<double>(::GetTickCount64()) / 1000.0; }
void mark_dirty() { g_dirty_at = stash_clock(); }

void stamp_saved_clock() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    if (localtime_s(&tm, &t) == 0) {
        std::snprintf(g_saved_clock, sizeof(g_saved_clock), "%02d:%02d:%02d",
                      tm.tm_hour, tm.tm_min, tm.tm_sec);
    }
}

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
        // 옛 빌드·손 편집 파일의 같은 이름 세트는 담기 목적지(find_set)를 엉뚱하게
        // 푼다 - 읽을 때 뒤엣것의 이름을 바꾸고 파일에도 되쓴다(최종 리뷰 I-1).
        const int renamed = g_stash.dedupe_set_names();
        if (renamed > 0) {
            log::warnf("보관함: 이름이 겹치는 세트 {}개의 이름을 \" (2)\" 꼴로 바꿨다",
                       renamed);
            mark_dirty();
        }
        ++g_sets_generation;
        log::infof("보관함 읽음: 즐겨찾기 {}개, 세트 {}개",
                   g_stash.favorites().size(), g_stash.set_count());
    }
}

bool save() {
    const std::wstring p = stash_path();
    if (p.empty()) {
        // 다른 실패 갈래처럼 알리고 10초 뒤에 다시 본다 - 매 프레임 재시도하지 않게.
        log::warnf("보관함을 저장하지 못했다 (DLL 경로를 못 얻었다)");
        notice_set(&g_notice, NoticeLevel::Bad, "저장 실패 - 파일이 잠겼는지 보십시오");
        g_dirty_at = stash_clock() + 9.0;
        return false;
    }
    const std::string text = g_stash.serialize();
    HANDLE h = ::CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        log::warnf("보관함을 저장하지 못했다 (열기 실패 {})", ::GetLastError());
        notice_set(&g_notice, NoticeLevel::Bad, "저장 실패 - 파일이 잠겼는지 보십시오");
        // 1초마다 다시 실패하며 알림을 새로 찍지 않게 10초 뒤에 다시 본다.
        g_dirty_at = stash_clock() + 9.0;
        return false;
    }
    DWORD wrote = 0;
    const BOOL ok = ::WriteFile(h, text.data(), static_cast<DWORD>(text.size()),
                                &wrote, nullptr);
    ::CloseHandle(h);
    if (!ok || wrote != text.size()) {
        log::warnf("보관함을 저장하지 못했다 (쓰기 {}/{} 바이트)", wrote, text.size());
        notice_set(&g_notice, NoticeLevel::Bad, "저장 실패 - 파일이 잠겼는지 보십시오");
        g_dirty_at = stash_clock() + 9.0;
        return false;
    }
    g_dirty_at = -1.0;
    stamp_saved_clock();
    log::infof("보관함 저장: 즐겨찾기 {}개, 세트 {}개", g_stash.favorites().size(),
               g_stash.set_count());
    return true;
}

// 한 항목의 지급 인자. 상한 자르기·소켓 조립은 예전 큐 코드 그대로다.
//
// 담금질·연마·내구도·소켓은 아이템마다 상한이 있고 넘으면 게임이 조용히
// 거절한다 - 그러면 큐가 그 자리에서 영영 멈춘다. 파일에 큰 값이 있으면
// 깎아서 보낸다. 내구도를 안 채우면 0 짜리가 나오고, 이 게임은 아무
// 아이템도 수리 데이터가 없어 되돌릴 수 없다. 소켓 6바이트는 파일의 원본
// 바이트가 아니라 보석 키에서 다시 조립한다 - 원본 바이트를 그대로 쓰면
// 첫 u16(순번)이 그 파일을 만든 빌드에 묶인다. 대응표를 아직 못 읽었으면
// 파일의 바이트로 물러선다.
game::GiveExtras extras_for(const game::StashEntry& e) {
    std::uint32_t cap = 0;
    if (const auto* c = game::item_by_key(e.key)) cap = c->max_temper;
    const std::uint32_t socket_cap = game::socket_room_for(e.key);

    game::GiveExtras extras;
    extras.temper = static_cast<std::uint16_t>(e.temper > cap ? cap : e.temper);
    const std::int16_t sharp_cap = game::max_sharpness_for(e.key);
    extras.sharpness = static_cast<std::uint16_t>(
        (sharp_cap > 0 && e.sharpness > static_cast<std::uint32_t>(sharp_cap))
            ? sharp_cap
            : e.sharpness);
    const std::uint16_t full = game::full_endurance_for(e.key);
    if (e.endurance == game::kStashNoEndurance) {
        extras.endurance = full;
    } else {
        extras.endurance =
            static_cast<std::uint16_t>(e.endurance > full ? full : e.endurance);
    }
    std::size_t room = socket_cap;
    if (room > game::kGiveMaxSockets) room = game::kGiveMaxSockets;
    for (const auto& sk : e.sockets) {
        if (extras.socket_count >= room) break;
        auto& dst = extras.sockets[extras.socket_count];
        if (!game::socket_bytes_for_key(sk.key, dst.raw)) {
            std::memcpy(dst.raw, sk.raw, game::kGiveSocketBytes);
        }
        ++extras.socket_count;
    }
    return extras;
}

// 큐를 한 발 진행한다. 창이 닫혀 있어도 stash_tick 이 부른다.
void run_queue(double now) {
    if (g_queue.items.empty()) return;
    // 앞 요청이 아직 게임 스레드에 있으면 이번 프레임은 쉰다.
    if (game::spawn_pending(game::DriveLane::Item)) return;
    // 지급 세션은 grant.cpp 가 한 규칙으로 고른다 - 서버 + 게이트. 여기서만
    // 호출 최다로 골랐더니, 인플레이스 로드 뒤 게이트가 닫힌 죽은 세션을
    // 그대로 잡았다(실측 2026-09-10). 그 세션으로 구동하면 게임 안에서
    // 0xC0000005 로 죽는다.
    const mem::LocalReader rd;
    const std::uintptr_t session = game::pick_drive_session(rd);
    switch (stash_queue_step(&g_queue, session != 0, now)) {
        case QueueStep::Idle:
        case QueueStep::Wait:
            return;
        case QueueStep::NoSession: {
            if (g_queue.no_session_noted) return;
            g_queue.no_session_noted = true;
            const std::size_t left = stash_queue_remaining(g_queue);
            notice_set(&g_notice, NoticeLevel::Warn,
                       "세션이 없어 {}개 남았습니다 - 월드에 들어가면 이어집니다", left);
            log::warnf("보관함 지급: 세션이 없어 {}개 남김 - 월드 진입 뒤 이어간다", left);
            return;
        }
        case QueueStep::Done:
            notice_set(&g_notice, NoticeLevel::Ok, "{}개 지급했습니다", g_queue_total);
            log::infof("보관함 지급 완료: {}개", g_queue_total);
            return;
        case QueueStep::Send:
            break;
    }
    g_queue.no_session_noted = false;
    const game::StashEntry& e = g_queue.items[g_queue.at];
    if (game::request_give(session, e.key, e.count, extras_for(e))) {
        stash_queue_sent(&g_queue, now);
    }
    // false 면 레인 쿨다운 - 다음 프레임에 같은 항목을 다시 시도한다.
}

void queue_start(std::vector<game::StashEntry> items, const char* what) {
    if (items.empty()) {
        notice_set(&g_notice, NoticeLevel::Info, "세트가 비어 있습니다");
        return;
    }
    // 도는 큐를 새 지급으로 갈아 끼우면 남은 것이 조용히 사라진다 - 알리고 남긴다
    // (최종 리뷰 I-2).
    const std::size_t left = stash_queue_remaining(g_queue);
    if (left > 0) {
        log::infof("보관함 지급 중단: {}개 남김 (새 지급으로 교체)", left);
        notice_set(&g_notice, NoticeLevel::Warn, "앞의 지급 {}개를 접고 새로 시작합니다",
                   left);
    }
    stash_queue_clear(&g_queue);
    g_queue.items = std::move(items);
    g_queue_total = g_queue.items.size();
    log::infof("보관함 지급 시작: {} {}개", what, g_queue_total);
}

const game::ItemCatalogEntry* find_item(std::uint32_t key) {
    return game::item_by_key(key);
}

// 이름이 max_w 를 넘으면 글자(UTF-8) 경계에서 잘라 "…" 을 붙이고, 전체는 툴팁으로.
void draw_name_clipped(const char* name, ImVec4 color, float max_w) {
    const float w = ImGui::CalcTextSize(name).x;
    if (w <= max_w) {
        ImGui::TextColored(color, "%s", name);
        return;
    }
    std::string cut(name);
    while (!cut.empty() &&
           ImGui::CalcTextSize((cut + "…").c_str()).x > max_w) {
        // 글자 하나를 뗀다: 뒤의 연속 바이트(10xxxxxx)를 다 걷어내고 선행 바이트
        // 하나. 선행 바이트를 남기면 깨진 글자가 남아 디코더가 "…" 까지 먹는다
        // (Task 4 리뷰 D1).
        while (!cut.empty() &&
               (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80) {
            cut.pop_back();
        }
        if (!cut.empty()) cut.pop_back();
    }
    ImGui::TextColored(color, "%s…", cut.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", name);
}

// 아이콘 + 이름을 한 줄로. 목록과 같은 색을 쓴다. line_w 는 이 줄이 쓸 수 있는
// 폭(아이콘 포함) - 이름은 아이콘과 간격을 뺀 나머지에 맞춰 자른다.
void draw_item_line(std::uint32_t key, float line_w) {
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
        const float text_w =
            line_w - (kIconSize + ImGui::GetStyle().ItemSpacing.x);
        draw_name_clipped(it->name.c_str(), grade_color(it->grade),
                          text_w > 0.0f ? text_w : 0.0f);
    }
}

}  // namespace

namespace {

// 이 아이템이 한 칸에 몇 개까지 쌓이는가. 표에 없으면 0 - 그때는
// 개수를 고칠 근거가 없으므로 손대지 않는다.
std::uint32_t max_stack_of(std::uint32_t key) {
    const game::ItemCatalogEntry* c = game::item_by_key(key);
    return c != nullptr ? c->max_stack : 0;
}

}  // namespace

int stash_open_set() {
    if (!g_loaded) load();
    // 인벤 표가 행마다 부르므로 이름 탐색은 세트 판·이름이 바뀔 때만 한다(최종 리뷰 M-9).
    static std::uint64_t cached_gen = ~0ULL;
    static std::string cached_name;
    static int cached = -1;
    if (cached_gen != g_sets_generation || cached_name != g_open_set_name) {
        cached_gen = g_sets_generation;
        cached_name = g_open_set_name;
        cached = g_stash.find_set(g_open_set_name);
    }
    return cached;
}

const char* stash_open_set_name() {
    const int i = stash_open_set();
    if (i < 0) return "";
    const game::StashSet* s = g_stash.set_at(i);
    return s != nullptr ? s->name.c_str() : "";
}

bool stash_add_entry(int set, const game::StashEntry& entry) {
    if (!g_loaded) load();
    if (set < 0 || set >= g_stash.set_count()) return false;
    game::StashSet* s = g_stash.set_at(set);
    if (s == nullptr) return false;
    s->items.push_back(entry);
    mark_dirty();
    return true;
}

void stash_toggle_favorite(unsigned int key) {
    if (!g_loaded) load();
    g_stash.toggle_favorite(key);
    log::infof("보관함: 즐겨찾기 {} {}", key, g_stash.is_favorite(key) ? "추가" : "제거");
    mark_dirty();
}

bool stash_is_favorite(unsigned int key) {
    if (!g_loaded) load();
    return g_stash.is_favorite(key);
}

void stash_tick() {
    if (!g_loaded) load();
    const double now = stash_clock();
    if (stash_autosave_due(g_dirty_at, now)) save();
    run_queue(now);
    // 지난 프레임에 창 본문을 안 그렸으면 펼쳐 둔 세트를 잊는다 - 닫힌 창의
    // 세트에 인벤 '보관' 이 담기지 않게. 한 프레임 늦는 것은 무해하다.
    if (!g_drawn_this_frame) g_open_set_name.clear();
    g_drawn_last_frame = g_drawn_this_frame;
    g_drawn_this_frame = false;
}

bool stash_body_visible() { return g_drawn_last_frame; }

void stash_flush() {
    if (g_loaded && g_dirty_at >= 0.0) save();
}

const Notice& stash_notice() { return g_notice; }

bool stash_queue_progress(std::size_t* done, std::size_t* total) {
    if (g_queue.items.empty()) return false;
    if (done != nullptr) *done = g_queue.at;
    if (total != nullptr) *total = g_queue.items.size();
    return true;
}

void stash_queue_cancel() {
    const std::size_t left = stash_queue_remaining(g_queue);
    stash_queue_clear(&g_queue);
    notice_set(&g_notice, NoticeLevel::Info, "중단했습니다 ({}개 남김)", left);
    log::infof("보관함 지급 중단: {}개 남김", left);
}

void draw_stash_panel(bool* open) {
    if (!g_loaded) load();

    if (!begin_window(Win::Stash, open)) {
        ImGui::End();
        return;
    }
    g_drawn_this_frame = true;

    ImGui::TextDisabled("게임 밖에 두는 목록입니다. 슬롯 제한과 무관합니다.");
    notice_draw(g_notice);
    std::size_t q_done = 0, q_total = 0;
    if (stash_queue_progress(&q_done, &q_total)) {
        ImGui::TextColored(col::kBusy, "지급 중 %zu / %zu (2초 간격)", q_done, q_total);
        ImGui::SameLine();
        if (ImGui::SmallButton("중단")) stash_queue_cancel();
    }

    // 이름이 풀리기 전엔 항목이 전부 "키 12345" 로 나온다 - 다른 창처럼 알린다.
    if (!loading_gate(game::items_named(), "아이템 이름", game::items_named_count(),
                      game::items_total_count())) {
        ImGui::End();
        return;
    }
    ImGui::Separator();

    // --- 즐겨찾기 ---------------------------------------------------
    const auto favs = g_stash.favorites();   // 지우면서 도니 복사한다
    char fav_hdr[48];
    std::snprintf(fav_hdr, sizeof(fav_hdr), "즐겨찾기 (%zu)###favs", favs.size());
    if (ImGui::CollapsingHeader(fav_hdr, ImGuiTreeNodeFlags_DefaultOpen)) {
        // 즐겨찾기가 많아도 세트를 스크롤 밖으로 밀어내지 않게 창 높이의 45% 까지만.
        const float row_h = kIconSize + ImGui::GetStyle().ItemSpacing.y;
        // 테두리 없는 자식은 안쪽 여백이 0 이라 줄 높이만 센다.
        const float want = favs.empty() ? ImGui::GetTextLineHeightWithSpacing()
                                        : row_h * static_cast<float>(favs.size());
        const float cap = ImGui::GetWindowHeight() * 0.45f;
        ImGui::BeginChild("favs_body", ImVec2(0, want < cap ? want : cap), ImGuiChildFlags_None,
                          ImGuiWindowFlags_None);
        if (favs.empty()) {
            ImGui::TextDisabled("아이템 목록에서 별표를 누르면 여기에 담깁니다");
        }
        for (const auto key : favs) {
            ImGui::PushID(static_cast<int>(key));
            if (ImGui::SmallButton("빼기")) {
                g_stash.toggle_favorite(key);
                log::infof("보관함: 즐겨찾기 {} 제거", key);
                mark_dirty();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("지급")) {
                set_grant_item_key(key);
                queue_start({game::StashEntry{key, 1}}, "즐겨찾기");
            }
            ImGui::SameLine();
            draw_item_line(key, ImGui::GetContentRegionAvail().x);
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    // --- 세트 -------------------------------------------------------
    ImGui::Separator();
    ImGui::Text("세트 (%d)", g_stash.set_count());
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputTextWithHint("##newset", "새 세트 이름", g_new_name,
                             sizeof(g_new_name));
    ImGui::SameLine();
    ImGui::BeginDisabled(g_new_name[0] == 0);
    if (ImGui::Button("세트 만들기")) {
        // 펼쳐 둔 세트를 이름으로 찾으므로 같은 이름이 둘이면 위의 것만 잡힌다 -
        // 만들 때 막는다(Task 2 리뷰).
        if (g_stash.find_set(g_new_name) >= 0) {
            notice_set(&g_notice, NoticeLevel::Warn,
                       "같은 이름의 세트가 이미 있습니다: {}", g_new_name);
        } else {
            g_stash.add_set(g_new_name);
            ++g_sets_generation;
            g_open_new_set = g_new_name;   // 다음 프레임에 머리글을 펼쳐 목적지가 된다
            log::infof("보관함: 세트 '{}' 만듦", g_new_name);
            g_new_name[0] = 0;
            mark_dirty();
        }
    }
    ImGui::EndDisabled();

    // 펼쳐진 세트가 담기 목적지다. 머리글을 접으면 목적지도 풀린다(최종 리뷰 M-4).
    std::string open_name;
    for (int i = 0; i < g_stash.set_count(); ++i) {
        game::StashSet* set = g_stash.set_at(i);
        ImGui::PushID(1000 + i);
        // ID 는 이름으로 고정한다 - "(N개)" 가 바뀔 때 머리글이 접히지 않게.
        char label[256];
        std::snprintf(label, sizeof(label), "%s (%zu개)###set:%s", set->name.c_str(),
                      set->items.size(), set->name.c_str());
        if (!g_open_new_set.empty() && g_open_new_set == set->name) {
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);   // 방금 만든 세트를 펼친다
            g_open_new_set.clear();
        }
        if (ImGui::CollapsingHeader(label)) {
            // 인벤토리 창이 "어디에 담을지" 를 이걸로 안다.
            open_name = set->name;
            if (ImGui::SmallButton("전부 지급")) {
                queue_start(set->items, set->name.c_str());
            }
            ImGui::SameLine();
            if (confirm_small_button("세트 지우기", false)) {
                log::infof("보관함: 세트 '{}' 지움 ({}개 항목)", set->name, set->items.size());
                g_stash.remove_set(i);
                ++g_sets_generation;
                mark_dirty();
                ImGui::PopID();
                break;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("지급 칸의 아이템을 담으려면 ↓");
            if (ImGui::SmallButton("지금 고른 아이템 담기")) {
                const unsigned int k = grant_item_key();
                if (k != 0) {
                    // 키만 담으면 꺼낼 때 맨 아이템이 나온다. 지급
                    // 칸에서 고른 담금질도 함께 담는다. 소켓은 지급
                    // 경로로 못 넣으므로 담지 않는다(인벤토리에서
                    // "보관" 으로 담으면 원본 소켓은 기록된다).
                    game::StashEntry e{k, grant_item_count()};
                    e.temper = grant_temper();
                    e.sharpness = grant_sharpness();
                    set->items.push_back(std::move(e));
                    mark_dirty();
                }
            }
            for (std::size_t j = 0; j < set->items.size(); ++j) {
                ImGui::PushID(static_cast<int>(j));
                if (ImGui::SmallButton("빼기")) {
                    set->items.erase(set->items.begin() +
                                     static_cast<std::ptrdiff_t>(j));
                    mark_dirty();
                    ImGui::PopID();
                    break;
                }
                ImGui::SameLine();
                // 한 줄만 꺼낸다. 큐에 하나만 넣으면 "전부 지급" 과
                // 같은 길을 타므로 상한 자르기 · 소켓 조립이 그대로다.
                if (ImGui::SmallButton("지급")) {
                    set_grant_item_key(set->items[j].key);
                    queue_start({set->items[j]}, "세트 항목");
                }
                ImGui::SameLine();
                draw_item_line(set->items[j].key,
                               ImGui::GetContentRegionAvail().x - 150.0f);
                // 오른쪽 끝에서 140px. GetWindowContentRegionMax 는 폐기 예정이라
                // 커서 기준으로 잰다.
                ImGui::SameLine(ImGui::GetCursorPosX() +
                                ImGui::GetContentRegionAvail().x - 140.0f);

                // 겹쳐 쌓이는 아이템은 개수를 고칠 수 있어야 한다.
                // 지금까지는 담을 때의 값이 그대로 굳어 있었다.
                // 겹치지 않는 것(장비)은 1 뿐이라 글자로만 낸다.
                auto& item = set->items[j];
                const std::uint32_t cap = max_stack_of(item.key);
                if (cap > 1) {
                    int n = (item.count > 0x7FFFFFFF)
                                ? 0x7FFFFFFF
                                : static_cast<int>(item.count);
                    ImGui::SetNextItemWidth(90.0f);
                    if (ImGui::InputInt("##cnt", &n, 1, 10)) {
                        n = game::clamp_count_to_stack(n, cap);
                        if (n != item.count) {
                            item.count = n;
                            mark_dirty();
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
    g_open_set_name = open_name;

    ImGui::Separator();
    ImGui::TextDisabled("cdtoybox_stash.txt 에 자동 저장 · 마지막 %s",
                        g_saved_clock[0] != 0 ? g_saved_clock : "없음");
    if (g_dirty_at >= 0.0) {
        ImGui::SameLine();
        ImGui::TextColored(col::kBusy, "(저장 대기)");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("다시 읽기")) {
        g_stash = game::Stash{};
        g_open_set_name.clear();
        g_open_new_set.clear();
        g_dirty_at = -1.0;
        load();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("파일로 되돌립니다 (아직 저장되지 않은 1초 안의 변경은 버립니다)");
    }
    ImGui::End();
}

}  // namespace cdtb::render

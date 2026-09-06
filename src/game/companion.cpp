#include "game/companion.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/log.h"
#include "game/grant.h"
#include "mem/hook.h"

namespace {
// 이 코드가 든 모듈(DLL 또는 exe)의 디렉터리. dllmain 의 self_directory 와
// 같지만 테스트·프로브도 링크할 수 있게 여기 둔다.
std::wstring module_directory() {
    HMODULE self = nullptr;
    ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(&module_directory), &self);
    wchar_t buf[MAX_PATH]{};
    const DWORD n = ::GetModuleFileNameW(self, buf, MAX_PATH);
    std::wstring s(buf, n);
    const std::size_t slash = s.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : s.substr(0, slash + 1);
}
}  // namespace

namespace cdtb::game {

// --------------------------------------------------- 순수 함수

bool decode_message_header(const std::uint8_t* payload, std::size_t len,
                           std::uint16_t* id_out, std::uint16_t* body_len_out) {
    if (payload == nullptr || len < 5) return false;
    std::uint16_t id = 0, body = 0;
    std::memcpy(&id, payload + 0, sizeof(id));
    std::memcpy(&body, payload + 3, sizeof(body));
    if (id_out) *id_out = id;
    if (body_len_out) *body_len_out = body;
    return true;
}

bool decode_hire_to_target(const std::uint8_t* payload, std::size_t len,
                           std::uint32_t* handle_out, std::uint8_t* flag_out) {
    std::uint16_t id = 0, body = 0;
    if (!decode_message_header(payload, len, &id, &body)) return false;
    if (body != 5 || len < 5 + 5) return false;
    std::uint32_t handle = 0;
    std::memcpy(&handle, payload + 5, sizeof(handle));
    if (handle_out) *handle_out = handle;
    if (flag_out) *flag_out = payload[9];
    return true;
}

std::string hex_bytes(const std::uint8_t* p, std::size_t n, std::size_t cap) {
    static const char* kDigits = "0123456789ABCDEF";
    std::string s;
    const std::size_t m = (n < cap) ? n : cap;
    s.reserve(m * 3 + 4);
    for (std::size_t i = 0; i < m; ++i) {
        if (i) s.push_back(' ');
        s.push_back(kDigits[(p[i] >> 4) & 0xF]);
        s.push_back(kDigits[p[i] & 0xF]);
    }
    if (n > cap) s += " \xE2\x80\xA6";  // …
    return s;
}

bool build_use_item_wire(std::uint32_t item_key, std::uint32_t b, std::uint8_t c,
                         std::uint32_t d, std::uint8_t* out, std::size_t cap,
                         std::size_t* len_out) {
    if (out == nullptr || cap < kUseItemWireLen) return false;
    const std::uint16_t id = kUseItemByInfoId;
    const std::uint16_t body = 13;
    std::memcpy(out + 0, &id, 2);
    out[2] = 0;
    std::memcpy(out + 3, &body, 2);
    std::memcpy(out + 5, &item_key, 4);
    std::memcpy(out + 9, &b, 4);
    out[13] = c;
    std::memcpy(out + 14, &d, 4);
    if (len_out) *len_out = kUseItemWireLen;
    return true;
}

// --------------------------------------------------- 캡처 훅

namespace {

using DeserFn = void*(__fastcall*)(void*, void*, void*, void*);

constexpr std::size_t kPacketLen = 0x10;      // u16 전체길이
constexpr std::size_t kPacketPayload = 0x18;  // 페이로드 포인터
constexpr int kMaxDumps = 80;
constexpr std::size_t kHexCap = 768;

std::atomic<int> g_dumps{0};
std::atomic<bool> g_installed{false};
std::mutex g_last_mutex;
HireTargetCapture g_last_hire;

void dump_payload(void* packet, const char* tag) {
    if (packet == nullptr) return;
    if (g_dumps.load(std::memory_order_relaxed) >= kMaxDumps) return;
    auto* p = reinterpret_cast<const std::uint8_t*>(packet);
    std::uint16_t len = 0;
    std::uint64_t payload = 0;
    std::memcpy(&len, p + kPacketLen, sizeof(len));
    std::memcpy(&payload, p + kPacketPayload, sizeof(payload));
    if (payload == 0 || len == 0 || len > 8192) return;
    g_dumps.fetch_add(1, std::memory_order_relaxed);
    auto* pl = reinterpret_cast<const std::uint8_t*>(payload);
    std::uint16_t id = 0, body = 0;
    decode_message_header(pl, len, &id, &body);
    log::infof("동반자 캡처 [{}] ID {} 본문 {}바이트 (전체 {}): {}", tag, id,
               body, len, hex_bytes(pl, len, kHexCap));
    if (id == kHireToTargetId) {
        std::uint32_t handle = 0;
        std::uint8_t flag = 0;
        if (decode_hire_to_target(pl, len, &handle, &flag)) {
            std::lock_guard<std::mutex> lock(g_last_mutex);
            g_last_hire.valid = true;
            g_last_hire.handle = handle;
            g_last_hire.flag = flag;
            log::infof("획득 대상: 액터 핸들 0x{:08X} 플래그 {}", handle, flag);
        } else {
            log::warnf("획득 대상: 본문이 5바이트가 아니다 ({}). 정적 분석과 다름",
                       body);
        }
    }
}

// 메시지마다 detour·원본이 따로 있어야 해서 매크로로 찍어 낸다.
#define CDTB_COMP_DETOUR(id, tag)                                            \
    DeserFn g_orig_##id = nullptr;                                          \
    void* __fastcall det_##id(void* a, void* b, void* p, void* d) {         \
        dump_payload(p, tag);                                                \
        return g_orig_##id(a, b, p, d);                                      \
    }
CDTB_COMP_DETOUR(hire_target, "획득/대상")
CDTB_COMP_DETOUR(hire_item, "획득/아이템")
CDTB_COMP_DETOUR(catch_summon, "붙잡기")
CDTB_COMP_DETOUR(regist_event, "등록이벤트")
CDTB_COMP_DETOUR(select_spawn, "스폰선택")
CDTB_COMP_DETOUR(call_quick, "부르기")
CDTB_COMP_DETOUR(data_list, "소유목록")
CDTB_COMP_DETOUR(after_regist, "등록후소환")
CDTB_COMP_DETOUR(hire_response, "획득응답")
CDTB_COMP_DETOUR(use_item, "아이템사용")
CDTB_COMP_DETOUR(use_item_info, "아이템사용/정보")
#undef CDTB_COMP_DETOUR

// 클래스 이름 -> 서술자 vtable[2] (역직렬화). 부분일치가 여럿이면
// 정확히 그 이름인 것만 고른다.
bool resolve_deser(const mem::Rtti& rtti, const mem::Reader& reader,
                   const char* cls, std::uintptr_t* deser_out) {
    const std::string want = std::string(".?AV") + cls + "@pa@@";
    std::uintptr_t descriptor = 0;
    for (const auto& t : rtti.find_types(cls, 8)) {
        if (t.name == want) {
            descriptor = t.descriptor;
            break;
        }
    }
    if (descriptor == 0) return false;
    const auto vts = rtti.vtables_for(descriptor);
    if (vts.empty()) return false;
    std::uintptr_t deser = 0;
    if (!reader.read(vts[0] + 0x10, &deser, sizeof(deser)) || deser == 0) {
        return false;
    }
    *deser_out = deser;
    return true;
}

bool hook_one(const mem::Rtti& rtti, const mem::Reader& reader, const char* cls,
              void* detour, void** orig, const char* tag) {
    std::uintptr_t deser = 0;
    if (!resolve_deser(rtti, reader, cls, &deser)) {
        log::warnf("동반자 캡처: {} 역직렬화를 못 찾음", cls);
        return false;
    }
    if (!mem::hook_install(reinterpret_cast<void*>(deser), detour, orig)) {
        log::warnf("동반자 캡처: {} 후킹 실패 (RVA 0x{:X})", tag,
                   deser - reader.module_base());
        return false;
    }
    log::infof("동반자 캡처: {} 후킹 (RVA 0x{:X})", tag,
               deser - reader.module_base());
    return true;
}

}  // namespace

HireTargetCapture last_hire_target() {
    std::lock_guard<std::mutex> lock(g_last_mutex);
    return g_last_hire;
}

int companion_capture_count() { return g_dumps.load(std::memory_order_relaxed); }

bool companion_capture_installed() {
    return g_installed.load(std::memory_order_acquire);
}

bool companion_capture_install(const mem::Rtti& rtti,
                               const mem::Reader& reader) {
    if (g_installed.load(std::memory_order_acquire)) return true;
    if (!mem::hook_init()) return false;

    int n = 0;
#define CDTB_HOOK(cls, id, tag)                                              \
    n += hook_one(rtti, reader, cls, &det_##id,                             \
                  reinterpret_cast<void**>(&g_orig_##id), tag)
    CDTB_HOOK("TrocTrHireMercenaryToTargetReq", hire_target, "획득/대상");
    CDTB_HOOK("TrocTrHireMercenaryFromInventoryReq", hire_item, "획득/아이템");
    CDTB_HOOK("TrocTrCatchBySummonReq", catch_summon, "붙잡기");
    CDTB_HOOK("TrocTrFrameEventRegistMercenaryReq", regist_event, "등록이벤트");
    CDTB_HOOK("TrocTrSelectMercenarySpawnReq", select_spawn, "스폰선택");
    CDTB_HOOK("TrocTrCallSpecialVehicleByQuickSlotReq", call_quick, "부르기");
    CDTB_HOOK("TrocTrMercenaryDataListAck", data_list, "소유목록");
    CDTB_HOOK("TrocTrSummonMercenaryAfterRegistAck", after_regist, "등록후소환");
    CDTB_HOOK("TrocTrResponseHiredMercenaryToTargetAck", hire_response,
              "획득응답");
    // 아이템 사용 두 경로. 부적을 게임이 어떻게 쓰는지(본문 A·B·C·D) 배운다.
    CDTB_HOOK("TrocTrUseItemReq", use_item, "아이템사용");
    CDTB_HOOK("TrocTrUseItemByItemInfoReq", use_item_info, "아이템사용/정보");
#undef CDTB_HOOK
    if (n == 0) return false;
    g_installed.store(true, std::memory_order_release);
    log::infof("동반자 캡처 준비됨 ({}경로) - 길들이기·등록·부르기·아이템 사용을 하면 뜬다", n);
    return true;
}


// --------------------------------------------------- 고용 작업 추적

namespace {

// (컴포넌트, &결과, &핸들, 0, 플래그) - 스택 인자 1개까지 그대로 넘긴다.
using HireWorkFn = void*(__fastcall*)(void*, std::uint32_t*, std::uint32_t*,
                                      std::uint32_t, std::uint8_t);
HireWorkFn g_orig_hire_work = nullptr;
std::atomic<bool> g_hire_trace{false};
std::atomic<int> g_hire_logs{0};
std::mutex g_hire_mutex;
HireWorkResult g_last_hire_work;
constexpr int kHireLogMax = 60;

void* __fastcall det_hire_work(void* comp, std::uint32_t* result,
                               std::uint32_t* handle, std::uint32_t z,
                               std::uint8_t flag) {
    void* r = g_orig_hire_work(comp, result, handle, z, flag);
    if (g_hire_logs.load(std::memory_order_relaxed) < kHireLogMax) {
        g_hire_logs.fetch_add(1, std::memory_order_relaxed);
        std::uint32_t hv = 0, code = 0;
        if (handle != nullptr) hv = *handle;
        if (result != nullptr) code = *result;
        log::infof("고용 작업: 핸들 0x{:08X} 플래그 {} -> 코드 {} ({})", hv, flag,
                   code, code == 0 ? "성공" : "거부");
        std::lock_guard<std::mutex> lock(g_hire_mutex);
        g_last_hire_work.valid = true;
        g_last_hire_work.handle = hv;
        g_last_hire_work.flag = flag;
        g_last_hire_work.code = code;
    }
    return r;
}

}  // namespace

HireWorkResult last_hire_work() {
    std::lock_guard<std::mutex> lock(g_hire_mutex);
    return g_last_hire_work;
}

bool companion_hire_trace_installed() {
    return g_hire_trace.load(std::memory_order_acquire);
}

bool companion_hire_trace_install(const mem::Reader& reader) {
    if (g_hire_trace.load(std::memory_order_acquire)) return true;
    if (!mem::hook_init()) return false;
    const std::uintptr_t fn = reader.module_base() + kHireWorkRva;
    // 프롤로그가 기대와 다르면(패치로 밀렸으면) 걸지 않는다.
    // 0x2ADE280: mov [rsp+0x10],rbx / mov [rsp+0x18],rsi / mov [rsp+0x20],rdi
    std::uint8_t head[8]{};
    if (!reader.read(fn, head, sizeof(head))) return false;
    if (!(head[0] == 0x48 && head[1] == 0x89 && head[2] == 0x5C &&
          head[3] == 0x24 && head[4] == 0x10)) {
        log::warnf("고용 작업 추적: RVA 0x{:X} 프롤로그가 다르다 ({:02X} {:02X} "
                   "{:02X} {:02X} {:02X}) - 걸지 않는다",
                   kHireWorkRva, head[0], head[1], head[2], head[3], head[4]);
        return false;
    }
    if (!mem::hook_install(reinterpret_cast<void*>(fn), &det_hire_work,
                           reinterpret_cast<void**>(&g_orig_hire_work))) {
        log::warnf("고용 작업 추적: 후킹 실패");
        return false;
    }
    g_hire_trace.store(true, std::memory_order_release);
    log::infof("고용 작업 추적 설치 (RVA 0x{:X}) - 거부 코드를 찍는다", kHireWorkRva);
    return true;
}

// --------------------------------------------------- 2976 구동

namespace {

MessageDesc g_use_item_msg;
std::atomic<bool> g_use_item_ready{false};

}  // namespace

bool companion_use_item_resolve(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_use_item_ready.load(std::memory_order_acquire)) return true;
    MessageDesc m;
    if (!resolve_message(rtti, reader, "TrocTrUseItemByItemInfoReq", &m)) {
        return false;
    }
    if (m.id != kUseItemByInfoId) {
        log::warnf("아이템 사용 메시지: ID 가 {} (기대 {})", m.id, kUseItemByInfoId);
    }
    g_use_item_msg = m;
    g_use_item_ready.store(true, std::memory_order_release);
    return true;
}

bool companion_use_item_ready() {
    return g_use_item_ready.load(std::memory_order_acquire);
}

bool request_use_item(std::uintptr_t session, std::uint32_t item_key,
                      std::uint32_t b, std::uint8_t c, std::uint32_t d) {
    if (!companion_use_item_ready() || session == 0 || item_key == 0) return false;
    std::uint8_t wire[32]{};
    std::size_t len = 0;
    if (!build_use_item_wire(item_key, b, c, d, wire, sizeof(wire), &len)) {
        return false;
    }
    log::infof("아이템 사용 구동(2976) 요청: 키 {} B {} C 0x{:X} D {}", item_key, b,
               c, d);
    return request_message(session, g_use_item_msg, wire, len);
}


// --------------------------------------------------- 실험용 메시지 표

namespace {

// 이 이름들만 미리 해석해 둔다. 명령 파일의 `msg` 가 와이어 머리의 ID 로 고른다.
const char* const kMessageClasses[] = {
    "TrocTrUseItemReq",                    // 2676 아이템 사용 (부적이 이것)
    "TrocTrUseItemByItemInfoReq",          // 2976
    "TrocTrHireMercenaryToTargetReq",      // 2338 대상 고용(획득)
    "TrocTrHireMercenaryFromInventoryReq", // 2454 인벤 고용
    "TrocTrCatchBySummonReq",              // 2386 붙잡기
    "TrocTrSelectMercenarySpawnReq",       // 2894 스폰 선택
};
constexpr int kMessageMax = 8;
MessageDesc g_msgs[kMessageMax];
int g_msg_count = 0;

const MessageDesc* find_message(std::uint16_t id) {
    for (int i = 0; i < g_msg_count; ++i) {
        if (g_msgs[i].id == id) return &g_msgs[i];
    }
    return nullptr;
}

}  // namespace

bool parse_hex_bytes(const std::string& text, std::uint8_t* out, std::size_t cap,
                     std::size_t* len_out) {
    if (out == nullptr) return false;
    std::size_t n = 0;
    int hi = -1;
    for (char ch : text) {
        int v;
        if (ch >= '0' && ch <= '9') v = ch - '0';
        else if (ch >= 'a' && ch <= 'f') v = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') v = ch - 'A' + 10;
        else if (ch == ' ' || ch == '\t' || ch == ',') continue;
        else return false;
        if (hi < 0) { hi = v; continue; }
        if (n >= cap) return false;
        out[n++] = static_cast<std::uint8_t>((hi << 4) | v);
        hi = -1;
    }
    if (hi >= 0) return false;   // 홀수 자릿수
    if (len_out) *len_out = n;
    return n > 0;
}

int companion_message_count() { return g_msg_count; }

bool companion_resolve_messages(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_msg_count > 0) return true;
    for (const char* cls : kMessageClasses) {
        if (g_msg_count >= kMessageMax) break;
        MessageDesc m;
        if (!resolve_message(rtti, reader, cls, &m)) continue;
        g_msgs[g_msg_count++] = m;
    }
    log::infof("실험용 메시지 {}개 해석됨 - 명령 파일 `msg <16진 와이어>` 로 구동",
               g_msg_count);
    return g_msg_count > 0;
}


// --------------------------------------------------- 획득 (2338)

bool build_hire_wire(std::uint32_t handle, std::uint8_t flag, std::uint8_t* out,
                     std::size_t cap, std::size_t* len_out) {
    if (out == nullptr || cap < kHireWireLen) return false;
    const std::uint16_t id = kHireToTargetId;
    const std::uint16_t body = 5;
    std::memcpy(out + 0, &id, 2);
    out[2] = 0;
    std::memcpy(out + 3, &body, 2);
    std::memcpy(out + 5, &handle, 4);
    out[9] = flag;
    if (len_out) *len_out = kHireWireLen;
    return true;
}

bool hire_target_ready() { return find_message(kHireToTargetId) != nullptr; }

bool request_hire_target(std::uintptr_t session, std::uint32_t handle,
                         std::uint8_t flag) {
    const MessageDesc* m = find_message(kHireToTargetId);
    if (m == nullptr || session == 0 || handle == 0) return false;
    std::uint8_t wire[16]{};
    std::size_t len = 0;
    if (!build_hire_wire(handle, flag, wire, sizeof(wire), &len)) return false;
    log::infof("획득 요청: 대상 핸들 0x{:08X} 플래그 {}", handle, flag);
    return request_message(session, *m, wire, len);
}

// --------------------------------------------------- 명령 파일

namespace {

std::atomic<bool> g_cmd_stop{false};
std::thread g_cmd_thread;
const mem::Reader* g_cmd_reader = nullptr;

// 그란트 패널과 같은 규칙: 서버 세션 중 가장 유력한 것.
std::uintptr_t pick_server_session_impl() {
    std::uintptr_t seen[16]{};
    std::uint32_t hits[16]{};
    const int n = seen_sessions(seen, hits, 16);
    if (n == 0) return 0;
    bool server[16]{};
    for (int i = 0; i < n; ++i) server[i] = session_is_server(i);
    const int pick = best_actor_index(hits, server, n);
    if (pick < 0 || pick >= n || !server[pick]) return 0;
    return seen[pick];
}

std::uint32_t parse_u32(const std::string& s, std::uint32_t dflt) {
    if (s.empty()) return dflt;
    return static_cast<std::uint32_t>(std::strtoul(s.c_str(), nullptr, 0));
}

std::vector<std::string> split_ws(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : line) {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::wstring command_path() { return module_directory() + L"cdtoybox_cmd.txt"; }

std::string read_and_delete(const std::wstring& path) {
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::string text;
    LARGE_INTEGER size{};
    if (::GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < 65536) {
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
    ::DeleteFileW(path.c_str());
    return text;
}

void command_loop() {
    const std::wstring path = command_path();
    while (!g_cmd_stop.load(std::memory_order_acquire)) {
        const std::string text = read_and_delete(path);
        if (!text.empty()) {
            std::size_t pos = 0;
            while (pos < text.size()) {
                std::size_t nl = text.find('\n', pos);
                if (nl == std::string::npos) nl = text.size();
                const std::string line = text.substr(pos, nl - pos);
                pos = nl + 1;
                if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
                if (line[0] == '#') continue;
                std::string reply;
                const bool ok = companion_run_command(line, &reply);
                log::infof("명령 [{}] -> {}{}", line, ok ? "실행" : "거부",
                           reply.empty() ? "" : (": " + reply));
                // 대기열은 한 번에 하나뿐이다. 다음 줄은 쿨다운 뒤에.
                for (int i = 0; i < 25 && !g_cmd_stop.load(); ++i) ::Sleep(100);
            }
        }
        for (int i = 0; i < 5 && !g_cmd_stop.load(); ++i) ::Sleep(100);
    }
}

}  // namespace

std::uintptr_t companion_pick_session() { return pick_server_session_impl(); }

bool companion_run_command(const std::string& line, std::string* reply) {
    const auto args = split_ws(line);
    if (args.empty()) return false;
    const std::string& cmd = args[0];
    auto say = [&](const std::string& s) {
        if (reply) *reply = s;
    };
    if (cmd == "give") {
        if (args.size() < 2) { say("give <키> [개수]"); return false; }
        const std::uint32_t key = parse_u32(args[1], 0);
        const std::int64_t count = args.size() > 2 ? static_cast<std::int64_t>(parse_u32(args[2], 1)) : 1;
        const std::uintptr_t session = companion_pick_session();
        if (session == 0) { say("서버 세션 없음"); return false; }
        if (!give_ready()) { say("지급 준비 안 됨"); return false; }
        const bool ok = request_give(session, key, count, GiveExtras{});
        say(ok ? "지급 요청" : "지급 거부(대기열/쿨다운)");
        return ok;
    }
    if (cmd == "useitem") {
        if (args.size() < 2) { say("useitem <키> [B] [C] [D]"); return false; }
        const std::uint32_t key = parse_u32(args[1], 0);
        const std::uint32_t b = args.size() > 2 ? parse_u32(args[2], 0) : 0;
        const std::uint8_t c = static_cast<std::uint8_t>(
            args.size() > 3 ? parse_u32(args[3], kUseItemByInfoKindC) : kUseItemByInfoKindC);
        const std::uint32_t d = args.size() > 4 ? parse_u32(args[4], 0) : 0;
        const std::uintptr_t session = companion_pick_session();
        if (session == 0) { say("서버 세션 없음"); return false; }
        if (!companion_use_item_ready()) { say("2976 미해석"); return false; }
        const bool ok = request_use_item(session, key, b, c, d);
        say(ok ? "사용 요청" : "사용 거부(대기열/쿨다운)");
        return ok;
    }
    if (cmd == "msg") {
        if (args.size() < 2) { say("msg <16진 와이어(머리 포함)>"); return false; }
        std::string hex;
        for (std::size_t i = 1; i < args.size(); ++i) hex += args[i];
        std::uint8_t wire[kMessageWireMax]{};
        std::size_t len = 0;
        if (!parse_hex_bytes(hex, wire, sizeof(wire), &len) || len < 5) {
            say("16진 파싱 실패"); return false;
        }
        std::uint16_t id = 0, body = 0;
        decode_message_header(wire, len, &id, &body);
        if (body + 5u != len) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "머리 본문길이 %u 인데 실제 %zu", body,
                          len - 5);
            say(buf);
            return false;
        }
        const MessageDesc* m = find_message(id);
        if (m == nullptr) { say("그 ID 는 해석돼 있지 않다"); return false; }
        const std::uintptr_t session = companion_pick_session();
        if (session == 0) { say("서버 세션 없음"); return false; }
        const bool ok = request_message(session, *m, wire, len);
        say(ok ? "구동 요청" : "구동 거부(대기열/쿨다운)");
        return ok;
    }
    say("모르는 명령");
    return false;
}

void companion_command_start(const mem::Reader& reader) {
    if (g_cmd_thread.joinable()) return;
    g_cmd_reader = &reader;
    g_cmd_stop.store(false, std::memory_order_release);
    g_cmd_thread = std::thread(command_loop);
    log::infof("명령 파일 감시 시작: cdtoybox_cmd.txt (give / useitem / msg)");
}

void companion_command_stop() {
    g_cmd_stop.store(true, std::memory_order_release);
    if (g_cmd_thread.joinable()) g_cmd_thread.join();
}

}  // namespace cdtb::game

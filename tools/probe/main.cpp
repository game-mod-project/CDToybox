// CDToybox 외부 분석 도구.
//
// 게임에 붙어 메모리를 읽고 RTTI로 객체를 찾는다. 인게임 UI가 아니라
// 명령줄 도구인 이유는, 이 작업의 조작자가 사람이 아니라 에이전트이기
// 때문이다. 반복 실행할 수 있어야 값을 찾는 일을 자율적으로 한다.

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "remote.h"
#include "mem/rtti.h"
#include "mem/image_dump.h"
#include "remote_reader.h"
#include "findquat.h"
#include "game/actors.h"
#include "game/camera.h"
#include "game/clan.h"
#include "game/equip.h"
#include "game/grant.h"
#include "game/inventory.h"
#include "game/items.h"
#include "game/localization.h"
#include "game/player.h"
#include "game/roster.h"
#include "game/stash.h"

using namespace cdtb;
using namespace cdtb::probe;

namespace {

void usage() {
    std::printf(
        "사용법: cdtb_probe <명령> [인자]\n"
        "\n"
        "  info                        프로세스와 모듈 정보\n"
        "  types <부분문자열> [최대]   RTTI 클래스 검색\n"
        "  vtable <클래스명>           클래스의 vtable 주소\n"
        "  instances <클래스명> [최대] 살아있는 인스턴스 주소\n"
        "  dump <주소> [바이트]        16진 + float 덤프\n"
        "  floats <주소> [개수]        float 격자 덤프\n"
        "  regions                     메모리 영역 요약\n"
        "  setf <주소> <값>            float 쓰기 (실행 중 게임에 반영)\n"
        "  poke <주소> <16진바이트>    바이트를 그대로 쓴다\n"
        "  heapfind <16진바이트>       힙에서 바이트 서명 찾기\n"
        "  heapdump <16진바이트> [앞] [뒤]  서명 자리마다 앞뒤 덤프\n"
        "  dumpmany <바이트수> <주소들>  여러 주소를 한 번에 덤프\n"
        "  instcount <이름조각들>       클래스별 인스턴스 개수만\n"
        "  diff <주소> [개수] [ms]     시간차로 변하는 float 슬롯 찾기\n"
        "  findvec3 <x> <y> <z> [오차] [최대]  좌표와 일치하는 float3 전부\n"
        "  findquat [ms] [최대]        시점을 돌리는 동안 변하는 쿼터니언\n"
        "  hold <주소> <x> <y> <z> [ms]  좌표를 눌러 써서 화면이 변하는지 본다\n"
        "  findvec3d <x> <y> <z> [오차] [최대] double 좌표 찾기\n"
        "  holdmany <x> <y> <z> <오차> <dy> [ms] [start] [count]  묶어서 눌러쓰기\n"
        "  findmat <x> <y> <z> [오차] [최대]   좌표 근처의 정규직교 4x4 찾기\n"
        "  loc                         현지화 시스템 + 카테고리별 개수\n"
        "  loc <키>                    현지화 키 하나 조회 (10진/0x16진)\n"
        "  loc item <엔티티키>         이름(0x70)과 다음 칸(0x71)\n"
        "  items [최대]                아이템 표를 키+이름으로 나열\n"
        "  items find <문자열>         이름에 그 문자열이 든 것만\n"
        "  itemmap [최대]              아이템키 <-> 짧은 식별자 대응표\n"
        "  itemmap id <짧은id> ...     짧은 식별자로 아이템 되찾기\n"
        "  itemmap key <아이템키> ...  아이템 키의 짧은 식별자\n"
        "  itemmap cand                변환 함수 패턴 후보 전부\n"
        "  invlist [주소]              인벤토리를 이름·담금질까지\n"
        "  invlist raw [주소]          + 뜻을 모르는 칸까지\n"
        "  invexport [파일]            인벤토리를 보관함 파일로\n"
        "  clan                        내가 가진 동반자 명부\n"
        "  charfind <조각> [최대]        캐릭터를 이름으로 찾아 행·키를 낸다\n"
        "  setspecies <번호> <행>     동반자의 종을 바꿈(클라+서버, 검증 후)\n"
        "  unspawn <번호> [read]     죽은 액터 핸들을 지워 소환 판정을 푸다\n"
        "  dumpimage [파일] [--raw]    실행 중 프로세스의 모듈 이미지를\n"
        "                              디스어셈블러가 읽는 PE 로 뜬다\n"
        "\n"
        "주소는 16진(0x 접두 선택)으로 준다.\n");
}

std::string ansi_to_utf8(const char* s);

std::uintptr_t parse_addr(const char* s) {
    return static_cast<std::uintptr_t>(std::strtoull(s, nullptr, 16));
}

// 16진 한 글자. 아니면 -1.
int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// 현지화 표를 읽어 이름을 푼다. 전부 읽기다.
//
// 게임 자신의 조회 함수(0x1410d7230)는 부르지 않는다. 그 함수는
// 잠금을 잡고, 못 찾은 키의 항목을 새로 만들어 표에 끼워 넣는다.
void cmd_loc(const mem::Rtti& rt, const mem::Reader& reader, const Remote& r,
             int argc, char** argv) {
    game::LocSystem sys;
    if (!game::find_loc_system(rt, reader, &sys)) {
        std::printf("현지화 시스템을 찾지 못했습니다.\n");
        std::uint64_t rva = 0;
        if (game::find_loc_global_rva(rt.image(), &rva)) {
            std::printf("  전역 RVA 0x%llX 는 찾았습니다.\n"
                        "  전역이 비었거나 문자열 풀이 아직 없습니다.\n",
                        static_cast<unsigned long long>(rva));
        } else {
            std::printf("  str() 본문 패턴이 이미지에서 유일하게 일치하지\n"
                        "  않습니다. 게임이 갱신돼 본문이 바뀌었을 수 있습니다.\n");
        }
        return;
    }

    std::printf("전역    0x%llX  (RVA 0x%llX)\n",
                static_cast<unsigned long long>(sys.global),
                static_cast<unsigned long long>(sys.global - r.module_base()));
    std::printf("시스템  0x%llX\n",
                static_cast<unsigned long long>(sys.object));
    std::printf("풀      0x%llX   크기 %u 바이트\n",
                static_cast<unsigned long long>(sys.pool), sys.pool_size);

    if (argc < 3) {
        std::vector<game::LocCategory> cats;
        if (!game::loc_categories(reader, sys, &cats)) {
            std::printf("카테고리 표를 읽지 못했습니다.\n");
            return;
        }
        std::printf("\n카테고리     개수  포인터 배열\n");
        std::size_t total = 0;
        for (int c = 0; c < static_cast<int>(cats.size()); ++c) {
            if (cats[c].count == 0) continue;
            total += cats[c].count;
            std::printf("  %3d    %9u  0x%llX\n", c, cats[c].count,
                        static_cast<unsigned long long>(cats[c].array));
        }
        std::printf("비어있지 않은 카테고리의 합계 %zu 항목\n", total);
        return;
    }

    // loc find <문자열> [최대]  : 텍스트로 키를 역으로 찾는다.
    //
    // 분류명("도구", "한손 무기")도 현지화 문자열이다. 그 키를 알면
    // 레코드의 어느 칸이 분류인지 바로 드러난다.
    if (std::strcmp(argv[2], "find") == 0) {
        if (argc < 4) { std::printf("사용법: loc find <문자열> [최대]\n"); return; }
        const std::string needle = ansi_to_utf8(argv[3]);
        const std::size_t max = (argc > 4) ? std::strtoull(argv[4], nullptr, 10)
                                           : 30;
        // 카테고리를 지정하지 않으면 설명(cat 7)이 한도를 다 써 버린다.
        const int only_cat = (argc > 5) ? std::atoi(argv[5]) : -1;
        // 정확히 일치하는 것만 볼지. 분류명은 짧아 부분 일치가 넘친다.
        const bool exact = (argc > 6) && std::strcmp(argv[6], "exact") == 0;

        // 풀을 한 번에 읽어 둔다. 항목마다 읽으면 너무 느리다.
        std::vector<char> pool(sys.pool_size);
        if (!reader.read(sys.pool, pool.data(), pool.size())) {
            std::printf("문자열 풀을 읽지 못했습니다\n");
            return;
        }
        std::vector<game::LocCategory> cats;
        if (!game::loc_categories(reader, sys, &cats)) {
            std::printf("카테고리 표를 읽지 못했습니다\n");
            return;
        }
        std::printf("\n'%s' 를 담은 항목\n\n", needle.c_str());
        std::printf("%-4s %-22s %-12s %-10s %s\n", "cat", "키", "엔티티",
                    "필드", "텍스트");
        std::size_t shown = 0, scanned = 0;
        for (int c = 0; c < static_cast<int>(cats.size()) && shown < max; ++c) {
            if (only_cat >= 0 && c != only_cat) continue;
            if (cats[c].count == 0 || cats[c].array == 0) continue;
            std::vector<std::uint64_t> ptrs(cats[c].count);
            if (!reader.read(cats[c].array, ptrs.data(), ptrs.size() * 8)) {
                continue;
            }
            for (const auto p : ptrs) {
                if (p == 0 || shown >= max) continue;
                ++scanned;
                struct { std::uint64_t key; std::uint32_t off; } h{};
                if (!reader.read(static_cast<std::uintptr_t>(p) + 0x10, &h,
                                 sizeof(h))) {
                    continue;
                }
                if (h.off == 0xFFFFFFFFu || h.off >= pool.size()) continue;
                const char* s = pool.data() + h.off;
                const std::size_t room = pool.size() - h.off;
                if (::strnlen(s, room) >= room) continue;
                const std::string_view sv(s);
                if (exact ? (sv != needle)
                          : (sv.find(needle) == std::string_view::npos)) {
                    continue;
                }
                ++shown;
                std::printf("%-4d %-22llu %-12u 0x%-8X %s\n", c,
                            static_cast<unsigned long long>(h.key),
                            game::loc_key_entity(h.key),
                            game::loc_key_field(h.key), s);
            }
        }
        std::printf("\n항목 %zu개를 훑어 %zu개 일치\n", scanned, shown);
        return;
    }

    // loc item <엔티티키>  : 이름(0x70)과 그 다음 칸(0x71)
    if (std::strcmp(argv[2], "item") == 0) {
        if (argc < 4) {
            std::printf("사용법: loc item <엔티티키>\n");
            return;
        }
        const std::uint32_t ent =
            static_cast<std::uint32_t>(std::strtoul(argv[3], nullptr, 0));
        // 필드 번호를 훑는다. 이름(0x70)·설명(0x71) 말고 무엇이 더
        // 달려 있는지 보려면 넓게 봐야 한다.
        const std::uint32_t lo = (argc > 4)
            ? static_cast<std::uint32_t>(std::strtoul(argv[4], nullptr, 0)) : 0;
        const std::uint32_t hi = (argc > 5)
            ? static_cast<std::uint32_t>(std::strtoul(argv[5], nullptr, 0))
            : 0xFF;
        std::printf("\n엔티티 %u (0x%X)  필드 0x%X..0x%X\n", ent, ent, lo, hi);
        std::size_t hits = 0;
        for (std::uint32_t f = lo; f <= hi; ++f) {
            const std::uint64_t k = game::loc_key(ent, f);
            std::string text;
            int cat = -1;
            if (!game::resolve(reader, sys, k, &text, &cat)) continue;
            ++hits;
            if (text.size() > 90) text = text.substr(0, 90) + "...";
            std::printf("  필드 0x%02X  [cat %2d]  '%s'\n", f, cat,
                        text.c_str());
        }
        std::printf("  일치 %zu개\n", hits);
        return;
    }

    // loc <키>  : 10진 또는 0x 16진
    const std::uint64_t key = std::strtoull(argv[2], nullptr, 0);
    std::printf("\n키 %llu = 0x%llX   (엔티티 %u, 필드 0x%X)\n",
                static_cast<unsigned long long>(key),
                static_cast<unsigned long long>(key),
                game::loc_key_entity(key), game::loc_key_field(key));
    std::string text;
    int cat = -1;
    if (game::resolve(reader, sys, key, &text, &cat)) {
        std::printf("카테고리 %d\n'%s'\n", cat, text.c_str());
    } else {
        std::printf("찾지 못했습니다.\n");
    }
}

// argv 는 시스템 ANSI 코드페이지로 온다(이 기계는 949). 게임의 문자열
// 풀은 UTF-8 이므로 그대로 비교하면 한글이 절대 맞지 않는다 - 'items
// find 화살' 이 0건으로 나왔다. 비교 전에 UTF-8 로 옮긴다.
std::string ansi_to_utf8(const char* s) {
    if (s == nullptr || *s == '\0') return {};
    const int wn = ::MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    if (wn <= 0) return s;
    std::wstring w(static_cast<std::size_t>(wn), L'\0');
    ::MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), wn);
    const int un = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), -1, nullptr, 0,
                                         nullptr, nullptr);
    if (un <= 0) return s;
    std::string u(static_cast<std::size_t>(un), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), -1, u.data(), un, nullptr,
                          nullptr);
    u.resize(std::strlen(u.c_str()));   // 세었던 널 종단을 뗀다
    return u;
}

// 힙에서 u32 값을 찾는다. 아이템 키가 어디 담겨 있는지 보는 용도다.
//
// 인벤토리 컴포넌트를 한 겹 따라가서는 못 찾았다. 값이 어디 있는지
// 먼저 알아야 소유 구조를 거슬러 올라갈 수 있다.
void cmd_findu32(const Remote& r, std::uint32_t value, std::size_t max) {
    const auto regs = r.regions();
    std::vector<std::uint8_t> buf;
    std::size_t found = 0, scanned = 0;
    std::printf("힙에서 %u (0x%X) 를 찾습니다\n", value, value);
    for (const auto& reg : regs) {
        if (!reg.writable || reg.is_image) continue;
        if (reg.size == 0 || reg.size > (512u << 20)) continue;
        buf.resize(reg.size);
        if (!r.read(reg.base, buf.data(), buf.size())) continue;
        scanned += reg.size;
        for (std::size_t i = 0; i + 4 <= buf.size(); i += 4) {
            std::uint32_t v = 0;
            std::memcpy(&v, buf.data() + i, 4);
            if (v != value) continue;
            std::printf("  0x%llX\n",
                        static_cast<unsigned long long>(reg.base + i));
            if (++found >= max) {
                std::printf("(%zu개에서 멈춥니다)\n", max);
                return;
            }
        }
    }
    std::printf("%zu곳, 훑은 양 %.1f GB\n", found, scanned / 1073741824.0);
}

// float 값 변화 스캔. 후보를 hp_candidates.txt 에 이어 붙여, 재실행하면
// 교집합으로 좁힌다. HP 를 바꿔 가며 두세 번 돌리면 살아있는 주소만 남는다.
//   hpscan <값> [eps]   (처음=전체 수집, 이후=교집합)
//   hpscan reset        (후보 파일 삭제)
void cmd_hpscan(const Remote& r, int argc, char** argv) {
    const char* path = "hp_candidates.txt";
    if (argc >= 3 && std::strcmp(argv[2], "reset") == 0) {
        std::remove(path);
        std::printf("후보 초기화\n");
        return;
    }
    if (argc < 3) {
        std::printf("사용: hpscan <값> [eps] | hpscan reset\n");
        return;
    }
    const float target = static_cast<float>(std::atof(argv[2]));
    const float eps = (argc > 3) ? static_cast<float>(std::atof(argv[3])) : 0.5f;

    std::vector<std::uint64_t> prev;
    if (std::FILE* f = std::fopen(path, "r")) {
        unsigned long long a = 0;
        while (std::fscanf(f, "%llx", &a) == 1) prev.push_back(a);
        std::fclose(f);
    }

    // 표현 무관 매칭: int32==값, int32==값*1000, float≈값, float≈값*1000.
    // 변화 스캔을 반복하면 틀린 표현은 교집합에서 저절로 사라진다.
    const std::int32_t iv1 = static_cast<std::int32_t>(target + 0.5f);
    const std::int32_t iv2 = static_cast<std::int32_t>(target * 1000.0f + 0.5f);
    const float f2 = target * 1000.0f;
    auto matches = [&](const void* p) {
        std::int32_t iv = 0;
        float fv = 0.0f;
        std::memcpy(&iv, p, 4);
        std::memcpy(&fv, p, 4);
        if (iv == iv1 || iv == iv2) return true;
        if (fv >= target - eps && fv <= target + eps) return true;
        if (fv >= f2 - 1.0f && fv <= f2 + 1.0f) return true;
        return false;
    };

    std::vector<std::uint64_t> now;
    if (prev.empty()) {
        const auto regs = r.regions();
        std::vector<std::uint8_t> buf;
        for (const auto& reg : regs) {
            if (!reg.writable || reg.is_image) continue;
            if (reg.size == 0 || reg.size > (512u << 20)) continue;
            buf.resize(reg.size);
            if (!r.read(reg.base, buf.data(), buf.size())) continue;
            for (std::size_t i = 0; i + 4 <= buf.size(); i += 4) {
                if (matches(buf.data() + i)) now.push_back(reg.base + i);
            }
        }
    } else {
        for (const auto a : prev) {
            std::uint8_t b4[4]{};
            if (r.read(a, b4, 4) && matches(b4)) now.push_back(a);
        }
    }

    if (std::FILE* f = std::fopen(path, "w")) {
        for (const auto a : now)
            std::fprintf(f, "%llX\n", static_cast<unsigned long long>(a));
        std::fclose(f);
    }
    std::printf("후보 %zu개 (%s, 값 %.1f)\n", now.size(),
                prev.empty() ? "초기 수집" : "교집합", target);
    for (std::size_t i = 0; i < now.size() && i < 40; ++i)
        std::printf("  0x%llX\n", static_cast<unsigned long long>(now[i]));
    if (now.size() > 40) std::printf("  ...(%zu 더)\n", now.size() - 40);
}

// 플레이어 인벤토리를 나열한다.
//
// 서버 인벤토리 컴포넌트를 직접 뜯어 찾았다. 힙 전체를 서명으로
// 훑는 것보다 이쪽이 확실하다 - 서명은 소켓 플래그를 0xFF 로
// 못박았다가 실제 값(0x00)과 어긋나 거의 아무것도 못 찾았다.
//
//   컴포넌트 +0x78  레코드 배열 (0x190 바이트 간격)
//   컴포넌트 +0x80  u32 사용 개수, u32 용량
//
// 레코드는 TrItemValue 와 같은 배치다.
//   +0x00 u64 인스턴스 ID   +0x08 u32 키   +0x10 i64 개수
//   +0x40 부터 {u32, u8} x 5  소켓
// 인벤토리를 다루는 명령이 공통으로 필요한 것들.
struct InvContext {
    std::uintptr_t component = 0;
    std::vector<game::ItemCatalogEntry> cat;
    std::map<std::uint32_t, std::uint32_t> id_to_key;   // 순번 -> 아이템 키
};

// 주소를 안 주면(want_hex 가 널) RTTI 로 찾는다. 살아 있는 인스턴스가
// 여럿이고 대부분 비어 있어서, 내용이 있는 첫 컴포넌트를 쓴다.
bool build_inv_context(const mem::Rtti& rt, const mem::Reader& reader,
                       const char* want_hex, InvContext* out) {
    if (want_hex != nullptr) {
        out->component = std::strtoull(want_hex, nullptr, 16);
    } else {
        constexpr const char* kClass =
            ".?AVServerInventoryActorComponent@pa@@";
        for (const auto addr : rt.instances_of_class(kClass, 64)) {
            std::vector<game::InventoryContainer> cs;
            if (!game::read_inventory_containers(reader, addr, &cs)) continue;
            bool any = false;
            for (const auto& c : cs) {
                if (c.used > 0) { any = true; break; }
            }
            if (!any) continue;
            out->component = addr;
            break;
        }
        if (out->component == 0) {
            std::printf("내용이 있는 서버 인벤토리 컴포넌트를 찾지"
                        " 못했습니다.\n");
            return false;
        }
        std::printf("컴포넌트 0x%llX\n",
                    static_cast<unsigned long long>(out->component));
    }

    // 이름을 붙이려고 아이템 표를 읽는다.
    std::uintptr_t manager = 0;
    if (game::find_item_manager(rt, reader, &manager)) {
        game::LocSystem sys;
        game::find_loc_system(rt, reader, &sys);
        game::build_item_catalog(reader, manager, sys, &out->cat);
    }
    if (out->cat.empty()) {
        std::printf("아이템 표를 못 읽었습니다\n");
        return false;
    }

    // 인벤토리 레코드의 +0x08 은 아이템 키가 아니라 표에서의 순번이다.
    // 순번 -> 키 대응표를 만들어 이름을 붙인다.
    game::ItemKeyMap km;
    if (game::find_item_key_map(reader, rt.image(),
                                static_cast<std::uint32_t>(out->cat.size()),
                                &km)) {
        std::vector<game::ItemKeyPair> pairs;
        if (game::read_item_key_map(reader, km, &pairs)) {
            for (const auto& p : pairs) out->id_to_key.emplace(p.id, p.key);
        }
    }
    return true;
}

// 인벤토리에서 **열린 소켓**을 가진 레코드를 찾아 덤프한다(both-realms 쓰기
// 실측 대상 선정용). 열림 = 소켓 raw[4] != 0xFF.
void cmd_invsock(const mem::Rtti& rt, const mem::Reader& reader) {
    if (!game::discover_inventory(rt, reader)) {
        std::printf("인벤토리 컴포넌트 못 찾음\n");
        return;
    }
    const std::uintptr_t comp = game::inventory_component();
    std::vector<game::InventoryContainer> conts;
    if (!game::read_inventory_containers(reader, comp, &conts)) {
        std::printf("컨테이너 못 읽음\n");
        return;
    }
    int found = 0;
    for (const auto& c : conts) {
        std::vector<game::InventoryRecord> recs;
        if (!game::read_inventory_records(reader, c, &recs)) continue;
        for (const auto& rec : recs) {
            std::vector<game::InventorySocket> socks;
            if (!game::read_inventory_sockets(reader, rec, &socks)) continue;
            bool has_open = false;
            for (const auto& s : socks) {
                if (s.raw[4] != 0xFF) { has_open = true; break; }
            }
            if (!has_open) continue;
            std::printf("종류%u rec=0x%llX inst=%llu 순번=%u sockptr=0x%llX cnt=%u\n",
                        c.kind, (unsigned long long)rec.address,
                        (unsigned long long)rec.instance_id, rec.index,
                        (unsigned long long)rec.sockets, rec.socket_count);
            for (std::size_t i = 0; i < socks.size(); ++i) {
                const auto& s = socks[i];
                std::printf("   [%zu] %02X %02X %02X %02X %02X %02X  %s\n", i,
                            s.raw[0], s.raw[1], s.raw[2], s.raw[3], s.raw[4],
                            s.raw[5], s.raw[4] == 0xFF ? "잠김" : "열림");
            }
            if (++found >= 12) { std::printf("(멈춤)\n"); return; }
        }
    }
    std::printf("열린 소켓 아이템 %d개\n", found);
}

void cmd_invlist(const mem::Rtti& rt, const mem::Reader& reader,
                 const Remote& r, int argc, char** argv) {
    // invlist raw : 아직 뜻을 모르는 칸까지 낸다.
    bool raw_mode = false;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "raw") == 0) raw_mode = true;
    }

    // "raw" 는 주소가 아니다. 그대로 넘기면 주소로 파싱된다.
    const char* addr =
        (argc >= 3 && !raw_mode) ? argv[2]
                                 : ((argc >= 4) ? argv[3] : nullptr);

    InvContext ctx;
    if (!build_inv_context(rt, reader, addr, &ctx)) {
        return;
    }
    const std::uintptr_t comp = ctx.component;
    const auto& cat = ctx.cat;
    const auto& id_to_key = ctx.id_to_key;
    if (id_to_key.empty()) {
        std::printf("대응표를 못 읽었습니다. 순번만 냅니다.\n");
    }

    // 컴포넌트 앞부분을 훑어 컨테이너 꼴을 찾던 방식은 걷어냈다.
    // 엉뚱한 것이 걸리면서 정작 플레이어 가방은 못 잡았다 - 가방은
    // +0x18 의 포인터 배열 너머에 있다.
    std::vector<game::InventoryContainer> conts;
    if (!game::read_inventory_containers(reader, comp, &conts)) {
        std::printf("컨테이너 배열을 읽지 못했습니다 (컴포넌트 0x%llX)\n",
                    static_cast<unsigned long long>(comp));
        return;
    }

    auto name_of = [&](std::uint32_t item_key) -> const char* {
        for (const auto& e : cat) {
            if (e.key == item_key) {
                return e.name.empty() ? "(이름 없음)" : e.name.c_str();
            }
        }
        return "(표에 없음)";
    };

    long long total = 0, filled = 0;
    int shown = 0;
    for (const auto& c : conts) {
        std::vector<game::InventoryRecord> recs;
        if (!game::read_inventory_records(reader, c, &recs)) continue;
        if (recs.empty()) continue;

        ++shown;
        std::printf("\n[종류 %u] 0x%llX  %zu개  (표시 %u / %u, 칸 %u)\n",
                    c.kind, static_cast<unsigned long long>(c.records),
                    recs.size(), c.used, c.capacity, c.slots);

        for (const auto& rec : recs) {
            ++total;
            std::uint32_t item_key = 0;
            const char* name = "(대응표에 없음)";
            const auto f = id_to_key.find(rec.index);
            if (f != id_to_key.end()) {
                item_key = f->second;
                name = name_of(item_key);
            }
            std::printf("  [%4u] 순번 %-6u 담금질 %-2u 키 %-11u x%-4lld "
                        "ID %-9llu %s\n",
                        rec.slot, rec.index, rec.temper, item_key,
                        static_cast<long long>(rec.count),
                        static_cast<unsigned long long>(rec.instance_id), name);

            // invlist raw : 변환 함수가 채우는 칸을 그대로 낸다.
            //
            // 어느 칸이 무엇인지 아직 다 모른다. 지급분과 원본을
            // 견주려면 값이 보여야 하므로 이름 대신 오프셋으로 낸다.
            // 배선은 specs/2026-09-03-static-analysis.md 의 표에 있다.
            if (raw_mode) {
                std::uint32_t f28 = 0, f2c = 0;
                // +0x70 은 u8 이다. 변환 함수가 `mov byte [rdi+0x70], bl`
                // 로 쓴다 - u32 로 읽으면 옆 칸이 섞여 값이 터무니없어진다.
                std::uint8_t f70 = 0;
                std::uint64_t f30 = 0, f38 = 0;
                std::uint16_t f40 = 0, f58 = 0;
                reader.read_value(rec.address + 0x28, &f28);
                reader.read_value(rec.address + 0x2C, &f2c);
                reader.read_value(rec.address + 0x30, &f30);
                reader.read_value(rec.address + 0x38, &f38);
                reader.read_value(rec.address + 0x40, &f40);
                reader.read_value(rec.address + 0x58, &f58);
                reader.read_value(rec.address + 0x70, &f70);
                std::printf("         +28 %-6u +2C %-6u +30 %-6llu +38 %-8llu"
                            " +40 %-6u +58 %-4u 소켓 %u\n",
                            f28, f2c,
                            static_cast<unsigned long long>(f30),
                            static_cast<unsigned long long>(f38), f40, f58,
                            static_cast<unsigned>(f70));
            }

            // 박힌 소켓이 있을 때만 낸다. 실측에서 대부분 비어 있다.
            std::vector<game::InventorySocket> socks;
            if (!game::read_inventory_sockets(reader, rec, &socks)) continue;
            for (const auto& s : socks) {
                if (s.empty()) continue;
                ++filled;
                std::uint32_t gem_key = 0;
                const char* gem = "(대응표에 없음)";
                const auto g = id_to_key.find(s.index);
                if (g != id_to_key.end()) {
                    gem_key = g->second;
                    gem = name_of(gem_key);
                }
                std::printf("         소켓[%u] 순번 %-6u 키 %-11u %s"
                            "  (%02X %02X %02X %02X %02X %02X)\n",
                            s.slot, s.index, gem_key, gem, s.raw[0], s.raw[1],
                            s.raw[2], s.raw[3], s.raw[4], s.raw[5]);
            }
        }
    }
    std::printf("\n컨테이너 %zu개 중 내용이 있는 것 %d개, 아이템 %lld개,"
                " 박힌 소켓 %lld개\n",
                conts.size(), shown, total, filled);
}

// 인벤토리를 보관함 파일로 빼낸다.
//
//   invexport [파일경로]
//
// 담금질·소켓까지 실어 둔다. 되돌리는 쪽(import)은 아직 없다 -
// 지금은 읽어서 쓰기만 한다.
//
// 순번은 표에서의 위치라 게임이 패치되면 달라진다. 파일에는 게임
// 자신의 식별자인 **아이템 키**를 쓴다.
//
// 기본 파일은 우리가 만드는 것이라 덮어쓴다. 경로를 직접 준 경우에는
// 이미 있으면 거절한다 - 모드의 보관함 파일을 가리켰다가 세트를
// 통째로 날리는 일을 막는다.
void cmd_invexport(const mem::Rtti& rt, const mem::Reader& reader, int argc,
                   char** argv) {
    const char* path = (argc >= 3) ? argv[2] : "cdtoybox_export.txt";
    if (argc >= 3) {
        if (std::FILE* exists = std::fopen(path, "rb")) {
            std::fclose(exists);
            std::printf("이미 있는 파일입니다: %s\n"
                        "덮어쓰지 않습니다. 다른 이름을 주거나 지우고"
                        " 다시 부르세요.\n",
                        path);
            return;
        }
    }

    InvContext ctx;
    if (!build_inv_context(rt, reader, nullptr, &ctx)) return;
    if (ctx.id_to_key.empty()) {
        std::printf("대응표를 못 읽었습니다. 순번을 아이템 키로 바꿀 수"
                    " 없어 빼내지 않습니다.\n");
        return;
    }

    std::vector<game::InventoryContainer> conts;
    if (!game::read_inventory_containers(reader, ctx.component, &conts)) {
        std::printf("컨테이너 배열을 읽지 못했습니다\n");
        return;
    }

    // 키 -> 표의 최대 내구도. 아이템마다 목록을 훑지 않으려고 미리
    // 만든다.
    std::map<std::uint32_t, std::uint16_t> max_endu;
    for (const auto& c : ctx.cat) max_endu[c.key] = c.max_endurance;
    const auto max_endurance_of = [&](std::uint32_t key) -> std::uint16_t {
        const auto it = max_endu.find(key);
        return (it == max_endu.end()) ? 0xFFFF : it->second;
    };

    game::Stash stash;
    long long items = 0, sockets = 0, skipped = 0;
    for (const auto& c : conts) {
        std::vector<game::InventoryRecord> recs;
        if (!game::read_inventory_records(reader, c, &recs)) continue;
        if (recs.empty()) continue;

        const int set = stash.add_set("가방 종류" + std::to_string(c.kind));
        for (const auto& rec : recs) {
            const auto f = ctx.id_to_key.find(rec.index);
            if (f == ctx.id_to_key.end()) {
                ++skipped;   // 키를 모르면 되돌릴 수 없다
                continue;
            }

            game::StashEntry e;
            e.key = f->second;
            e.count = rec.count;
            e.temper = rec.temper;

            // 내구도는 **아이템 종류가 가진 것일 때만** 적는다.
            //
            // 레코드 값(0xFFFF = 없음)으로 가르면 안 된다 - 갓 지급한
            // 것은 게임이 저장을 한 바퀴 돌기 전까지 0 이라, 내구도가
            // 없는 아이템도 `e0` 으로 적히고 다시 꺼내면 부서진 것이
            // 된다. 실측에서 칠흑의 사수 판금 망토가 그랬다.
            if (max_endurance_of(f->second) != 0xFFFF) {
                e.endurance = rec.endurance;
            }
            e.sharpness = rec.sharpness;

            std::vector<game::InventorySocket> socks;
            if (game::read_inventory_sockets(reader, rec, &socks)) {
                for (const auto& s : socks) {
                    if (s.empty()) continue;
                    const auto g = ctx.id_to_key.find(s.index);
                    if (g == ctx.id_to_key.end()) continue;
                    game::StashSocket ss;
                    ss.slot = s.slot;
                    ss.key = g->second;
                    std::memcpy(ss.raw, s.raw, sizeof(ss.raw));
                    e.sockets.push_back(ss);
                    ++sockets;
                }
            }
            stash.set_at(set)->items.push_back(std::move(e));
            ++items;
        }
    }

    if (items == 0) {
        std::printf("빼낼 것이 없습니다.\n");
        return;
    }

    const std::string text = stash.serialize();
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        std::printf("파일을 열지 못했습니다: %s\n", path);
        return;
    }
    const std::size_t wrote = std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
    if (wrote != text.size()) {
        std::printf("쓰다 말았습니다 (%zu / %zu 바이트): %s\n", wrote,
                    text.size(), path);
        return;
    }

    std::printf("%s 에 썼습니다\n", path);
    std::printf("  세트 %d개, 아이템 %lld개, 박힌 소켓 %lld개\n",
                stash.set_count(), items, sockets);
    if (skipped > 0) {
        std::printf("  대응표에 없어 건너뛴 것 %lld개\n", skipped);
    }
}

// 인벤토리 안의 아이템 인스턴스(TrItemValue)를 찾는다.
//
// 담금질·소켓이 반영된 장비를 게임 밖으로 빼내려면 이 구조를 읽어야
// 한다. 예전에는 "키 옆에 개수가 있는 곳" 으로 찾다가 실패했다 -
// 그런 구조가 여럿이었다. 지금은 구조 전체를 알아서 서명이 훨씬
// 강하다.
//
//   +0x08  u32  아이템 키   (실제 표에 있는 키여야 한다)
//   +0x10  i64  개수        (1 이상, 터무니없지 않아야)
//   +0x40..+0x5E  {u32, u8} x 5   생성자가 0xFFFF/0xFF 로 채우는 소켓
//
// 소켓이 비었으면 0xFFFF 가 그대로 남고, 박혀 있으면 다른 값이다.
// 둘 다 허용하되 자리 수가 맞는지를 본다.
void cmd_itemvalue(const mem::Rtti& rt, const mem::Reader& reader,
                   const Remote& r, int argc, char** argv) {
    const std::size_t limit =
        (argc > 2) ? static_cast<std::size_t>(std::atoi(argv[2])) : 40;

    // 아이템 키 집합을 먼저 만든다. 표에 없는 키는 버린다.
    std::uintptr_t manager = 0;
    std::vector<std::uint32_t> keys;
    {
        if (cdtb::game::find_item_manager(rt, reader, &manager)) {
            std::vector<cdtb::game::ItemEntry> items;
            if (cdtb::game::read_item_table(reader, manager, &items, 0)) {
                keys.reserve(items.size());
                for (const auto& e : items) keys.push_back(e.key);
                std::sort(keys.begin(), keys.end());
            }
        }
    }
    if (keys.empty()) {
        std::printf("아이템 표를 못 읽었습니다\n");
        return;
    }
    std::printf("아이템 키 %zu개를 기준으로 찾습니다\n", keys.size());

    std::vector<std::uint8_t> buf;
    std::size_t found = 0;
    for (const auto& reg : r.regions()) {
        if (!reg.writable || reg.is_image) continue;
        if (reg.size < 0x200 || reg.size > (512u << 20)) continue;
        buf.resize(reg.size);
        if (!r.read(reg.base, buf.data(), buf.size())) continue;

        for (std::size_t i = 0; i + 0x60 <= buf.size(); i += 8) {
            std::uint32_t key = 0;
            std::memcpy(&key, buf.data() + i + 0x08, 4);
            if (key == 0) continue;
            if (!std::binary_search(keys.begin(), keys.end(), key)) continue;

            std::int64_t count = 0;
            std::memcpy(&count, buf.data() + i + 0x10, 8);
            if (count <= 0 || count > 1000000) continue;

            // 소켓 자리. 생성자가 {u32 0xFFFF, u8 0xFF} 를 다섯 벌
            // 깔아 둔다. 비어 있으면 그 모양 그대로고, 박혀 있으면
            // 다른 값이다. 한 칸만 맞는 것은 우연이므로 셋 이상을
            // 요구한다 - 실측에서 한 칸 기준은 온갖 것이 걸렸다.
            int exact = 0;
            for (int s = 0; s < 5; ++s) {
                std::uint32_t v = 0;
                std::memcpy(&v, buf.data() + i + 0x40 + s * 6, 4);
                const std::uint8_t f = buf[i + 0x40 + s * 6 + 4];
                if (v == 0xFFFFu && f == 0xFFu) ++exact;
            }
            if (exact < 5) continue;   // 다섯 칸 전부 기본 모양이어야

            // 인스턴스 ID. 생성자는 -1 을 넣고, 게임이 만들 때 진짜
            // 값을 넣는다. -1 이면 아직 발급 안 된 템플릿이다.
            std::uint64_t inst0 = 0;
            std::memcpy(&inst0, buf.data() + i, 8);
            if (inst0 == 0xFFFFFFFFFFFFFFFFull) continue;
            if (inst0 == 0) continue;
            // 자기 근처를 가리키는 포인터는 다른 구조다.
            const std::uintptr_t here = reg.base + i;
            if (inst0 > here - 0x1000 && inst0 < here + 0x1000) continue;

            std::uint16_t f0c = 0, f28 = 0, f2a = 0;
            std::memcpy(&f0c, buf.data() + i + 0x0C, 2);
            std::memcpy(&f28, buf.data() + i + 0x28, 2);
            std::memcpy(&f2a, buf.data() + i + 0x2A, 2);
            std::printf("  0x%llX  키 %-9u 개수 %-5lld ID 0x%llX  "
                        "+0C %u +28 %u +2A %u\n",
                        static_cast<unsigned long long>(here), key,
                        static_cast<long long>(count),
                        static_cast<unsigned long long>(inst0), f0c, f28, f2a);
            if (++found >= limit) {
                std::printf("(%zu개에서 멈춥니다)\n", found);
                return;
            }
        }
    }
    std::printf("모두 %zu곳\n", found);
}

// 힙에서 어떤 주소를 담은 8바이트를 찾는다.
//
// 힙에서 바이트 서명을 찾는다.
//
//   heapfind <16진 바이트들> [최대]
//
// 같은 값을 들고 있는 곳이 여럿일 때 원본을 찾으려고 쓴다. 소켓처럼
// 우리가 읽는 레코드가 사본이고 게임이 되쓰는 경우, 원본도 같은
// 바이트를 들고 있을 것이므로 여기서 후보가 나온다.
//
// 하드웨어 워치포인트를 못 쓰는 대신이다 - 이 게임은 보호 코드가
// 250ms 마다 디버그 레지스터를 지운다
// (docs/superpowers/specs 의 카메라 조사 참고).
void cmd_heapfind(const Remote& r, int argc, char** argv) {
    if (argc < 3) {
        std::printf("사용법: heapfind <16진 바이트들> [최대]\n");
        return;
    }

    std::string hex;
    for (int i = 2; i < argc; ++i) {
        for (const char* p = argv[i]; *p != '\0'; ++p) {
            if (*p != ' ' && *p != ',') hex += *p;
        }
    }
    if (hex.empty() || (hex.size() % 2) != 0) {
        std::printf("16진 바이트가 짝이 안 맞습니다: %zu 글자\n", hex.size());
        return;
    }

    std::vector<std::uint8_t> want;
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const auto hi = hex_digit(hex[i]);
        const auto lo = hex_digit(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            std::printf("16진이 아닌 글자: %c%c\n", hex[i], hex[i + 1]);
            return;
        }
        want.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }

    std::printf("힙에서 %zu바이트 서명을 찾습니다:", want.size());
    for (const auto b : want) std::printf(" %02X", b);
    std::printf("\n");

    std::vector<std::uint8_t> buf;
    std::size_t found = 0, scanned = 0;
    for (const auto& reg : r.regions()) {
        if (!reg.writable || reg.is_image) continue;
        if (reg.size < want.size() || reg.size > (512u << 20)) continue;
        buf.resize(reg.size);
        if (!r.read(reg.base, buf.data(), buf.size())) continue;
        scanned += reg.size;
        for (std::size_t i = 0; i + want.size() <= buf.size(); ++i) {
            if (std::memcmp(buf.data() + i, want.data(), want.size()) != 0) {
                continue;
            }
            std::printf("  0x%llX\n",
                        static_cast<unsigned long long>(reg.base + i));
            if (++found >= 200) {
                std::printf("(200개에서 멈춥니다)\n");
                return;
            }
        }
    }
    std::printf("모두 %zu곳, 훑은 양 %.1f GB\n", found,
                scanned / 1073741824.0);
}

// 서명이 나온 자리마다 그 앞뒤를 덤프한다.
//
// heapfind 로 자리를 찾고 dump 로 하나씩 보면 붙을 때마다 350MB
// 이미지를 다시 읽어야 해서 한 건에 2~3분이 든다. 후보가 수십 개면
// 그것만으로 하루가 간다. 한 번 붙어서 전부 뜬다.
//
// 정상 표본과 불량 표본의 레코드를 나란히 놓고 다른 칸을 찾는 데
// 쓴다(동반자 등록 조사, 2026-09-06).
// 주소 여러 개를 한 번 붙어서 덤프한다.
//
// dump 는 한 번에 하나라, 붙을 때마다 350MB 이미지를 다시 읽어
// 한 건에 2~3분이 든다. 레코드와 그것이 가리키는 하위 객체들을
// 나란히 보려면 대여섯 번을 붙어야 해서 그것만으로 시간이 다 간다.
//
// 정상 표본과 불량 표본을 비교하는 데 쓴다(동반자 등록 조사,
// 2026-09-06). 서식은 dump 와 같아 비교 스크립트가 그대로 읽는다.
void cmd_dumpmany(const Remote& r, int argc, char** argv) {
    if (argc < 4) {
        std::printf("사용법: dumpmany <바이트수> <주소> [주소 ...]\n");
        return;
    }
    std::size_t n = std::strtoull(argv[2], nullptr, 0);
    if (n == 0) n = 0x100;
    if (n > 0x4000) n = 0x4000;
    std::vector<std::uint8_t> buf(n);
    for (int i = 3; i < argc; ++i) {
        const auto addr =
            static_cast<std::uintptr_t>(std::strtoull(argv[i], nullptr, 0));
        std::printf("\n=== 히트 %d  0x%llX  (창 0x%llX)\n", i - 3,
                    static_cast<unsigned long long>(addr),
                    static_cast<unsigned long long>(addr));
        if (!r.read(addr, buf.data(), buf.size())) {
            std::printf("(읽기 실패)\n");
            continue;
        }
        for (std::size_t o = 0; o < n; o += 16) {
            std::printf("0x%llX  ",
                        static_cast<unsigned long long>(addr + o));
            for (std::size_t k = 0; k < 16; ++k) {
                if (o + k < n) {
                    std::printf("%02X ", buf[o + k]);
                } else {
                    std::printf("   ");
                }
            }
            std::printf(" |");
            for (std::size_t k = 0; k < 16 && o + k < n; ++k) {
                const std::uint8_t c = buf[o + k];
                std::printf("%c", (c >= 32 && c < 127) ? c : '.');
            }
            std::printf("|\n");
        }
    }
}

void cmd_heapdump(const Remote& r, int argc, char** argv) {
    if (argc < 3) {
        std::printf("사용법: heapdump <16진 바이트들> [앞=0x40] [뒤=0xC0]\n");
        return;
    }
    // 마지막 인자 둘이 10진/16진 수면 창 크기로 본다. 16진 서명과
    // 섞이지 않게 "0x" 가 붙은 것만 창으로 인정한다.
    std::size_t before = 0x40, after = 0xC0;
    int last = argc;
    while (last > 3) {
        const char* a = argv[last - 1];
        if (a[0] != '0' || (a[1] != 'x' && a[1] != 'X')) break;
        --last;
        if (argc - last > 2) break;
    }
    if (argc - last >= 1) before = std::strtoull(argv[last], nullptr, 0);
    if (argc - last >= 2) after = std::strtoull(argv[last + 1], nullptr, 0);
    if (before > 0x1000) before = 0x1000;
    if (after > 0x1000) after = 0x1000;

    std::string hex;
    for (int i = 2; i < last; ++i) {
        for (const char* p = argv[i]; *p != '\0'; ++p) {
            if (*p != ' ' && *p != ',') hex += *p;
        }
    }
    if (hex.empty() || (hex.size() % 2) != 0) {
        std::printf("16진 바이트가 짝이 안 맞습니다: %zu 글자\n", hex.size());
        return;
    }
    std::vector<std::uint8_t> want;
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const auto hi = hex_digit(hex[i]);
        const auto lo = hex_digit(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            std::printf("16진이 아닌 글자: %c%c\n", hex[i], hex[i + 1]);
            return;
        }
        want.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }

    std::printf("서명 %zu바이트, 창 -0x%zX .. +0x%zX\n", want.size(), before,
                after);
    std::vector<std::uint8_t> buf;
    std::size_t found = 0;
    for (const auto& reg : r.regions()) {
        if (!reg.writable || reg.is_image) continue;
        if (reg.size < want.size() || reg.size > (512u << 20)) continue;
        buf.resize(reg.size);
        if (!r.read(reg.base, buf.data(), buf.size())) continue;
        for (std::size_t i = 0; i + want.size() <= buf.size(); ++i) {
            if (std::memcmp(buf.data() + i, want.data(), want.size()) != 0) {
                continue;
            }
            const std::size_t lo = (i > before) ? i - before : 0;
            std::size_t hi = i + after;
            if (hi > buf.size()) hi = buf.size();
            std::printf("\n=== 히트 %zu  0x%llX  (창 0x%llX)\n", found,
                        static_cast<unsigned long long>(reg.base + i),
                        static_cast<unsigned long long>(reg.base + lo));
            // dump 와 같은 서식이라 비교 스크립트가 그대로 읽는다.
            for (std::size_t o = lo; o < hi; o += 16) {
                std::printf("0x%llX  ",
                            static_cast<unsigned long long>(reg.base + o));
                for (std::size_t k = 0; k < 16; ++k) {
                    if (o + k < hi) {
                        std::printf("%02X ", buf[o + k]);
                    } else {
                        std::printf("   ");
                    }
                }
                std::printf(" |");
                for (std::size_t k = 0; k < 16 && o + k < hi; ++k) {
                    const std::uint8_t c = buf[o + k];
                    std::printf("%c", (c >= 32 && c < 127) ? c : '.');
                }
                std::printf("|\n");
            }
            if (++found >= 80) {
                std::printf("(80개에서 멈춥니다)\n");
                return;
            }
        }
    }
    std::printf("\n모두 %zu곳\n", found);
}


// findptr 은 모듈 이미지만 본다. 메시지 서술자를 가리키는 표는
// 이미지에 없고 힙에 있어서 이게 필요했다.
void cmd_heapptr(const Remote& r, int argc, char** argv) {
    if (argc < 3) {
        std::printf("사용법: heapptr <주소> [최대]\n");
        return;
    }
    const std::uint64_t want =
        std::strtoull(argv[2], nullptr, 16);
    const std::size_t limit =
        (argc > 3) ? static_cast<std::size_t>(std::atoi(argv[3])) : 40;

    std::printf("힙에서 0x%llX 를 담은 곳을 찾습니다\n",
                static_cast<unsigned long long>(want));
    std::vector<std::uint8_t> buf;
    std::size_t found = 0;
    for (const auto& reg : r.regions()) {
        if (!reg.writable || reg.is_image) continue;
        if (reg.size < 8 || reg.size > (512u << 20)) continue;
        buf.resize(reg.size);
        if (!r.read(reg.base, buf.data(), buf.size())) continue;
        for (std::size_t i = 0; i + 8 <= buf.size(); i += 8) {
            std::uint64_t v = 0;
            std::memcpy(&v, buf.data() + i, 8);
            if (v != want) continue;
            std::printf("  0x%llX\n",
                        static_cast<unsigned long long>(reg.base + i));
            if (++found >= limit) {
                std::printf("(%zu개에서 멈춥니다)\n", found);
                return;
            }
        }
    }
    std::printf("모두 %zu곳\n", found);
}

// 값이 바뀌는 것을 따라가며 후보를 좁힌다. 치트엔진이 쓰는 방식이다.
//
// "키 옆에 개수가 있는 곳" 을 찾는 방법은 실패했다. 그런 구조가
// 여럿이고 개수처럼 보이는 값이 실제 개수와 달랐다. 값 자체를 쫓는
// 편이 확실하다 - 아이템을 하나 쓰면 진짜 개수만 줄어든다.
//
//   scan <값>       처음 훑어 후보를 파일에 적는다
//   scan next <값>  적어 둔 후보 중 지금 그 값인 것만 남긴다
//
// 몇 번 반복하면 한 곳으로 수렴한다.
// 모듈 이미지에서 AOB 패턴 히트 수와 RVA 를 보고한다(인라인 훅 사이트 검증용).
void cmd_aob(const mem::Rtti& rt, const mem::Reader& reader, int argc,
             char** argv) {
    if (argc <= 2) {
        std::printf("사용법: aob \"48 89 5F ?? ...\"\n");
        return;
    }
    const auto parsed = mem::parse_pattern(argv[2]);
    if (!parsed) {
        std::printf("패턴 파싱 실패\n");
        return;
    }
    const auto& img = rt.image();
    const mem::Range range{img.data(), img.size()};
    const auto hits = mem::find_all(range, *parsed, 16);
    std::printf("히트 %zu개%s\n", hits.size(),
                hits.size() == 1 ? " (유일 - 훅 안전)" : "");
    for (const auto* h : hits) {
        const std::uint64_t rva = static_cast<std::uint64_t>(h - img.data());
        std::printf("  RVA 0x%llX  addr 0x%llX\n", (unsigned long long)rva,
                    (unsigned long long)(reader.module_base() + rva));
    }
}

void cmd_scan(const Remote& r, int argc, char** argv) {
    const char* kFile = "cdtb_scan.bin";
    const bool next = (argc > 2) && std::strcmp(argv[2], "next") == 0;
    const int vi = next ? 3 : 2;
    if (argc <= vi) {
        std::printf("사용법: scan <값>  |  scan next <값>\n");
        return;
    }
    const std::uint32_t value =
        static_cast<std::uint32_t>(std::strtoul(argv[vi], nullptr, 0));
    // 개수는 대개 한두 바이트다. 폭을 지정할 수 있게 둔다.
    const int width = (argc > vi + 1) ? std::atoi(argv[vi + 1]) : 4;

    auto matches = [&](const std::uint8_t* p) {
        if (width == 1) return *p == static_cast<std::uint8_t>(value);
        if (width == 2) {
            std::uint16_t v = 0;
            std::memcpy(&v, p, 2);
            return v == static_cast<std::uint16_t>(value);
        }
        std::uint32_t v = 0;
        std::memcpy(&v, p, 4);
        return v == value;
    };

    std::vector<std::uintptr_t> cands;
    if (next) {
        std::FILE* f = std::fopen(kFile, "rb");
        if (f == nullptr) {
            std::printf("이전 후보 파일이 없습니다. 먼저 scan <값> 을 하세요.\n");
            return;
        }
        std::uintptr_t a = 0;
        while (std::fread(&a, sizeof(a), 1, f) == 1) cands.push_back(a);
        std::fclose(f);
        std::printf("이전 후보 %zu개\n", cands.size());
        std::vector<std::uintptr_t> keep;
        std::uint8_t tmp[4]{};
        for (const auto a2 : cands) {
            if (!r.read(a2, tmp, static_cast<std::size_t>(width))) continue;
            if (matches(tmp)) keep.push_back(a2);
        }
        cands.swap(keep);
    } else {
        std::vector<std::uint8_t> buf;
        for (const auto& reg : r.regions()) {
            if (!reg.writable || reg.is_image) continue;
            if (reg.size == 0 || reg.size > (512u << 20)) continue;
            buf.resize(reg.size);
            if (!r.read(reg.base, buf.data(), buf.size())) continue;
            const std::size_t end = buf.size() - static_cast<std::size_t>(width);
            for (std::size_t i = 0; i <= end; ++i) {
                if (matches(buf.data() + i)) cands.push_back(reg.base + i);
            }
            if (cands.size() > 40000000u) break;   // 안전장치
        }
    }

    std::FILE* f = std::fopen(kFile, "wb");
    if (f != nullptr) {
        for (const auto a : cands) std::fwrite(&a, sizeof(a), 1, f);
        std::fclose(f);
    }
    std::printf("값 %u (%d바이트) 후보 %zu개 -> %s\n", value, width,
                cands.size(), kFile);
    for (std::size_t i = 0; i < cands.size() && i < 24; ++i) {
        std::printf("  0x%llX\n", static_cast<unsigned long long>(cands[i]));
    }
}

// 인벤토리를 찾는다. "키 옆에 그 개수가 있는가" 로 가른다.
//
// 아이템 키만으로는 못 가린다. 마스터 표·도감·상점 목록·제작 재료·UI
// 캐시가 전부 키를 담고 있어, 힙을 훑으면 한 키가 수십 곳에서 나온다.
// **개수가 붙어 있는 구조는 사실상 인벤토리뿐이다** - 도감에는 개수가
// 없고 마스터 표에는 최대 스택만 있다.
//
// 게다가 서로 다른 (키,개수) 쌍이 한 덩어리 안에 둘 이상 모여 있으면
// 우연일 수 없다. 인벤토리 칸은 이웃해 있기 때문이다.
void cmd_invfind(const Remote& r, int argc, char** argv) {
    struct Want { std::uint32_t key; std::uint32_t count; };
    std::vector<Want> want;
    for (int i = 2; i < argc; ++i) {
        const char* colon = std::strchr(argv[i], ':');
        if (colon == nullptr) continue;
        want.push_back({static_cast<std::uint32_t>(std::strtoul(argv[i], nullptr, 0)),
                        static_cast<std::uint32_t>(std::strtoul(colon + 1, nullptr, 0))});
    }
    if (want.empty()) {
        std::printf("사용법: invfind <키:개수> [키:개수 ...]\n");
        return;
    }
    std::printf("찾는 것:");
    for (const auto& w : want) std::printf(" %u x%u", w.key, w.count);
    std::printf("\n\n");

    constexpr std::size_t kNear = 0x40;    // 키에서 개수까지 볼 거리
    constexpr std::size_t kGroup = 0x800;  // 한 덩어리로 볼 범위

    std::vector<std::uint8_t> buf;
    // (주소, 어느 want 인지)
    std::vector<std::pair<std::uintptr_t, std::size_t>> hits;
    for (const auto& reg : r.regions()) {
        if (!reg.writable || reg.is_image) continue;
        if (reg.size == 0 || reg.size > (512u << 20)) continue;
        buf.resize(reg.size);
        if (!r.read(reg.base, buf.data(), buf.size())) continue;
        for (std::size_t i = 0; i + 4 <= buf.size(); i += 4) {
            std::uint32_t v = 0;
            std::memcpy(&v, buf.data() + i, 4);
            for (std::size_t w = 0; w < want.size(); ++w) {
                if (v != want[w].key) continue;
                // 개수가 근처에 있는지 본다.
                const std::size_t lo = (i > kNear) ? i - kNear : 0;
                const std::size_t hi = (i + kNear + 4 <= buf.size())
                                           ? i + kNear : buf.size() - 4;
                // 개수가 u32 라는 보장이 없다. u8·u16 도 본다 - 스택
                // 상한이 20 대라 한 바이트로 충분하기 때문이다.
                // 'near' 는 windows.h 의 레거시 매크로라 쓰지 않는다.
                bool count_nearby = false;
                const std::uint32_t c32 = want[w].count;
                for (std::size_t j = lo; j <= hi && !count_nearby; ++j) {
                    if (buf[j] == static_cast<std::uint8_t>(c32) &&
                        c32 <= 0xFF) {
                        count_nearby = true;
                        break;
                    }
                    if (j + 2 <= buf.size()) {
                        std::uint16_t c16 = 0;
                        std::memcpy(&c16, buf.data() + j, 2);
                        if (c16 == c32) count_nearby = true;
                    }
                }
                if (count_nearby) hits.emplace_back(reg.base + i, w);
            }
        }
    }

    std::printf("키+개수가 같이 있는 곳 %zu 곳\n", hits.size());
    for (const auto& h : hits) {
        std::printf("  0x%llX  키 %u x%u\n",
                    static_cast<unsigned long long>(h.first),
                    want[h.second].key, want[h.second].count);
    }
    std::printf("\n");
    // 서로 다른 아이템이 한 덩어리에 모인 곳을 찾는다.
    std::sort(hits.begin(), hits.end());
    for (std::size_t i = 0; i < hits.size(); ++i) {
        std::size_t j = i;
        std::vector<bool> kinds(want.size(), false);
        while (j < hits.size() && hits[j].first - hits[i].first < kGroup) {
            kinds[hits[j].second] = true;
            ++j;
        }
        std::size_t n = 0;
        for (const bool b : kinds) n += b ? 1 : 0;
        if (n >= 2) {
            std::printf("  0x%llX ~ 0x%llX  서로 다른 아이템 %zu종 (%zu개 일치)\n",
                        static_cast<unsigned long long>(hits[i].first),
                        static_cast<unsigned long long>(hits[j - 1].first), n,
                        j - i);
            i = j - 1;
        }
    }
}

// 살아 있는 인벤토리 컴포넌트 중 플레이어 것을 가려낸다. 전부 읽기다.
//
// 판정 근거는 "아이템 표에 실재하는 키가 얼마나 들어 있는가" 다.
// 인스턴스가 여럿이라 주소만 보고 고를 수 없고, 잘못 고른 채로
// 쓰기로 넘어가면 무엇이 깨지는지도 모르게 된다.
void cmd_inv(const mem::Rtti& rt, const mem::Reader& reader, int argc,
             char** argv) {
    std::uintptr_t mgr = 0;
    if (!game::find_item_manager(rt, reader, &mgr)) {
        std::printf("ItemInfoManager 를 찾지 못했습니다.\n");
        return;
    }
    game::LocSystem sys;
    game::find_loc_system(rt, reader, &sys);
    std::vector<game::ItemCatalogEntry> items;
    if (!game::build_item_catalog(reader, mgr, sys, &items)) {
        std::printf("아이템 표를 읽지 못했습니다.\n");
        return;
    }
    std::map<std::uint32_t, const game::ItemCatalogEntry*> by_key;
    for (const auto& e : items) by_key[e.key] = &e;

    // 찾을 키를 주면 그것만 본다. 안 주면 작은 값을 걸러낸다 -
    // 2 나 101 같은 값은 우연히 유효 키라서 어디서나 걸린다.
    std::vector<std::uint32_t> want;
    for (int i = 2; i < argc; ++i) {
        const unsigned long v = std::strtoul(argv[i], nullptr, 0);
        if (v != 0) want.push_back(static_cast<std::uint32_t>(v));
    }
    const char* cls = ".?AVClientInventoryActorComponent@pa@@";
    if (!want.empty()) {
        std::printf("찾는 키:");
        for (const auto k : want) std::printf(" %u", k);
        std::printf("\n");
    }
    const auto insts = rt.instances_of_class(cls, 32);
    std::printf("%s\n인스턴스 %zu개\n\n", cls, insts.size());

    // 객체 자체와, 객체가 가리키는 곳 한 겹까지 본다. 아이템 목록이
    // 객체 안에 박혀 있을 수도, 따로 할당돼 있을 수도 있다.
    constexpr std::size_t kSelf = 0x200;
    constexpr std::size_t kDeep = 0x1000;
    for (const auto base : insts) {
        std::vector<std::uint8_t> self(kSelf);
        if (!reader.read(base, self.data(), self.size())) continue;

        struct Hit { std::uintptr_t at; std::uint32_t key; };
        std::vector<Hit> hits;
        auto scan = [&](const std::vector<std::uint8_t>& buf,
                        std::uintptr_t origin) {
            for (std::size_t i = 0; i + 4 <= buf.size(); i += 4) {
                std::uint32_t v = 0;
                std::memcpy(&v, buf.data() + i, 4);
                if (!want.empty()) {
                    if (std::find(want.begin(), want.end(), v) == want.end()) {
                        continue;
                    }
                } else {
                    // 작은 값은 어디서나 우연히 걸린다.
                    if (v < 1000 || v > 0x7FFFFFFF) continue;
                    if (by_key.find(v) == by_key.end()) continue;
                }
                if (hits.size() < 12) hits.push_back({origin + i, v});
            }
        };
        scan(self, base);

        std::size_t deep_hits = 0;
        for (std::size_t i = 0; i + 8 <= self.size(); i += 8) {
            std::uint64_t p = 0;
            std::memcpy(&p, self.data() + i, 8);
            if (p < 0x10000 || (p & 7) != 0) continue;
            std::vector<std::uint8_t> deep(kDeep);
            if (!reader.read(static_cast<std::uintptr_t>(p), deep.data(),
                             deep.size())) {
                continue;
            }
            const std::size_t before = hits.size();
            scan(deep, static_cast<std::uintptr_t>(p));
            deep_hits += hits.size() - before;
        }

        std::uint32_t a = 0, b = 0;
        reader.read_value(base + 0x20, &a);
        reader.read_value(base + 0x24, &b);
        std::printf("0x%llX  +0x20=%u/%u  아이템키 %zu개 (그중 포인터 너머 %zu)\n",
                    static_cast<unsigned long long>(base), a, b, hits.size(),
                    deep_hits);
        for (const auto& h : hits) {
            const auto* e = by_key[h.key];
            std::printf("    0x%llX  %-9u %s\n",
                        static_cast<unsigned long long>(h.at), h.key,
                        (e && !e->name.empty()) ? e->name.c_str() : "(이름 없음)");
        }
    }
}

// 탈것·용병·캐릭터 카탈로그를 낸다. 모드와 같은 함수를 돌려 배포 전
// 검증한다. roster [탈것|용병|캐릭터] [최대개수]
// 살아 있는 액터를 걷어 캐릭터 이름을 붙인다 (모드의 근처 탭과 같은 코드).
// 내가 가진 동반자 명부. 전부 읽기다.
//
// 용병단 컴포넌트의 레코드 배열을 걷는다(game/clan.h).
void cmd_clan(mem::Rtti& rt, const mem::Reader& reader, int argc, char** argv) {
    if (!game::discover_roster(rt, reader)) {
        std::printf("로스터(캐릭터 표)를 못 찾았습니다 - 이름 없이 행 번호만 냅니다.\n");
    }
    const bool want_client = (argc > 2 && std::strcmp(argv[2], "client") == 0);
    std::uintptr_t clan = 0;
    const bool found = want_client
                           ? game::find_clan_component_client(reader, rt, &clan)
                           : game::find_clan_component(reader, rt, &clan);
    std::printf("%s 명부\n",
                want_client ? "클라이언트" : "서버");
    if (!found) {
        std::printf("용병단 컴포넌트를 못 찾았습니다 (월드 밖?).\n");
        return;
    }
    std::vector<game::ClanEntry> list;
    if (!game::read_clan_roster(reader, clan, &list)) {
        std::printf("명부를 읽지 못했습니다.\n");
        return;
    }
    std::size_t spawned = 0;
    for (const auto& e : list) if (e.spawned()) ++spawned;
    std::printf("용병단 0x%llX - 동반자 %zu명, 그중 월드에 %zu명\n",
                static_cast<unsigned long long>(clan), list.size(), spawned);
    for (const auto& e : list) {
        std::string owner;
        if (e.owner_row != 0xFFFF) {
            owner = "  [" + (e.owner_name.empty()
                                 ? ("행 " + std::to_string(e.owner_row))
                                 : e.owner_name) + "]";
        }
        std::printf("  번호 %8llu  행 %5u  키 %6u  타입행 %2d  %-42s %s%s%s\n",
                    static_cast<unsigned long long>(e.merc_no), e.row, e.key,
                    e.merc_row == 0xFFFF ? -1 : static_cast<int>(e.merc_row),
                    e.name.empty() ? "(이름 없음)" : e.name.c_str(),
                    e.label.empty() ? "" : e.label.c_str(),
                    e.spawned() ? "  [월드]" : "", owner.c_str());
    }
    // 소유자 갈래를 세어 둔다. 플레이어블(클리프·데미안·웅카…) 개인
    // 소유인지, 개인 임자가 없는 용병대 몫인지, NPC 개인 것인지.
    std::size_t own_playable = 0, own_npc = 0, own_none = 0;
    for (const auto& e : list) {
        if (e.owner_row == 0xFFFF) ++own_none;
        else if (e.owner_playable) ++own_playable;
        else ++own_npc;
    }
    std::printf("소유: 플레이어블 %zu · 용병대(임자 없음) %zu · NPC %zu\n",
                own_playable, own_none, own_npc);
}

// 플레이어블 캐릭터를 가릴 수 있는지 본다. MercenaryInfo 의
// `_isPlayable`(+0x22) 이 후보다.
void cmd_playable(const mem::Rtti& rt, const mem::Reader& reader) {
    if (!game::discover_roster(rt, reader)) { std::printf("로스터 실패\n"); return; }
    std::printf("지금 조종 중인 캐릭터 행: %u\n",
                game::main_character_row(reader));
    std::printf("용병 표 (_isPlayable):\n");
    for (const auto& m : game::mercenary_catalog()) {
        std::printf("  행 %2u  타입 %2u  플레이어블 %d  %s\n", m.row,
                    m.merc_type, m.merc_playable ? 1 : 0, m.name.c_str());
    }
    std::printf("\n플레이어블 타입에 속한 캐릭터:\n");
    for (const auto& e : game::character_catalog()) {
        if (!e.is_companion()) continue;
        if (!game::is_playable_merc_row(e.merc_row)) continue;
        std::printf("  행 %5u  키 %6u  타입행 %2u  %-40s %s\n", e.row, e.key,
                    e.merc_row, e.name.c_str(), e.label.c_str());
    }
}

// 표시 관문 후보들을 전수로 교차 집계한다. "게임 목록에 뜨는 종"의
// 기준이 무엇인지 관찰이 아니라 숫자로 가리려고 만들었다.
void cmd_gate(const mem::Rtti& rt, const mem::Reader& reader) {
    if (!game::discover_roster(rt, reader)) { std::printf("로스터 실패\n"); return; }
    const auto& cat = game::character_catalog();
    struct Cell { int total = 0; int no_equip = 0; int no_count = 0; };
    std::map<int, Cell> by_type;
    for (const auto& e : cat) {
        if (!e.is_companion()) continue;
        Cell& c = by_type[static_cast<int>(e.merc_row)];
        ++c.total;
        if (e.equip_info == 0xFFFF) ++c.no_equip;
        if (!e.merc_countable) ++c.no_count;
    }
    std::printf("타입행  전체  장비없음  셈안함   이름\n");
    for (const auto& [row, c] : by_type) {
        std::printf("%6d %5d %9d %8d   %s\n", row, c.total, c.no_equip,
                    c.no_count, game::mercenary_type_name(
                        static_cast<std::uint16_t>(row)).c_str());
    }
    // 목록에 올리는 세 타입(1 탈것 / 5 특수 / 9 반려) 안에서 장비
    // 정보가 없는 것들. 지금 기준이 실제로 거르는 것이 이것뿐이다.
    std::printf("\n장비 정보가 없는 1/5/9 종:\n");
    for (const auto& e : cat) {
        if (!e.is_companion() || e.equip_info != 0xFFFF) continue;
        const int t = static_cast<int>(e.merc_row);
        if (t != 1 && t != 5 && t != 9) continue;
        std::printf("  행 %5u  타입행 %2d  %-44s %s\n", e.row, t,
                    e.name.c_str(), e.label.c_str());
    }
}

// 캐릭터를 이름으로 찾는다. **행 번호**를 내는 것이 목적이다 -
// 명부 레코드의 +0x20 과 고용 검사가 쓰는 것이 키가 아니라 행이다.
void cmd_charfind(mem::Rtti& rt, const mem::Reader& reader, int argc,
                  char** argv) {
    if (argc < 3) {
        std::printf("사용법: charfind <이름조각> [최대=40]\n");
        return;
    }
    if (!game::discover_roster(rt, reader)) {
        std::printf("로스터를 못 찾았습니다.\n");
        return;
    }
    const std::string want = ansi_to_utf8(argv[2]);
    const std::size_t cap = (argc > 3) ? std::strtoull(argv[3], nullptr, 10) : 40;
    const auto& cat = game::character_catalog();
    std::size_t n = 0;
    for (const auto& e : cat) {
        if (e.name.find(want) == std::string::npos &&
            e.label.find(want) == std::string::npos) {
            continue;
        }
        if (n++ >= cap) break;
        std::printf("  행 %5u  키 %6u  타입행 %2d  장비 %5u  소유판 %5u  셈 %d  "
                    "%-40s %s\n", e.row, e.key,
                    e.merc_row == 0xFFFF ? -1 : static_cast<int>(e.merc_row),
                    e.equip_info, e.owned_merc_row, e.merc_countable ? 1 : 0,
                    e.name.c_str(), e.label.c_str());
    }
    std::printf("%zu건 (캐릭터 %zu 중)\n", n, cat.size());
}

// 동반자의 종을 바꿔 쓴다. **이것만 쓸 것** - 생 poke 로
// 명부에 쓰면 안 된다.
//
// 2026-09-09 사고: 명부 주소를 읽어 둔 뒤 그 사이에 동반자가 늘어
// 게임이 레코드를 통째로 새로 만들었는데, 그것을 모르고 예전
// 주소에 써서 남의 객체(GameData_GimmickPointData)를 망가뜨렸고
// 게임이 팀겼다. 그래서 이 명령은 쓰기 직전에 번호로 다시
// 찾고, 표식(+0x22==0xFFFF)과 번호(+0x28)를 확인하고, 쓴 뒤 다시 읽어
// 확인한다.
void cmd_setspecies(mem::Rtti& rt, const mem::Reader& reader, const Remote& r,
                    int argc, char** argv) {
    if (argc < 4) {
        std::printf("사용법: setspecies <용병번호> <새 행번호>\n"
                    "  번호는 clan, 행번호는 charfind 로 찾는다.\n");
        return;
    }
    const std::uint64_t no = std::strtoull(argv[2], nullptr, 0);
    const unsigned long row = std::strtoul(argv[3], nullptr, 0);
    if (no == 0 || row > 0xFFFE) { std::printf("인자가 이상하다.\n"); return; }
    const auto want = static_cast<std::uint16_t>(row);

    game::SpeciesWriteTarget t;
    if (!game::resolve_species_write(rt, reader, no, &t)) {
        std::printf("자리를 못 찾았다 (서버 0x%llX, 클라 0x%llX).\n"
                    "  번호가 맞는지, 월드 안인지 볼 것.\n",
                    static_cast<unsigned long long>(t.server),
                    static_cast<unsigned long long>(t.client));
        return;
    }
    std::printf("번호 %llu: 서버 0x%llX(행 %u) · 클라 0x%llX(행 %u) -> 행 %u\n",
                static_cast<unsigned long long>(no),
                static_cast<unsigned long long>(t.server), t.server_row,
                static_cast<unsigned long long>(t.client), t.client_row, want);
    std::uint8_t buf[2] = {static_cast<std::uint8_t>(want & 0xFF),
                           static_cast<std::uint8_t>(want >> 8)};
    if (!r.write(t.server, buf, 2) || !r.write(t.client, buf, 2)) {
        std::printf("쓰기 실패.\n");
        return;
    }
    // 쓴 뒤 반드시 다시 찾아 확인한다. 쓰는 사이에 명부가
    // 재구성되면 엉뚜한 자리에 쓴 것이다.
    game::SpeciesWriteTarget after;
    if (!game::resolve_species_write(rt, reader, no, &after)) {
        std::printf("쓴 뒤 다시 찾지 못했다 - 명부가 바뀌었을 수 있다!\n");
        return;
    }
    std::printf("확인: 서버 행 %u, 클라 행 %u %s\n", after.server_row,
                after.client_row,
                (after.server_row == want && after.client_row == want)
                    ? "- 반영됨"
                    : "- 어깋난다");
}

// 소환 판정을 바로잡는다. 레코드 +0x50 에 남은 죽은 액터 핸들을
// 지운다. 자세한 근거는 game/clan.h 설명.
void cmd_unspawn(mem::Rtti& rt, const mem::Reader& reader, const Remote& r,
                 int argc, char** argv) {
    if (argc < 3) {
        std::printf("사용법: unspawn <용병번호> [read]\n"
                    "  read 를 붙이면 읽기만 한다.\n");
        return;
    }
    const std::uint64_t no = std::strtoull(argv[2], nullptr, 0);
    const bool read_only = (argc > 3 && std::strcmp(argv[3], "read") == 0);
    if (no == 0) { std::printf("번호가 0 이다.\n"); return; }
    game::SpawnFlagTarget t;
    if (!game::resolve_spawn_flag(rt, reader, no, &t)) {
        std::printf("자리를 못 찾았다 (서버 0x%llX, 클라 0x%llX).\n",
                    static_cast<unsigned long long>(t.server),
                    static_cast<unsigned long long>(t.client));
        return;
    }
    std::printf("번호 %llu: 서버 0x%llX 핸들 %08X · 클라 0x%llX 핸들 %08X\n",
                static_cast<unsigned long long>(no),
                static_cast<unsigned long long>(t.server), t.server_handle,
                static_cast<unsigned long long>(t.client), t.client_handle);
    if (read_only) return;
    if (t.server_handle == 0 && t.client_handle == 0) {
        std::printf("이미 0 이라 할 일이 없다.\n");
        return;
    }
    const std::uint32_t zero = 0;
    if (!r.write(t.server, &zero, 4) || !r.write(t.client, &zero, 4)) {
        std::printf("쓰기 실패.\n");
        return;
    }
    game::SpawnFlagTarget after;
    if (game::resolve_spawn_flag(rt, reader, no, &after)) {
        std::printf("확인: 서버 %08X, 클라 %08X %s\n", after.server_handle,
                    after.client_handle,
                    (after.server_handle == 0 && after.client_handle == 0)
                        ? "- 풀렸다"
                        : "- 어깋난다");
    }
}

void cmd_nearby(const mem::Rtti& rt, const mem::Reader& reader, int argc,
                char** argv) {
    const bool all = (argc > 2) && std::strcmp(argv[2], "all") == 0;
    std::uintptr_t mgr = 0;
    if (!game::find_actor_manager(reader, rt, &mgr)) {
        std::printf("액터 매니저를 못 찾았습니다 (월드 밖?).\n");
        return;
    }
    std::printf("액터 매니저 0x%llX\n", static_cast<unsigned long long>(mgr));
    if (!game::discover_roster(rt, reader)) {
        std::printf("로스터(캐릭터 표)를 못 찾았습니다 - 이름 없이 행 번호만 냅니다.\n");
    }
    std::vector<game::LiveActor> list;
    if (!game::snapshot_live_actors(reader, mgr, &list)) {
        std::printf("액터 걷기 실패.\n");
        return;
    }
    std::size_t comp = 0, named = 0;
    for (const auto& a : list) {
        if (a.is_companion()) ++comp;
        if (!a.name.empty()) ++named;
    }
    std::printf("액터 %zu개, 이름 풀림 %zu, 동반자 타입 %zu\n", list.size(), named,
                comp);
    for (const auto& a : list) {
        if (!all && !a.is_companion()) continue;
        char own[32] = "";
        // 임자가 있으면 획득(2338)이 0x97AE29C9 로 거부한다.
        if (a.owned()) std::snprintf(own, sizeof(own), "  [소유 %08X]", a.owner);
        std::printf("  0x%llX  핸들 %08X  행 %5u  키 %6u  타입행 %2d  %s%s%s%s\n",
                    static_cast<unsigned long long>(a.actor), a.handle, a.row, a.key,
                    a.is_companion() ? static_cast<int>(a.merc_row) : -1,
                    a.name.empty() ? "(이름 없음)" : a.name.c_str(),
                    game::roster_is_wild(a.name) ? "  [야생]" : "",
                    a.hirable ? "  [고용가능]" : "", own);
    }
}

void cmd_roster(const mem::Rtti& rt, const mem::Reader& reader, int argc,
                char** argv) {
    struct Tbl {
        const char* arg;
        const char* cls;
    };
    const Tbl tables[] = {
        {"탈것", ".?AVVehicleInfoManager@pa@@"},
        {"용병", ".?AVMercenaryInfoManager@pa@@"},
        {"캐릭터", ".?AVCharacterInfoManager@pa@@"},
    };
    // roster loc [표번호 0=탈것/1=용병/2=캐릭터] - 레코드 후보 오프셋을
    // resolve 에 넣어 한글 표시명의 현지화 키가 어디 있는지 찾는다.
    if (argc > 2 && std::strcmp(argv[2], "loc") == 0) {
        game::LocSystem sys;
        if (!game::find_loc_system(rt, reader, &sys) || !sys.valid()) {
            std::printf("현지화 시스템을 못 찾았습니다.\n");
            return;
        }
        int ti = (argc > 3) ? std::atoi(argv[3]) : 0;
        if (ti < 0 || ti > 2) ti = 0;
        std::uintptr_t mgr = 0;
        if (!game::find_static_manager(reader, rt, tables[ti].cls, &mgr)) {
            std::printf("매니저를 못 찾았습니다.\n");
            return;
        }
        std::uint32_t count = 0;
        std::uintptr_t records = 0;
        if (!game::roster_header(reader, mgr, &count, &records)) return;
        const std::size_t offs[] = {0x08, 0x10, 0x14, 0x18, 0x20, 0x28,
                                    0x30, 0x38, 0x40, 0x48, 0x50, 0x58,
                                    0x60, 0x68, 0x70, 0x78};
        for (std::uint32_t i = 0; i < count && i < 4; ++i) {
            std::uint64_t rec = 0;
            if (!reader.read_value(records + i * 8, &rec) || rec == 0) continue;
            const auto record = static_cast<std::uintptr_t>(rec);
            std::uint64_t so = 0;
            reader.read_value(record + 0x08, &so);
            std::string nm =
                game::read_engine_string(reader, static_cast<std::uintptr_t>(so));
            std::printf("record[%u] 내부이름=%s\n", i, nm.c_str());
            for (std::size_t off : offs) {
                std::uint64_t key = 0;
                if (!reader.read_value(record + off, &key) || key == 0) continue;
                std::string txt;
                if (game::resolve(reader, sys, key, &txt, nullptr) &&
                    !txt.empty()) {
                    std::printf("   +0x%02zX key=%llu -> '%s'\n", off,
                                static_cast<unsigned long long>(key),
                                txt.c_str());
                }
            }
        }
        return;
    }

    const char* want = (argc > 2) ? argv[2] : nullptr;
    const std::size_t show = (argc > 3) ? std::strtoull(argv[3], nullptr, 10)
                                        : 15;
    for (const auto& t : tables) {
        if (want != nullptr && std::strcmp(want, t.arg) != 0) continue;
        std::vector<game::RosterEntry> cat;
        if (!game::build_static_catalog(reader, rt, t.cls, &cat)) {
            std::printf("%s: 매니저를 찾지 못했습니다.\n", t.arg);
            continue;
        }
        std::printf("=== %s: %zu개 ===\n", t.arg, cat.size());
        std::size_t n = 0;
        for (const auto& e : cat) {
            if (n++ >= show) break;
            std::printf("  %-6u  %s\n", e.key,
                        e.name.empty() ? "(이름 없음)" : e.name.c_str());
        }
    }
}

// 아이템 표를 걸어 키와 이름을 낸다. 전부 읽기다.
void cmd_items(const mem::Rtti& rt, const mem::Reader& reader, int argc,
               char** argv) {
    std::uintptr_t mgr = 0;
    if (!game::find_item_manager(rt, reader, &mgr)) {
        std::printf("ItemInfoManager 를 찾지 못했습니다.\n");
        return;
    }
    game::LocSystem sys;
    if (!game::find_loc_system(rt, reader, &sys)) {
        std::printf("현지화 시스템을 찾지 못해 이름 없이 키만 냅니다.\n");
    }

    // 모드가 돌리는 것과 같은 함수다. 배포하기 전에 여기서 결과를
    // 확인할 수 있어야 한다 - camera 명령과 같은 이유다.
    std::vector<game::ItemCatalogEntry> items;
    if (!game::build_item_catalog(reader, mgr, sys, &items)) {
        std::printf("아이템 목록을 만들지 못했습니다. (매니저 0x%llX)\n",
                    static_cast<unsigned long long>(mgr));
        return;
    }
    std::printf("매니저   0x%llX\n", static_cast<unsigned long long>(mgr));
    std::printf("아이템   %zu개\n", items.size());

    // items save <파일>  : 표 전체를 파일로 내린다.
    //
    // 이후 분석은 게임 없이 반복할 수 있다. 등급·분류가 어느 칸인지는
    // 알려진 값과의 상관으로 가려야 하는데, 그때마다 게임을 켤 수는
    // 없다.
    //
    // 형식 (리틀엔디언):
    //   u32 매직 'CDTI', u32 개수, u32 레코드 크기
    //   개수 번 반복: u32 키, u64 이름키, u32 이름길이, 이름, 레코드
    if (argc > 3 && std::strcmp(argv[2], "save") == 0) {
        constexpr std::size_t kRecBytes = 0x500;
        std::vector<game::ItemEntry> raw;
        if (!game::read_item_table(reader, mgr, &raw, 0)) {
            std::printf("표를 읽지 못했습니다\n");
            return;
        }
        std::FILE* f = std::fopen(argv[3], "wb");
        if (f == nullptr) {
            std::printf("파일을 열지 못했습니다: %s\n", argv[3]);
            return;
        }
        const std::uint32_t magic = 0x49544443;   // 'CDTI'
        const std::uint32_t count = static_cast<std::uint32_t>(raw.size());
        const std::uint32_t recsz = static_cast<std::uint32_t>(kRecBytes);
        std::fwrite(&magic, 4, 1, f);
        std::fwrite(&count, 4, 1, f);
        std::fwrite(&recsz, 4, 1, f);

        std::vector<std::uint8_t> rec(kRecBytes);
        std::size_t wrote = 0, failed = 0;
        for (const auto& it : raw) {
            std::string name;
            game::resolve(reader, sys, it.name_key, &name, nullptr);
            if (!reader.read(it.record, rec.data(), rec.size())) {
                std::fill(rec.begin(), rec.end(), 0);
                ++failed;
            }
            const std::uint32_t nlen = static_cast<std::uint32_t>(name.size());
            std::fwrite(&it.key, 4, 1, f);
            std::fwrite(&it.name_key, 8, 1, f);
            std::fwrite(&nlen, 4, 1, f);
            if (nlen != 0) std::fwrite(name.data(), 1, nlen, f);
            std::fwrite(rec.data(), 1, rec.size(), f);
            ++wrote;
        }
        std::fclose(f);
        std::printf("\n%zu개를 %s 에 썼습니다 (레코드 읽기 실패 %zu)\n", wrote,
                    argv[3], failed);
        return;
    }

    // items aux <키>  : 그 아이템의 보조 객체들을 함께 뜬다.
    //
    // 매니저에는 레코드(+0x58) 말고도 아이템별 배열이 더 있다.
    // 분류·등급·가격이 레코드에 없으므로 이쪽을 본다.
    if (argc > 3 && std::strcmp(argv[2], "aux") == 0) {
        const std::uint32_t want =
            static_cast<std::uint32_t>(std::strtoul(argv[3], nullptr, 0));
        std::vector<game::ItemEntry> raw;
        game::read_item_table(reader, mgr, &raw, 0);
        std::size_t idx = raw.size();
        for (std::size_t i = 0; i < raw.size(); ++i) {
            if (raw[i].key == want) { idx = i; break; }
        }
        if (idx == raw.size()) {
            std::printf("키 %u 를 찾지 못했습니다\n", want);
            return;
        }
        std::string name;
        game::resolve(reader, sys, raw[idx].name_key, &name, nullptr);
        std::printf("\n키 %u  색인 %zu  '%s'\n", want, idx, name.c_str());

        struct Aux { const char* label; std::size_t field; std::size_t stride;
                     std::size_t bytes; };
        const Aux auxes[] = {
            {"+0x50 배열", 0x50, 8, 0x50},
            {"+0x80 배열", 0x80, 8, 0x20},
        };
        for (const auto& a : auxes) {
            std::uint64_t base = 0;
            if (!reader.read_value(mgr + a.field, &base) || base == 0) {
                std::printf("\n%s : 비어 있음\n", a.label);
                continue;
            }
            std::uint64_t obj = 0;
            if (!reader.read_value(
                    static_cast<std::uintptr_t>(base) + idx * a.stride, &obj) ||
                obj == 0) {
                std::printf("\n%s : 항목이 비어 있음\n", a.label);
                continue;
            }
            std::printf("\n%s [%zu] -> 0x%llX\n", a.label, idx,
                        static_cast<unsigned long long>(obj));
            std::vector<std::uint32_t> w(a.bytes / 4);
            if (!reader.read(static_cast<std::uintptr_t>(obj), w.data(),
                             w.size() * 4)) {
                std::printf("  읽기 실패\n");
                continue;
            }
            for (std::size_t i = 0; i < w.size(); i += 4) {
                std::printf("  +0x%03zX ", i * 4);
                for (std::size_t k = 0; k < 4 && i + k < w.size(); ++k) {
                    std::printf(" %10u(%08X)", w[i + k], w[i + k]);
                }
                std::printf("\n");
            }
        }
        return;
    }

    // items rec <키> [바이트]  : 레코드 한 개를 u32 격자로 뜬다.
    // 카테고리·등급·가격이 어느 칸인지 찾는 정찰용이다.
    if (argc > 3 && std::strcmp(argv[2], "rec") == 0) {
        const std::uint32_t want =
            static_cast<std::uint32_t>(std::strtoul(argv[3], nullptr, 0));
        const std::size_t bytes =
            (argc > 4) ? std::strtoull(argv[4], nullptr, 0) : 0x100;
        std::vector<game::ItemEntry> raw;
        game::read_item_table(reader, mgr, &raw, 0);
        for (const auto& it : raw) {
            if (it.key != want) continue;
            std::string name;
            game::resolve(reader, sys, it.name_key, &name, nullptr);
            std::printf("\n키 %u  레코드 0x%llX  '%s'\n", it.key,
                        static_cast<unsigned long long>(it.record),
                        name.c_str());
            std::vector<std::uint32_t> w(bytes / 4);
            if (!reader.read(it.record, w.data(), w.size() * 4)) {
                std::printf("레코드를 읽지 못했습니다\n");
                return;
            }
            // 0 만 있는 줄은 건너뛴다. 레코드가 0x500 바이트라
            // 전부 찍으면 읽을 수가 없다.
            std::size_t skipped = 0;
            for (std::size_t i = 0; i < w.size(); i += 4) {
                bool all_zero = true;
                for (std::size_t k = 0; k < 4 && i + k < w.size(); ++k) {
                    if (w[i + k] != 0) all_zero = false;
                }
                if (all_zero) { ++skipped; continue; }
                std::printf("+0x%03zX ", i * 4);
                for (std::size_t k = 0; k < 4 && i + k < w.size(); ++k) {
                    std::printf(" %10u(%08X)", w[i + k], w[i + k]);
                }
                std::printf("\n");
            }
            std::printf("(0 만 있는 줄 %zu개 생략)\n", skipped);
            return;
        }
        std::printf("키 %u 를 표에서 찾지 못했습니다\n", want);
        return;
    }

    // items diff <키A> <키B> [바이트]  : 두 레코드에서 다른 칸만 낸다.
    // 등급·가격·카테고리가 어느 칸인지 좁히는 정찰용이다.
    if (argc > 4 && std::strcmp(argv[2], "diff") == 0) {
        const std::uint32_t ka =
            static_cast<std::uint32_t>(std::strtoul(argv[3], nullptr, 0));
        const std::uint32_t kb =
            static_cast<std::uint32_t>(std::strtoul(argv[4], nullptr, 0));
        const std::size_t bytes =
            (argc > 5) ? std::strtoull(argv[5], nullptr, 0) : 0x500;

        std::vector<game::ItemEntry> raw;
        game::read_item_table(reader, mgr, &raw, 0);
        std::uintptr_t ra = 0, rb = 0;
        std::string na, nb;
        for (const auto& it : raw) {
            if (it.key == ka && ra == 0) {
                ra = it.record;
                game::resolve(reader, sys, it.name_key, &na, nullptr);
            }
            if (it.key == kb && rb == 0) {
                rb = it.record;
                game::resolve(reader, sys, it.name_key, &nb, nullptr);
            }
        }
        if (ra == 0 || rb == 0) {
            std::printf("키를 찾지 못했습니다 (A=%s B=%s)\n",
                        ra ? "있음" : "없음", rb ? "있음" : "없음");
            return;
        }
        std::vector<std::uint32_t> a(bytes / 4), b(bytes / 4);
        if (!reader.read(ra, a.data(), a.size() * 4) ||
            !reader.read(rb, b.data(), b.size() * 4)) {
            std::printf("레코드를 읽지 못했습니다\n");
            return;
        }
        std::printf("\nA %u '%s'\nB %u '%s'\n\n", ka, na.c_str(), kb,
                    nb.c_str());
        std::printf("%-8s %22s %22s\n", "오프셋", "A", "B");
        std::size_t same = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i] == b[i]) { ++same; continue; }
            // 포인터로 보이는 칸은 건너뛴다. 값이 아니라 주소다.
            const bool ptr_like =
                (i + 1 < a.size() && a[i + 1] == 0x2F1 && b[i + 1] == 0x2F1) ||
                (i > 0 && a[i] == b[i] && a[i] > 0x100);
            if (ptr_like) { ++same; continue; }
            std::printf("+0x%03zX  %10u(%08X) %10u(%08X)\n", i * 4, a[i], a[i],
                        b[i], b[i]);
        }
        std::printf("\n같은 칸 %zu / %zu\n", same, a.size());
        return;
    }

    // items hist <오프셋> [폭]  : 그 칸의 값 분포를 6,810개 전체에서 센다.
    // 값 종류가 적으면 등급·분류 같은 열거형이고, 넓게 퍼지면 가격
    // 이나 아이디다. 어느 칸이 무엇인지 좁히는 정찰용이다.
    if (argc > 3 && std::strcmp(argv[2], "hist") == 0) {
        const std::size_t off = std::strtoull(argv[3], nullptr, 0);
        const int width = (argc > 4) ? std::atoi(argv[4]) : 4;
        std::vector<game::ItemEntry> raw;
        if (!game::read_item_table(reader, mgr, &raw, 0)) {
            std::printf("표를 읽지 못했습니다\n");
            return;
        }
        std::map<std::uint64_t, std::size_t> hist;
        std::size_t failed = 0;
        for (const auto& it : raw) {
            std::uint64_t v = 0;
            bool ok = false;
            if (width == 1) { std::uint8_t x = 0; ok = reader.read_value(it.record + off, &x); v = x; }
            else if (width == 2) { std::uint16_t x = 0; ok = reader.read_value(it.record + off, &x); v = x; }
            else if (width == 8) { ok = reader.read_value(it.record + off, &v); }
            else { std::uint32_t x = 0; ok = reader.read_value(it.record + off, &x); v = x; }
            if (!ok) { ++failed; continue; }
            ++hist[v];
        }
        std::printf("\n+0x%zX (%d바이트): 값 종류 %zu개, 읽기 실패 %zu\n\n",
                    off, width, hist.size(), failed);
        std::vector<std::pair<std::uint64_t, std::size_t>> rows(hist.begin(),
                                                                hist.end());
        std::sort(rows.begin(), rows.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        for (std::size_t i = 0; i < rows.size() && i < 24; ++i) {
            std::printf("  %12llu (0x%llX)  x %zu\n",
                        static_cast<unsigned long long>(rows[i].first),
                        static_cast<unsigned long long>(rows[i].first),
                        rows[i].second);
        }
        if (rows.size() > 24) std::printf("  ... 그 외 %zu종\n", rows.size() - 24);
        return;
    }

    // items cats : 부위(분류·장비타입)별로 소켓 상한이 어떻게 깔려 있는지.
    //
    // 부위별 소켓 수를 따로 잡으려면 무엇으로 묶어야 하는지 알아야 한다.
    // 분류(+0xA3)로 대부분 갈리는데 갑옷·망토처럼 한 분류에 여러 장비
    // 타입(+0x42)이 섞이는 자리가 있어 둘 다 낸다.
    if (argc > 2 && std::strcmp(argv[2], "cats") == 0) {
        struct Row {
            std::uint8_t cat = 0;
            std::uint16_t etype = 0;
            std::size_t count = 0;
            std::size_t with_socket = 0;
            std::uint32_t max_cap = 0;
            std::string sample;
        };
        std::vector<Row> rows;
        for (const auto& it : items) {
            if (it.equip_type == 0xFFFF) continue;   // 장비만
            Row* r = nullptr;
            for (auto& x : rows) {
                if (x.cat == it.category && x.etype == it.equip_type) {
                    r = &x;
                    break;
                }
            }
            if (r == nullptr) {
                rows.push_back(Row{it.category, it.equip_type, 0, 0, 0, {}});
                r = &rows.back();
            }
            ++r->count;
            if (it.max_sockets > 0) ++r->with_socket;
            if (it.max_sockets > r->max_cap) r->max_cap = it.max_sockets;
            if (r->sample.empty() && !it.name.empty()) r->sample = it.name;
        }
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            if (a.cat != b.cat) return a.cat < b.cat;
            return a.etype < b.etype;
        });
        std::printf("\n%-5s %-7s %-6s %-8s %-6s %s\n", "분류", "장비타입",
                    "개수", "소켓있음", "상한", "보기");
        for (const auto& r : rows) {
            std::printf("%-5u %-7u %-6zu %-8zu %-6u %s\n", r.cat, r.etype,
                        r.count, r.with_socket, r.max_cap, r.sample.c_str());
        }
        std::printf("\n장비 묶음 %zu개\n", rows.size());
        return;
    }

    // items [최대]  |  items find <문자열>
    std::string needle;
    bool filtering = false;
    std::size_t max = 40;
    if (argc > 2) {
        if (std::strcmp(argv[2], "find") == 0) {
            if (argc < 4) { std::printf("사용법: items find <문자열>\n"); return; }
            needle = ansi_to_utf8(argv[3]);
            filtering = true;
            max = 0;
        } else {
            max = std::strtoull(argv[2], nullptr, 10);
        }
    }

    std::size_t named = 0, shown = 0;
    std::printf("\n%-10s %-20s %s\n", "키", "이름키", "이름");
    for (const auto& it : items) {
        const bool ok = !it.name.empty();
        if (ok) ++named;
        if (filtering) {
            if (!ok || it.name.find(needle) == std::string::npos) continue;
        }
        if (max != 0 && shown >= max) continue;
        ++shown;
        // 새로 읽는 칸들. 없는 아이템에는 안 붙인다.
        char extra[112] = {0};
        int at = 0;
        if (it.max_endurance != 0xFFFF) {
            at += std::snprintf(extra + at, sizeof(extra) - at, "  내구도 %u",
                                it.max_endurance);
        }
        if (it.max_sockets != 0) {
            at += std::snprintf(extra + at, sizeof(extra) - at, "  소켓 %u",
                                it.max_sockets);
        }
        if (it.max_temper != 0) {
            at += std::snprintf(extra + at, sizeof(extra) - at, "  담금질 %u",
                                it.max_temper);
        }
        if (it.repair_entries != 0) {
            at += std::snprintf(extra + at, sizeof(extra) - at, "  수리 %u",
                                it.repair_entries);
        }
        if (it.max_sharpness != 0) {
            std::snprintf(extra + at, sizeof(extra) - at, "  연마 %d",
                          it.max_sharpness);
        }
        std::printf("%-10u %-20llu %s%s\n", it.key,
                    static_cast<unsigned long long>(it.name_key),
                    ok ? it.name.c_str() : "(이름 없음)", extra);
    }
    std::printf("\n이름이 풀린 것 %zu / %zu\n", named, items.size());
    if (max != 0 && items.size() > shown) {
        std::printf("%zu개만 냈습니다. 전부 보려면 개수를 크게 주세요.\n", shown);
    }
}

// 아이템 키 <-> 짧은 식별자 대응표를 읽는다.
//
//   itemmap                표 요약 + 앞 20칸
//   itemmap id <값> ...    짧은 식별자로 아이템을 되찾는다 (역조회)
//   itemmap key <값> ...   아이템 키의 짧은 식별자 (정조회)
//
// export/import 에 필요한 것은 역조회다. 인벤토리 레코드에는 짧은
// 식별자만 있고, 지급은 아이템 키로 한다.
// 패턴 후보를 전부 낸다. 같은 꼴의 조회 코드가 표마다 있어 한 곳만
// 일치하지 않는다 - 실측 34곳이다. 어느 것이 아이템 표인지는 표의
// 개수로 가른다.
void dump_itemmap_candidates(const mem::Rtti& rt, const mem::Reader& reader) {
    const auto rvas = game::find_item_key_map_rvas(rt.image(), 512);
    std::printf("변환 함수 패턴 후보 %zu곳\n", rvas.size());
    for (const auto rva : rvas) {
        const std::uintptr_t g =
            reader.module_base() + static_cast<std::uintptr_t>(rva);
        std::uint64_t obj = 0, slots = 0;
        std::uint32_t cnt = 0, cap = 0;
        reader.read_value(g, &obj);
        if (obj != 0) {
            const std::uintptr_t t = static_cast<std::uintptr_t>(obj) + 0x68;
            reader.read_value(t + 0x04, &cnt);
            reader.read_value(t + 0x08, &cap);
            reader.read_value(t + 0x10, &slots);
        }
        std::printf("  RVA 0x%-8llX 객체 0x%-13llX 개수 %-6u 용량 %-6u 슬롯 0x%llX\n",
                    static_cast<unsigned long long>(rva),
                    static_cast<unsigned long long>(obj), cnt, cap,
                    static_cast<unsigned long long>(slots));
    }
}

// 치트 메시지 하나를 모드와 같은 코드(resolve_cheat_message)로
// 해석해 ID·서술자·처리기 RVA 를 낸다. 배포 전에 어느 치트가
// 새 exe 에서 해석되고 어느 것이 깨졌는지 실측한다.
//
//   cheat <클래스명>
//   cheat                 알려진 치트 여럿을 한 번에
void cmd_cheat_one(const mem::Rtti& rt, const mem::Reader& reader,
                   const char* name) {
    game::CheatMessage m;
    if (!game::resolve_cheat_message(rt, reader, name, &m)) {
        std::printf("  %-40s  해석 실패\n", name);
        return;
    }
    const auto base = reader.module_base();
    std::printf("  %-40s  ID %-5u  서술자 RVA 0x%llX  처리기 RVA 0x%llX\n",
                name, m.id,
                static_cast<unsigned long long>(m.descriptor - base),
                static_cast<unsigned long long>(m.handler - base));
}

void cmd_cheat(const mem::Rtti& rt, const mem::Reader& reader, int argc,
               char** argv) {
    if (argc > 2) {
        cmd_cheat_one(rt, reader, argv[2]);
        return;
    }
    // 인자가 없으면 관심 치트를 한 번에 - 무엇이 깨졌는지 한눈에.
    static const char* kKnown[] = {
        "CreateItemFromTrItemValueCheatReq",
        "SpawnItemToGroundByCheatReq",
        "SpawnCharacterCheatReq",
        "VaryEnduranceItemByCheatReq",
        "VaryStatCheatReq",
    };
    for (const char* n : kKnown) cmd_cheat_one(rt, reader, n);
}

void cmd_equip(const mem::Rtti& rt, const mem::Reader& reader, int argc,
               char** argv) {
    if (argc > 2 && std::strcmp(argv[2], "diag") == 0) {
        // 모든 장비 컴포넌트를 클래스·조각수·플레이어신호와 함께 나열한다.
        const auto objs = rt.find_objects("EquipSlotActorComponent", 128);
        std::printf("EquipSlotActorComponent: %zu\n", objs.size());
        for (const auto& o : objs) {
            game::EquipTable t;
            if (!game::find_equip_table(reader, o.address, &t)) continue;
            t.comp = o.address;
            std::vector<game::WornPiece> ps;
            if (!game::read_worn_gear(reader, t, &ps) || ps.size() < 3) continue;
            std::uint64_t ch = 0;
            reader.read_value(o.address + 0x08, &ch);
            const std::uintptr_t arr =
                game::player_gauge_array(reader, ch);
            std::int32_t hpmax = 0;
            if (arr) reader.read_value(arr + 0x18, &hpmax);
            const bool isp = game::char_is_player(reader, ch);
            std::printf("  [%s] comp=0x%llX pieces=%zu char=0x%llX gauge=%s "
                        "hpmax=%d player=%d\n",
                        o.cls.c_str(), (unsigned long long)o.address, ps.size(),
                        (unsigned long long)ch, arr ? "Y" : "N", hpmax, isp);
        }
        return;
    }
    if (argc > 2) {
        const std::uintptr_t comp =
            std::strtoull(argv[2], nullptr, 16);
        std::printf("comp=0x%llX (direct)\n", (unsigned long long)comp);
        game::EquipTable t;
        if (!game::find_equip_table(reader, comp, &t)) {
            std::printf("equip table NOT found\n");
            return;
        }
        std::printf("equip table arr=0x%llX cnt=%u stride=0x%X\n",
                    (unsigned long long)t.arr, t.cnt, t.stride);
        std::vector<game::WornPiece> ps;
        game::read_worn_gear(reader, t, &ps);
        std::printf("worn pieces: %zu\n", ps.size());
        for (const auto& w : ps) {
            std::printf("  inst=0x%llX key=%u refine=%u slot=%u unlocked=%d  sock:",
                        (unsigned long long)w.instance, w.key, w.refine,
                        w.slot_tag, w.unlocked);
            for (int k = 0; k < 5; ++k)
                std::printf(" [%u m%X i%02X]", w.sockets[k].gem,
                            w.sockets[k].marker, w.sockets[k].index);
            std::printf("\n");
        }
        return;
    }
    std::vector<game::EquipTable> tabs;
    const int nt = game::collect_equip_tables(rt, reader, &tabs);
    std::printf("valid equip tables: %d\n", nt);
    game::EquipTable t;
    std::vector<game::WornPiece> ps;
    if (!game::read_player_worn(rt, reader, &t, &ps)) {
        std::printf("player worn gear NOT found\n");
        return;
    }
    std::printf("player table arr=0x%llX cnt=%u stride=0x%X  pieces=%zu\n",
                (unsigned long long)t.arr, t.cnt, t.stride, ps.size());
    for (const auto& w : ps) {
        std::printf("  inst=0x%llX key=%u refine=%u slot=%u unlocked=%d  sock:",
                    (unsigned long long)w.instance, w.key, w.refine,
                    w.slot_tag, w.unlocked);
        for (int k = 0; k < 5; ++k)
            std::printf(" [%u m%X i%02X]", w.sockets[k].gem,
                        w.sockets[k].marker, w.sockets[k].index);
        std::printf("\n");
    }
}

// B-1 검증: 스탯 블록 후보를 덤프한다. 플레이어 것은 값이 캐릭터 시트
// (x1000 이라 여기 값 ÷1000)와 맞는 블록이다. 인자로 액터 주소를 주면
// 그 액터만 스캔한다.
// status 컴포넌트(및 그 포인터 1단계)에서 주어진 값을 int32/float/x1000 으로
// 찾는다. 시트 수치를 넣어 오프셋을 못박는 용도.
void scan_status_for_value(const mem::Rtti& rt, const mem::Reader& reader,
                           int val) {
    const auto objs = rt.find_objects("StatusActorComponent", 512);
    std::printf("=== 값 %d 스캔 (StatusActorComponent %zu개) ===\n", val,
                objs.size());
    int hits = 0;
    for (const auto& o : objs) {
        for (std::size_t off = 0; off <= 0xC00 && hits < 60; off += 4) {
            std::int32_t iv = 0;
            float fv = 0.0f;
            if (reader.read_value(o.address + off, &iv)) {
                if (iv == val || iv == val * 1000) {
                    std::printf("  [%s] 0x%llX +0x%03zX = %d (int)\n",
                                o.cls.c_str(), (unsigned long long)o.address,
                                off, iv);
                    ++hits;
                }
            }
            if (reader.read_value(o.address + off, &fv)) {
                if (fv == static_cast<float>(val)) {
                    std::printf("  [%s] 0x%llX +0x%03zX = %.1f (float)\n",
                                o.cls.c_str(), (unsigned long long)o.address,
                                off, fv);
                    ++hits;
                }
            }
        }
    }
    std::printf("총 %d 히트\n", hits);
}

// 주어진 주소의 int32(및 float)를 훑어 그럴듯한 스탯 값을 덤프한다. 이미지
// 로드가 필요 없어 빠르다.
void dump_status_ints(const mem::Reader& reader, std::uintptr_t base) {
    std::printf("=== 0x%llX int32 덤프 (1..3,000,000) ===\n",
                (unsigned long long)base);
    for (std::size_t off = 0; off <= 0xC00; off += 4) {
        std::int32_t iv = 0;
        if (!reader.read_value(base + off, &iv)) continue;
        if (iv >= 1 && iv <= 3000000) {
            float fv = 0.0f;
            reader.read_value(base + off, &fv);
            std::printf("  +0x%03zX = %d\n", off, iv);
        }
    }
}

void cmd_player(const mem::Rtti& rt, const mem::Reader& reader, const Remote& r,
                int argc, char** argv) {
    // player find : 실제 discovery(스티키·조각최다) 를 돌려 결과를 본다.
    if (argc > 2 && std::strcmp(argv[2], "find") == 0) {
        // 프로브에서는 equip 캐시가 비어 있으므로 equip_discover 로 채운 뒤
        // player_discover(값싼, equip_player_comp 사용)를 부른다.
        game::equip_discover(rt, reader);
        game::player_discover(reader);
        const auto v = game::player_vitals(reader);
        std::printf("player_char=0x%llX  HP %d/%d STA %d/%d SPI %d/%d ok=%d\n",
                    (unsigned long long)game::player_char(), v.hp_cur, v.hp_max,
                    v.sta_cur, v.sta_max, v.spi_cur, v.spi_max, v.ok);
        // 게이지 배열의 HP(entry0)·스태미나(entry12) 전체 i64 필드. 피해 전후
        // diff 로 어느 필드가 줄어드는지(=live HP) 특정한다.
        const std::uintptr_t ch = game::player_char();
        const std::uintptr_t arr = game::player_gauge_array(reader, ch);
        if (arr) {
            auto dumpe = [&](const char* nm, std::uintptr_t e) {
                std::printf("  %s i64:", nm);
                for (std::size_t o = 0x08; o <= 0x38; o += 8) {
                    std::int64_t x = 0;
                    reader.read_value(e + o, &x);
                    std::printf(" +%zX=%lld", o, (long long)x);
                }
                std::printf("\n");
            };
            dumpe("HP(e0) ", arr);
            dumpe("STA(e12)", arr + 12 * 0x90);
        }
        return;
    }
    // player scan <값>  : 시트 수치로 오프셋 탐색
    if (argc > 3 && std::strcmp(argv[2], "scan") == 0) {
        scan_status_for_value(rt, reader, std::atoi(argv[3]));
        return;
    }
    // player dump <addr> [addr2...] : 주소들의 int32 덤프
    if (argc > 3 && std::strcmp(argv[2], "dump") == 0) {
        for (int i = 3; i < argc; ++i)
            dump_status_ints(reader, std::strtoull(argv[i], nullptr, 16));
        return;
    }
    // player status : 플레이어 액터가 가리키는 StatusActorComponent 를 찾아 덤프.
    if (argc > 2 && std::strcmp(argv[2], "status") == 0) {
        game::EquipTable pt;
        std::vector<game::WornPiece> ps;
        if (!game::read_player_worn(rt, reader, &pt, &ps)) {
            std::printf("플레이어 장비 테이블 못 찾음\n");
            return;
        }
        std::uint64_t actor = 0;
        reader.read_value(pt.comp + 0x08, &actor);
        std::printf("player actor=0x%llX\n", (unsigned long long)actor);
        // 액터가 가리키는 포인터 중 클래스명에 Status 가 든 것을 찾는다.
        for (std::size_t off = 0; off <= 0x800; off += 8) {
            std::uint64_t p = 0;
            if (!reader.read_value(actor + off, &p)) continue;
            if (p < 0x100000000ULL) continue;
            const std::string cls = rt.class_of_object(p);
            if (cls.find("Status") == std::string::npos) continue;
            std::printf("  actor+0x%03zX -> [%s] 0x%llX\n", off, cls.c_str(),
                        (unsigned long long)p);
            // 이 컴포넌트의 cur/max 쌍(8 또는 0x10 간격)을 덤프한다.
            for (std::size_t o = 0; o <= 0x900; o += 4) {
                std::int32_t cu = 0, m8 = 0, m10 = 0;
                reader.read_value(p + o, &cu);
                reader.read_value(p + o + 8, &m8);
                reader.read_value(p + o + 0x10, &m10);
                if (cu >= 1 && cu <= 3000000) {
                    if (m8 >= cu && m8 <= 3000000 && m8 - cu <= m8)
                        std::printf("      +0x%03zX %d / +8 %d\n", o, cu, m8);
                    else if (m10 >= cu && m10 <= 3000000)
                        std::printf("      +0x%03zX %d / +10 %d\n", o, cu, m10);
                }
            }
        }
        return;
    }
    // player maxes <a> <b> <c> : 세 최대값(안 변함)이 가까이 모인 구조체를 찾는다.
    // 재생성으로 현재값 스캔이 막혀도, 최대값들은 고정이라 스탯 블록을 짚는다.
    if (argc > 4 && std::strcmp(argv[2], "maxes") == 0) {
        const std::int32_t A = std::atoi(argv[3]);
        const std::int32_t B = std::atoi(argv[4]);
        const std::int32_t C = (argc > 5) ? std::atoi(argv[5]) : 0;
        const auto regs = r.regions();
        std::printf("=== %d & %d & %d 가 0x200 내 모인 곳 ===\n", A, B, C);
        std::vector<std::uint8_t> buf;
        int hits = 0;
        for (const auto& reg : regs) {
            if (!reg.writable || reg.is_image) continue;
            if (reg.size == 0 || reg.size > (512u << 20)) continue;
            buf.resize(reg.size);
            if (!r.read(reg.base, buf.data(), buf.size())) continue;
            const std::size_t win = 0x200;
            for (std::size_t i = 0; i + 4 <= buf.size(); i += 4) {
                std::int32_t v = 0;
                std::memcpy(&v, buf.data() + i, 4);
                if (v != A) continue;
                // 근처에서 B, C 를 찾는다.
                bool hasB = false, hasC = (C == 0);
                const std::size_t lo = (i > win) ? i - win : 0;
                const std::size_t hi =
                    (i + win < buf.size()) ? i + win : buf.size() - 4;
                for (std::size_t j = lo; j <= hi; j += 4) {
                    std::int32_t w = 0;
                    std::memcpy(&w, buf.data() + j, 4);
                    if (w == B) hasB = true;
                    if (C != 0 && w == C) hasC = true;
                }
                if (hasB && hasC) {
                    std::printf("  0x%llX (%d 위치)\n",
                                (unsigned long long)(reg.base + i), A);
                    if (++hits >= 40) { std::printf("(멈춤)\n"); return; }
                }
            }
        }
        std::printf("%d곳\n", hits);
        return;
    }
    // player hpwatch : 체인 영역(actor+0x68+0x20+0x18) 안에서 HP 변화 diff.
    //   1회차: 스냅샷 저장. 2회차(HP 바꾼 뒤): 값이 바뀐 오프셋만 = HP 후보.
    if (argc > 2 && std::strcmp(argv[2], "hpwatch") == 0) {
        game::EquipTable pt;
        std::vector<game::WornPiece> ps;
        if (!game::read_player_worn(rt, reader, &pt, &ps)) {
            std::printf("플레이어 장비 테이블 못 찾음\n");
            return;
        }
        std::uint64_t actor = 0, s1 = 0, s2 = 0, base = 0;
        reader.read_value(pt.comp + 0x08, &actor);
        reader.read_value(actor + 0x68, &s1);
        reader.read_value(s1 + 0x20, &s2);
        reader.read_value(s2 + 0x18, &base);
        std::printf("actor=0x%llX base=0x%llX\n", (unsigned long long)actor,
                    (unsigned long long)base);
        if (base < 0x100000000ULL) {
            std::printf("체인 무효\n");
            return;
        }
        const char* path = "hpwatch.txt";
        // 이전 스냅샷 로드: "offset value" 행들.
        std::vector<std::pair<std::size_t, std::int32_t>> prev;
        if (std::FILE* f = std::fopen(path, "r")) {
            unsigned long long o = 0;
            long long v = 0;
            while (std::fscanf(f, "%llx %lld", &o, &v) == 2)
                prev.emplace_back(static_cast<std::size_t>(o),
                                  static_cast<std::int32_t>(v));
            std::fclose(f);
        }
        if (prev.empty()) {
            // 스냅샷: HP 처럼 보이는 값(1..3,000,000)을 전부 저장.
            std::FILE* f = std::fopen(path, "w");
            int n = 0;
            for (std::size_t o = 0; o <= 0x800; o += 4) {
                std::int32_t v = 0;
                if (!reader.read_value(base + o, &v)) continue;
                if (v >= 1 && v <= 3000000) {
                    if (f) std::fprintf(f, "%zX %d\n", o, v);
                    ++n;
                }
            }
            if (f) std::fclose(f);
            std::printf("스냅샷 %d개 저장. 이제 게임에서 HP 를 바꾼 뒤 다시 실행.\n",
                        n);
        } else {
            std::printf("바뀐 오프셋(HP 후보):\n");
            for (const auto& kv : prev) {
                std::int32_t v = 0;
                if (!reader.read_value(base + kv.first, &v)) continue;
                if (v != kv.second && v >= 1 && v <= 3000000)
                    std::printf("  +0x%03zX  %d -> %d\n", kv.first, kv.second,
                                v);
            }
            std::remove(path);
            std::printf("(끝. 다시 하려면 한 번 더 실행해 새 스냅샷)\n");
        }
        return;
    }
    // player chain : CT 검증 게이지 배열을 라이브 확인.
    //   char+0x68 actor, +0x20 marker, +0x18 root, +0x58 array, 0x90 stride.
    //   entry: +0x00 i32 type, +0x08 i64 cur, +0x30 i64 cap. entry[0] 타입==0(체력).
    if (argc > 2 && std::strcmp(argv[2], "chain") == 0) {
        game::EquipTable pt;
        std::vector<game::WornPiece> ps;
        if (!game::read_player_worn(rt, reader, &pt, &ps)) {
            std::printf("플레이어 장비 테이블 못 찾음\n");
            return;
        }
        auto q = [&](std::uint64_t a, std::size_t o) {
            std::uint64_t v = 0;
            reader.read_value(a + o, &v);
            return v;
        };
        std::uint64_t ch = 0;
        reader.read_value(pt.comp + 0x08, &ch);
        const std::uint64_t actor = q(ch, 0x68);
        const std::uint64_t mark = q(actor, 0x20);
        const std::uint64_t root = q(mark, 0x18);
        const std::uint64_t arr = q(root, 0x58);
        std::printf("char=0x%llX actor=0x%llX mark=0x%llX root=0x%llX arr=0x%llX\n",
                    (unsigned long long)ch, (unsigned long long)actor,
                    (unsigned long long)mark, (unsigned long long)root,
                    (unsigned long long)arr);
        std::int32_t t0 = -1;
        reader.read_value(arr + 0x00, &t0);
        std::printf("entry[0] type=%d %s\n", t0,
                    t0 == 0 ? "(Health 게이트 통과)" : "(체력 아님 - 체인 의심)");
        // CDR 평면 오프셋(godmode 대상) 검증: cur/max.
        auto pr = [&](const char* nm, std::size_t oc, std::size_t om) {
            std::int32_t c = 0, m = 0;
            reader.read_value(arr + oc, &c);
            reader.read_value(arr + om, &m);
            std::printf("  %s: cur(+0x%zX)=%d  max(+0x%zX)=%d\n", nm, oc, c, om,
                        m);
        };
        pr("HP  ", 0x08, 0x18);
        pr("STA ", 0x6C8, 0x6D8);
        pr("SPI ", 0x758, 0x768);
        // 엔트리[0](체력) 전체 필드(i64) - 피해가 어느 필드를 줄이는지 본다.
        //   +0x00 type +0x08 cur +0x18 base +0x20 norm +0x28 floor +0x30 cap
        std::printf("entry[0] raw i64:");
        for (std::size_t o = 0x08; o <= 0x38; o += 8) {
            std::int64_t v = 0;
            reader.read_value(arr + o, &v);
            std::printf(" +%zX=%lld", o, (long long)v);
        }
        std::printf("\n");
        auto tyname = [](int t) {
            switch (t) {
                case 0: return "Health";
                case 17: return "HEAT(핀금지)";
                case 18: return "COMBUSTION(핀금지)";
                case 19: return "MOUNT gallop/flight";
                case 20: return "sprint stamina";
                case 21: return "spirit pool";
                case 22: return "stamina pool";
                case 23: return "spirit variant";
                case 48: return "MOUNT fire(탈것만)";
                default: return "";
            }
        };
        for (int k = 0; k < 64; ++k) {
            const std::uint64_t e = arr + static_cast<std::uint64_t>(k) * 0x90;
            std::int32_t ty = 0;
            std::int64_t cur = 0, cap = 0;
            reader.read_value(e + 0x00, &ty);
            reader.read_value(e + 0x08, &cur);
            reader.read_value(e + 0x30, &cap);
            if (ty < 0 || ty > 4096) continue;
            if (k > 0 && ty == 0 && cur == 0 && cap == 0) continue;
            std::printf("  [%2d] type=%-3d cur=%-10lld cap=%-10lld  %s\n", k, ty,
                        (long long)cur, (long long)cap, tyname(ty));
        }
        return;
    }
    // 기본: 플레이어 액터를 equip 앵커로 얻어 status 컴포넌트를 덤프(진단).
    game::EquipTable pt;
    std::vector<game::WornPiece> ps;
    if (!game::read_player_worn(rt, reader, &pt, &ps)) {
        std::printf("플레이어 장비 테이블 못 찾음\n");
        return;
    }
    std::uint64_t pactor = 0;
    reader.read_value(pt.comp + 0x08, &pactor);
    std::printf("플레이어 actor=0x%llX\n", (unsigned long long)pactor);

    // StatusActorComponent 중 백참조(+0x00..+0x40)가 플레이어 액터인 것을 찾는다.
    const auto objs = rt.find_objects("StatusActorComponent", 64);
    std::uintptr_t pcomp = 0;
    for (const auto& o : objs) {
        for (std::size_t bo = 0; bo <= 0x40; bo += 8) {
            std::uint64_t v = 0;
            if (reader.read_value(o.address + bo, &v) && v == pactor) {
                std::printf("플레이어 status=[%s] 0x%llX (백참조 +0x%zX)\n",
                            o.cls.c_str(), (unsigned long long)o.address, bo);
                if (pcomp == 0) pcomp = o.address;
            }
        }
    }
    if (pcomp == 0) {
        std::printf("액터 백참조로 status 컴포넌트를 못 찾음. 앞 3개 덤프:\n");
        for (int i = 0; i < 3 && i < static_cast<int>(objs.size()); ++i)
            std::printf("  [%s] 0x%llX\n", objs[i].cls.c_str(),
                        (unsigned long long)objs[i].address);
        return;
    }

    // 컴포넌트의 int32 를 훑어 그럴듯한 (cur,max) 자원 쌍을 찾는다.
    std::printf("--- status 0x%llX int32 스캔 (cur<=max, 0<max<5e7) ---\n",
                (unsigned long long)pcomp);
    for (std::size_t off = 0; off <= 0xA00; off += 4) {
        std::int32_t cu = 0, mx = 0;
        if (!reader.read_value(pcomp + off, &cu)) continue;
        if (!reader.read_value(pcomp + off + 0x10, &mx)) continue;
        if (mx > 0 && mx < 50000000 && cu >= 0 && cu <= mx && mx >= 10) {
            std::printf("  +0x%03zX  %d / (+0x10) %d\n", off, cu, mx);
        }
    }
    std::printf("체력/스태미나/정신력을 시트 값과 맞춰 오프셋을 확정하세요.\n");
}

void cmd_itemmap(const mem::Rtti& rt, const mem::Reader& reader, int argc,
                 char** argv) {
    if (argc > 2 && std::strcmp(argv[2], "cand") == 0) {
        dump_itemmap_candidates(rt, reader);
        return;
    }

    // 이름을 붙이고 후보를 가려내려면 아이템 표가 먼저 있어야 한다 -
    // 대응표는 표의 개수(실측 6,810)로 찾는다.
    std::vector<game::ItemCatalogEntry> items;
    std::uintptr_t mgr = 0;
    if (game::find_item_manager(rt, reader, &mgr)) {
        game::LocSystem sys;
        game::find_loc_system(rt, reader, &sys);
        game::build_item_catalog(reader, mgr, sys, &items);
    }
    if (items.empty()) {
        std::printf("아이템 표를 읽지 못했습니다. 개수를 몰라 대응표를\n"
                    "가려낼 수 없습니다.\n");
        return;
    }
    std::printf("아이템 표 %zu개\n", items.size());

    const auto icount = static_cast<std::uint32_t>(items.size());
    game::ItemKeyMap m;
    bool got = game::find_item_key_map_from_manager(reader, mgr, icount, &m);
    if (got) {
        std::printf("매니저+0x68 에서 즉시 찾음 (객체 0x%llX)\n",
                    static_cast<unsigned long long>(m.object));
    }
    if (!got && !game::find_item_key_map(reader, rt.image(), icount, &m)) {
        std::printf("패턴으로 못 찾음 - 힙 스캔으로 재시도합니다.\n");
        std::vector<std::uint32_t> keys;
        keys.reserve(items.size());
        for (const auto& it : items) keys.push_back(it.key);
        std::sort(keys.begin(), keys.end());
        if (!game::find_item_key_map_by_scan(
                reader, static_cast<std::uint32_t>(items.size()), keys, &m)) {
            std::printf("힙 스캔으로도 못 찾았습니다. 후보를 냅니다.\n\n");
            dump_itemmap_candidates(rt, reader);
            return;
        }
        std::printf("힙 스캔 성공: 객체 0x%llX\n",
                    static_cast<unsigned long long>(m.object));
    }
    std::vector<game::ItemKeyPair> pairs;
    if (!game::read_item_key_map(reader, m, &pairs)) {
        std::printf("슬롯 배열을 읽지 못했습니다 (0x%llX, %u칸).\n",
                    static_cast<unsigned long long>(m.slots), m.capacity);
        return;
    }

    if (m.global != 0) {
        std::printf("전역     0x%llX  (RVA 0x%llX)\n",
                    static_cast<unsigned long long>(m.global),
                    static_cast<unsigned long long>(m.global -
                                                    reader.module_base()));
    } else {
        std::printf("전역     (없음 - 힙 스캔으로 객체를 직접 찾음)\n");
    }
    std::printf("객체     0x%llX\n", static_cast<unsigned long long>(m.object));
    std::printf("표       0x%llX   개수 %u / 용량 %u\n",
                static_cast<unsigned long long>(m.table), m.count, m.capacity);
    std::printf("슬롯     0x%llX\n", static_cast<unsigned long long>(m.slots));
    std::printf("채워진 칸 %zu개\n", pairs.size());

    std::map<std::uint32_t, const game::ItemCatalogEntry*> by_key;
    for (const auto& it : items) by_key[it.key] = &it;

    auto name_of = [&](std::uint32_t key) -> const char* {
        const auto it = by_key.find(key);
        if (it == by_key.end()) return "(아이템 표에 없음)";
        return it->second->name.empty() ? "(이름 없음)"
                                        : it->second->name.c_str();
    };

    // 역조회가 유일해야 export/import 가 성립한다. 실제로 그런지 센다.
    std::map<std::uint32_t, std::uint32_t> by_id;   // 순번 -> 아이템 키
    std::map<std::uint32_t, std::uint32_t> fwd;     // 아이템 키 -> 순번
    std::size_t dup_id = 0, dup_key = 0, missing = 0;
    std::uint32_t max_id = 0;
    for (const auto& p : pairs) {
        if (!by_id.emplace(p.id, p.key).second) ++dup_id;
        if (!fwd.emplace(p.key, p.id).second) ++dup_key;
        if (by_key.find(p.key) == by_key.end()) ++missing;
        if (p.id > max_id) max_id = p.id;
    }
    std::printf("순번 최대 %u, 중복 %zu건\n", max_id, dup_id);
    std::printf("아이템 키 중복 %zu건, 아이템 표에 없는 키 %zu건\n",
                dup_key, missing);

    // 대응표의 순서가 아이템 표(ItemInfoManager)의 순서와 같은지 전수로
    // 본다. 같다면 순번은 곧 목록에서의 위치라 별도 표 없이도 풀린다.
    std::size_t order_diff = 0;
    std::size_t first_diff = 0;
    const std::size_t n = pairs.size() < items.size() ? pairs.size()
                                                     : items.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (pairs[i].key == items[i].key) continue;
        if (order_diff == 0) first_diff = i;
        ++order_diff;
    }
    if (pairs.size() != items.size()) {
        std::printf("아이템 표와 개수가 다릅니다 (%zu / %zu)\n", pairs.size(),
                    items.size());
    }
    if (order_diff == 0) {
        std::printf("아이템 표와 순서가 %zu칸 전부 같습니다\n", n);
    } else {
        std::printf("아이템 표와 순서가 다른 칸 %zu개 (처음 %zu번)\n",
                    order_diff, first_diff);
    }

    // itemmap id <값> ...   역조회
    if (argc > 3 && std::strcmp(argv[2], "id") == 0) {
        std::printf("\n%-8s %-11s %s\n", "순번", "아이템키", "이름");
        for (int i = 3; i < argc; ++i) {
            const std::uint32_t id = static_cast<std::uint32_t>(
                std::strtoul(argv[i], nullptr, 10));
            const auto it = by_id.find(id);
            if (it == by_id.end()) {
                std::printf("%-8u %-11s %s\n", id, "-", "(대응표에 없음)");
                continue;
            }
            std::printf("%-8u %-11u %s\n", id, it->second,
                        name_of(it->second));
        }
        return;
    }

    // itemmap key <값> ...  정조회
    if (argc > 3 && std::strcmp(argv[2], "key") == 0) {
        std::printf("\n%-11s %-8s %s\n", "아이템키", "순번", "이름");
        for (int i = 3; i < argc; ++i) {
            const std::uint32_t key = static_cast<std::uint32_t>(
                std::strtoul(argv[i], nullptr, 10));
            const auto it = fwd.find(key);
            if (it == fwd.end()) {
                std::printf("%-11u %-8s %s\n", key, "-", "(대응표에 없음)");
                continue;
            }
            std::printf("%-11u %-8u %s\n", key, it->second, name_of(key));
        }
        return;
    }

    std::size_t max = 20;
    if (argc > 2) max = std::strtoull(argv[2], nullptr, 10);
    std::printf("\n%-8s %-11s %s\n", "순번", "아이템키", "이름");
    for (std::size_t i = 0; i < pairs.size() && i < max; ++i) {
        std::printf("%-8u %-11u %s\n", pairs[i].id, pairs[i].key,
                    name_of(pairs[i].key));
    }
}

void cmd_info(const Remote& r) {
    std::printf("PID          %lu\n", r.pid());
    std::printf("모듈 베이스  0x%llX\n",
                static_cast<unsigned long long>(r.module_base()));
    std::printf("모듈 크기    %.1f MB\n",
                static_cast<double>(r.module_size()) / (1024.0 * 1024.0));
}

void cmd_regions(const Remote& r) {
    const auto regs = r.regions();
    std::size_t total = 0, wr = 0, heap = 0;
    for (const auto& x : regs) {
        total += x.size;
        if (x.writable) wr += x.size;
        if (x.writable && !x.is_image) heap += x.size;
    }
    std::printf("영역 %zu개\n", regs.size());
    std::printf("  전체        %.1f MB\n", total / 1048576.0);
    std::printf("  쓰기가능    %.1f MB\n", wr / 1048576.0);
    std::printf("  힙(비이미지) %.1f MB\n", heap / 1048576.0);
}

void cmd_types(mem::Rtti& rt, const char* needle, std::size_t max) {
    const auto found = rt.find_types(needle, max);
    std::printf("일치 %zu개\n", found.size());
    for (const auto& t : found) {
        std::printf("  0x%llX  %s\n",
                    static_cast<unsigned long long>(t.descriptor),
                    t.name.c_str());
    }
}

// 정확한 이름 하나를 고른다. 여러 개면 첫 번째를 쓰고 경고한다.
bool resolve_one(mem::Rtti& rt, const char* name, mem::Rtti::TypeInfo* out) {
    auto found = rt.find_types(name, 64);
    if (found.empty()) {
        std::printf("클래스를 찾지 못했습니다: %s\n", name);
        return false;
    }
    // 완전 일치를 우선한다.
    for (const auto& t : found) {
        if (t.name == name) { *out = t; return true; }
    }
    if (found.size() > 1) {
        std::printf("부분 일치 %zu개 - 첫 번째를 씁니다\n", found.size());
        for (std::size_t i = 0; i < found.size() && i < 8; ++i) {
            std::printf("    %s\n", found[i].name.c_str());
        }
    }
    *out = found[0];
    return true;
}

void cmd_vtable(mem::Rtti& rt, const char* name) {
    mem::Rtti::TypeInfo ti;
    if (!resolve_one(rt, name, &ti)) return;
    std::printf("%s\n  TypeDescriptor 0x%llX\n", ti.name.c_str(),
                static_cast<unsigned long long>(ti.descriptor));

    const auto vts = rt.vtables_for(ti.descriptor);
    if (vts.empty()) {
        std::printf("  vtable을 찾지 못했습니다 (추상 클래스이거나 "
                    "COL 레이아웃이 다릅니다)\n");
        return;
    }
    for (const auto v : vts) {
        std::printf("  vtable 0x%llX\n", static_cast<unsigned long long>(v));
    }
}

void cmd_instances(mem::Rtti& rt, const Remote& r, const char* name,
                   std::size_t max) {
    mem::Rtti::TypeInfo ti;
    if (!resolve_one(rt, name, &ti)) return;
    const auto vts = rt.vtables_for(ti.descriptor);
    if (vts.empty()) {
        std::printf("vtable이 없어 인스턴스를 찾을 수 없습니다\n");
        return;
    }
    std::printf("%s  vtable %zu개\n", ti.name.c_str(), vts.size());
    for (const auto v : vts) {
        const auto inst = rt.instances_of_class(ti.name, max);
        std::printf("  vtable 0x%llX -> 인스턴스 %zu개\n",
                    static_cast<unsigned long long>(v), inst.size());
        for (std::size_t i = 0; i < inst.size() && i < max; ++i) {
            std::printf("    0x%llX\n",
                        static_cast<unsigned long long>(inst[i]));
        }
    }
    (void)r;
}

// 이름에 조각이 든 클래스들의 인스턴스 개수를 한 번에 센다.
//
// instances 는 클래스 하나뿐이라, 붙을 때마다 350MB 이미지를 다시
// 읽는 비용이 클래스 수만큼 곱해진다. 지역 이동 전후를 비교하려면
// 같은 측정을 두 번 해야 하므로 그 비용이 두 배가 된다.
//
// 개수만 찍는다. 무엇이 늘었는지 보려는 것이지 주소가 필요한 게
// 아니다(동반자 등록 조사, 2026-09-06).
void cmd_instcount(mem::Rtti& rt, int argc, char** argv) {
    if (argc < 3) {
        std::printf("사용법: instcount <이름조각> [이름조각 ...]\n");
        return;
    }
    for (int i = 2; i < argc; ++i) {
        const auto types = rt.find_types(argv[i], 400);
        std::printf("\n=== \"%s\" 에 걸린 클래스 %zu개\n", argv[i], types.size());
        for (const auto& t : types) {
            const auto inst = rt.instances_of_class(t.name, 4000);
            if (inst.empty()) continue;   // 0개는 잡음이라 뺀다
            std::printf("  %6zu  %s\n", inst.size(), t.name.c_str());
        }
    }
}

void cmd_dump(const Remote& r, std::uintptr_t addr, std::size_t n) {
    std::vector<std::uint8_t> buf(n);
    if (!r.read(addr, buf.data(), n)) {
        std::printf("읽기 실패: 0x%llX (%zu 바이트)\n",
                    static_cast<unsigned long long>(addr), n);
        return;
    }
    for (std::size_t i = 0; i < n; i += 16) {
        std::printf("0x%llX  ", static_cast<unsigned long long>(addr + i));
        for (std::size_t k = 0; k < 16; ++k) {
            if (i + k < n) std::printf("%02X ", buf[i + k]);
            else std::printf("   ");
        }
        std::printf(" |");
        for (std::size_t k = 0; k < 16 && i + k < n; ++k) {
            const auto c = buf[i + k];
            std::printf("%c", (c >= 32 && c < 127) ? static_cast<char>(c) : '.');
        }
        std::printf("|\n");
    }
}

void cmd_floats(const Remote& r, std::uintptr_t addr, std::size_t count) {
    std::vector<float> buf(count);
    if (!r.read(addr, buf.data(), count * sizeof(float))) {
        std::printf("읽기 실패: 0x%llX\n",
                    static_cast<unsigned long long>(addr));
        return;
    }
    for (std::size_t i = 0; i < count; i += 4) {
        std::printf("+0x%03zX  ", i * 4);
        for (std::size_t k = 0; k < 4 && i + k < count; ++k) {
            std::printf("%14.4f", buf[i + k]);
        }
        std::printf("\n");
    }
}

void cmd_setf(const Remote& r, std::uintptr_t addr, float v) {
    float before = 0.0f;
    const bool had = r.read_value(addr, &before);
    if (!r.write(addr, &v, sizeof(v))) {
        std::printf("쓰기 실패: 0x%llX\n",
                    static_cast<unsigned long long>(addr));
        return;
    }
    float after = 0.0f;
    r.read_value(addr, &after);
    std::printf("0x%llX  %s%.4f -> 요청 %.4f, 실제 %.4f%s\n",
                static_cast<unsigned long long>(addr), had ? "" : "(읽기실패) ",
                before, v, after,
                (after == v) ? "" : "  [게임이 되돌렸거나 다른 값]");
}

// 바이트를 그대로 쓴다.
//
//   poke <주소> <16진 바이트들>       poke 0x123 24 0D FF FF 00 FF
//
// 게임 함수를 거치지 않는 쓰기다. 되돌릴 수 있도록 **원본을 먼저
// 낸다** - 그 줄을 그대로 다시 주면 되돌아간다.
void cmd_poke(const Remote& r, int argc, char** argv) {
    if (argc < 4) {
        std::printf("사용법: poke <주소> <16진 바이트들>\n");
        return;
    }
    const std::uintptr_t addr = parse_addr(argv[2]);

    // 인자를 이어 붙여 16진만 남긴다. "24 0D" 도 "240D" 도 받는다.
    std::string hex;
    for (int i = 3; i < argc; ++i) {
        for (const char* p = argv[i]; *p != '\0'; ++p) {
            if (*p != ' ' && *p != ',') hex += *p;
        }
    }
    if (hex.empty() || (hex.size() % 2) != 0) {
        std::printf("16진 바이트가 짝이 안 맞습니다: %zu 글자\n", hex.size());
        return;
    }

    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const auto hi = hex_digit(hex[i]);
        const auto lo = hex_digit(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            std::printf("16진이 아닌 글자가 있습니다: %c%c\n", hex[i],
                        hex[i + 1]);
            return;
        }
        bytes.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }

    std::vector<std::uint8_t> before(bytes.size());
    if (!r.read(addr, before.data(), before.size())) {
        std::printf("읽지 못했습니다: 0x%llX\n",
                    static_cast<unsigned long long>(addr));
        return;
    }
    std::printf("원본  ");
    for (const auto b : before) std::printf("%02X ", b);
    std::printf("\n되돌리려면: poke 0x%llX",
                static_cast<unsigned long long>(addr));
    for (const auto b : before) std::printf(" %02X", b);
    std::printf("\n");

    if (!r.write(addr, bytes.data(), bytes.size())) {
        std::printf("쓰기 실패\n");
        return;
    }
    std::vector<std::uint8_t> after(bytes.size());
    r.read(addr, after.data(), after.size());
    std::printf("쓴 뒤  ");
    for (const auto b : after) std::printf("%02X ", b);
    std::printf("%s\n", (after == bytes) ? "" : "  [게임이 되돌렸다]");
}

// 같은 영역을 두 번 읽어 달라진 float 슬롯을 찾는다.
// 게임이 매 프레임 갱신하는 값을 사람 조작 없이 골라낼 수 있다.
void cmd_diff(const Remote& r, std::uintptr_t addr, std::size_t count,
              unsigned wait_ms) {
    std::vector<float> a(count), b(count);
    if (!r.read(addr, a.data(), count * sizeof(float))) {
        std::printf("읽기 실패\n");
        return;
    }
    ::Sleep(wait_ms);
    if (!r.read(addr, b.data(), count * sizeof(float))) {
        std::printf("두 번째 읽기 실패\n");
        return;
    }
    std::size_t changed = 0;
    for (std::size_t i = 0; i < count; ++i) {
        // float 비교로는 안 된다. NaN은 자기 자신과도 같지 않아
        // 포인터를 float으로 읽은 슬롯이 전부 오탐으로 잡힌다.
        std::uint32_t ba = 0, bb = 0;
        std::memcpy(&ba, &a[i], 4);
        std::memcpy(&bb, &b[i], 4);
        if (ba == bb) continue;
        ++changed;
        std::printf("+0x%03zX  %14.4f -> %14.4f\n", i * 4, a[i], b[i]);
    }
    std::printf("변한 슬롯 %zu / %zu (%u ms 간격)\n", changed, count, wait_ms);
}

// 메모리 전체에서 주어진 float3 와 일치하는 자리를 찾는다.
//
// 카메라 좌표는 여러 곳에 복제돼 있다. 어느 복사본이 렌더를
// 구동하는지 가리려면 먼저 전부 찾아야 한다. 컴포넌트+0x360 에
// 우리 값을 써 넣어도 화면이 안 변한 것이 이 명령이 필요해진
// 이유다 - 그 칸은 렌더가 읽는 값이 아니었다.
void cmd_findvec3(const Remote& r, float x, float y, float z, float eps,
                  std::size_t max_hits) {
    std::vector<std::uint8_t> buf;
    std::size_t scanned = 0, hits = 0;

    for (const auto& reg : r.regions()) {
        if (reg.size == 0 || reg.size > (1u << 30)) continue;
        buf.resize(reg.size);
        if (!r.read(reg.base, buf.data(), reg.size)) continue;
        scanned += reg.size;

        // 4바이트 정렬만 본다. 좌표는 정렬된 float 이고,
        // 정렬을 가정하면 후보와 시간이 모두 1/4 로 준다.
        for (std::size_t i = 0; i + 12 <= reg.size; i += 4) {
            float v[3];
            std::memcpy(v, buf.data() + i, 12);
            // NaN 을 먼저 걸러야 한다. fabs(NaN - x) > eps 는 거짓이라
            // 비교만으로는 전부 통과해 버린다. 포인터를 float 으로
            // 읽은 자리가 죄다 오탐으로 잡힌다.
            if (!std::isfinite(v[0]) || !std::isfinite(v[1]) ||
                !std::isfinite(v[2])) {
                continue;
            }
            if (std::fabs(v[0] - x) > eps) continue;
            if (std::fabs(v[1] - y) > eps) continue;
            if (std::fabs(v[2] - z) > eps) continue;
            std::printf("0x%llX  (%.3f, %.3f, %.3f)  %s%s\n",
                        static_cast<unsigned long long>(reg.base + i),
                        v[0], v[1], v[2],
                        reg.writable ? "쓰기가능" : "읽기전용",
                        reg.is_image ? " 이미지" : "");
            if (++hits >= max_hits) {
                std::printf("(최대 %zu개에서 멈춤)\n", max_hits);
                return;
            }
        }
    }
    std::printf("일치 %zu곳 / %.1f MB 훑음 (오차 %.3f)\n", hits,
                static_cast<double>(scanned) / 1048576.0, eps);
}

// 카메라의 변환 행렬을 찾는다.
//
// 회전 행렬은 각 행이 단위벡터이고 서로 직교한다. 메모리에서
// 이 조건을 만족하는 4x4 를 찾고, 그 이동 성분이 알고 있는
// 카메라 좌표 근처인 것만 남기면 후보가 몇 개로 준다.
// 컴포넌트+0x360 을 우리 값으로 고정해도 화면이 안 변했으므로,
// 렌더가 읽는 것은 그 칸이 아니라 이런 행렬이다.
void cmd_findmat(const Remote& r, float x, float y, float z, float eps,
                 std::size_t max_hits) {
    auto dot = [](const float* a, const float* b) {
        return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
    };
    std::vector<std::uint8_t> buf;
    std::size_t scanned = 0, hits = 0;

    for (const auto& reg : r.regions()) {
        if (reg.size == 0 || reg.size > (1u << 30)) continue;
        buf.resize(reg.size);
        if (!r.read(reg.base, buf.data(), reg.size)) continue;
        scanned += reg.size;

        for (std::size_t i = 0; i + 64 <= reg.size; i += 4) {
            float m[16];
            std::memcpy(m, buf.data() + i, 64);

            // 이동 성분이 먼저다. 가장 싸게 걸러낸다.
            // 행우선(12,13,14) 과 열우선(3,7,11) 을 모두 본다.
            const float* t = nullptr;
            const char* order = nullptr;
            if (std::isfinite(m[12]) && std::fabs(m[12]-x) <= eps &&
                std::isfinite(m[13]) && std::fabs(m[13]-y) <= eps &&
                std::isfinite(m[14]) && std::fabs(m[14]-z) <= eps) {
                t = m + 12; order = "행우선";
            } else {
                continue;
            }

            // 상단 3x3 이 정규직교인가.
            const float* rows[3] = {m, m + 4, m + 8};
            bool ok = true;
            for (int k = 0; k < 3 && ok; ++k) {
                for (int c = 0; c < 3; ++c) {
                    if (!std::isfinite(rows[k][c])) { ok = false; break; }
                }
                if (!ok) break;
                const float len = dot(rows[k], rows[k]);
                if (std::fabs(len - 1.0f) > 0.02f) ok = false;
            }
            if (ok) {
                if (std::fabs(dot(rows[0], rows[1])) > 0.02f) ok = false;
                if (std::fabs(dot(rows[0], rows[2])) > 0.02f) ok = false;
                if (std::fabs(dot(rows[1], rows[2])) > 0.02f) ok = false;
            }
            if (!ok) continue;

            std::printf("0x%llX  %s  위치(%.2f, %.2f, %.2f)  %s\n",
                        static_cast<unsigned long long>(reg.base + i),
                        order, t[0], t[1], t[2],
                        reg.writable ? "쓰기가능" : "읽기전용");
            std::printf("    앞 (%+.3f %+.3f %+.3f)  우 (%+.3f %+.3f %+.3f)  "
                        "상 (%+.3f %+.3f %+.3f)\n",
                        m[8], m[9], m[10], m[0], m[1], m[2],
                        m[4], m[5], m[6]);
            if (++hits >= max_hits) {
                std::printf("(최대 %zu개에서 멈춤)\n", max_hits);
                return;
            }
        }
    }
    std::printf("행렬 %zu개 / %.1f MB 훑음 (오차 %.1f)\n", hits,
                static_cast<double>(scanned) / 1048576.0, eps);
}

void cmd_whatis(mem::Rtti& rt, std::uintptr_t addr) {
    const auto name = rt.class_of_object(addr);
    if (name.empty()) {
        std::printf("0x%llX  (RTTI로 식별되지 않음)\n",
                    static_cast<unsigned long long>(addr));
    } else {
        std::printf("0x%llX  %s\n", static_cast<unsigned long long>(addr),
                    name.c_str());
    }
}

// 객체의 8바이트 슬롯을 훑어, 포인터면 그 대상 클래스를 함께 보여준다.
// 구조체 안에 무엇이 들어 있는지 한눈에 파악하는 용도다.
void cmd_fields(mem::Rtti& rt, const Remote& r, std::uintptr_t addr,
                std::size_t slots) {
    std::vector<std::uint64_t> buf(slots);
    if (!r.read(addr, buf.data(), slots * 8)) {
        std::printf("읽기 실패\n");
        return;
    }
    for (std::size_t i = 0; i < slots; ++i) {
        const std::uint64_t v = buf[i];
        std::printf("+0x%03zX  %016llX", i * 8,
                    static_cast<unsigned long long>(v));

        // float 두 개로도 해석해 준다.
        float f0, f1;
        std::memcpy(&f0, reinterpret_cast<const char*>(&v), 4);
        std::memcpy(&f1, reinterpret_cast<const char*>(&v) + 4, 4);
        std::printf("  %12.4f %12.4f", f0, f1);

        if (v > 0x10000 && v < 0x7FFFFFFFFFFF) {
            const auto cls = rt.class_of_object(static_cast<std::uintptr_t>(v));
            if (!cls.empty()) std::printf("  -> %s", cls.c_str());
            else if (i == 0) {
                const auto own = rt.class_of_vtable(
                    static_cast<std::uintptr_t>(v));
                if (!own.empty()) std::printf("  [vtable] %s", own.c_str());
            }
        }
        std::printf("\n");
    }
}

// 프로세스가 실제로 무언가 하고 있는지 잰다.
//
// "값이 안 변한다"는 관측은 두 가지를 뜻할 수 있다 - 우리가 엉뚱한
// 곳을 보고 있거나, 게임이 아예 멈춰 있거나. 둘을 구분하지 않으면
// 잘못된 결론으로 간다.
void cmd_activity(const Remote& r, unsigned wait_ms, std::size_t max_mb) {
    auto regs = r.regions();
    std::vector<Remote::Region> pick;
    std::size_t budget = max_mb * 1024 * 1024;
    for (const auto& x : regs) {
        if (!x.writable || x.is_image) continue;
        if (x.size < 0x10000 || x.size > (32u << 20)) continue;
        if (x.size > budget) break;
        pick.push_back(x);
        budget -= x.size;
    }

    std::vector<std::vector<std::uint8_t>> before(pick.size());
    for (std::size_t i = 0; i < pick.size(); ++i) {
        before[i].assign(pick[i].size, 0);
        r.read(pick[i].base, before[i].data(), before[i].size());
    }
    ::Sleep(wait_ms);

    std::size_t total = 0, diff = 0, regions_changed = 0;
    std::vector<std::uint8_t> now;
    for (std::size_t i = 0; i < pick.size(); ++i) {
        now.assign(pick[i].size, 0);
        if (!r.read(pick[i].base, now.data(), now.size())) continue;
        std::size_t d = 0;
        for (std::size_t k = 0; k < now.size(); ++k) {
            if (now[k] != before[i][k]) ++d;
        }
        total += now.size();
        diff += d;
        if (d > 0) ++regions_changed;
    }
    std::printf("표본 %zu 영역, %.1f MB, %u ms 간격\n", pick.size(),
                total / 1048576.0, wait_ms);
    std::printf("변한 바이트 %zu (%.4f%%), 변한 영역 %zu\n", diff,
                total ? 100.0 * diff / total : 0.0, regions_changed);
    std::printf("판정: %s\n",
                diff == 0 ? "정지 - 게임이 갱신하지 않고 있다"
                          : "동작 중");
}

// 실행 중인 프로세스의 모듈 이미지를 파일로 뜬다.
//
// 디스크의 실행 파일은 Denuvo 로 싸여 있어 코드가 보이지 않는다.
// 풀린 코드는 프로세스 메모리에만 있으므로 정적 분석은 여기서 뜬
// 파일로 한다. 이미지를 읽는 것 자체는 RTTI 탐색이 늘 하던 일이고,
// 여기서는 저장과 헤더 고치기만 더한다.
//
// 기본은 섹션 헤더를 "파일 오프셋 = RVA" 로 고친 PE 다 - Ghidra 나
// IDA 가 아무 설정 없이 연다. --raw 는 메모리 배치 그대로 낸다
// (평면 바이너리로 열고 베이스를 직접 준다).
void cmd_dumpimage(mem::Rtti& rt, const Remote& r,
                   const mem::Rtti::ImageLoad& stats, int argc, char** argv) {
    bool raw = false;
    const char* path = nullptr;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--raw") == 0) {
            raw = true;
            continue;
        }
        if (path == nullptr) path = argv[i];
    }

    // 이름을 직접 준 경우에만 덮어쓰기를 거절한다. 기본 파일은
    // 언제든 다시 뜰 수 있으므로 막지 않는다.
    if (path != nullptr) {
        if (std::FILE* exists = std::fopen(path, "rb")) {
            std::fclose(exists);
            std::printf("이미 있는 파일입니다: %s\n"
                        "덮어쓰지 않습니다. 다른 이름을 주거나 지우고"
                        " 다시 부르세요.\n",
                        path);
            return;
        }
    } else {
        path = raw ? "cdtb_image.bin" : "cdtb_image.exe";
    }

    std::vector<std::uint8_t> image = rt.take_image();
    if (image.empty()) {
        std::printf("이미지가 비었습니다\n");
        return;
    }

    std::printf("모듈 0x%llX + %zu 바이트 (%.1f MB)\n",
                static_cast<unsigned long long>(r.module_base()), image.size(),
                static_cast<double>(image.size()) / 1048576.0);
    if (stats.failed_chunks > 0) {
        std::printf("  못 읽은 곳 %zu / %zu 청크 (%.2f MB, %.2f%%) - 0 으로"
                    " 남았습니다\n",
                    stats.failed_chunks, stats.chunks,
                    static_cast<double>(stats.failed_bytes) / 1048576.0,
                    stats.chunks ? 100.0 * stats.failed_chunks / stats.chunks
                                 : 0.0);
    } else {
        std::printf("  구멍 없음 - %zu 청크 전부 읽었습니다\n", stats.chunks);
    }

    if (!raw) {
        mem::DumpFixup fx;
        std::string err;
        if (!mem::make_dump_loadable(image.data(), image.size(),
                                     r.module_base(), &fx, &err)) {
            std::printf("PE 헤더를 고치지 못했습니다: %s\n"
                        "--raw 로 메모리 배치 그대로 뜰 수 있습니다.\n",
                        err.c_str());
            return;
        }
        std::printf("ImageBase 0x%llX, 진입점 RVA 0x%X, 섹션 %zu개\n",
                    static_cast<unsigned long long>(fx.image_base),
                    fx.entry_rva, fx.sections.size());
        for (const auto& sc : fx.sections) {
            char attr[4] = {'-', '-', '-', 0};
            if (sc.characteristics & 0x20000000u) attr[0] = 'X';
            if (sc.characteristics & 0x40000000u) attr[1] = 'R';
            if (sc.characteristics & 0x80000000u) attr[2] = 'W';
            std::printf("  %-8s RVA 0x%08X  가상 0x%08X  파일 0x%08X  %s%s\n",
                        sc.name.c_str(), sc.rva, sc.virtual_size, sc.raw_size,
                        attr, sc.truncated ? "  (잘림)" : "");
        }
    }

    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        std::printf("파일을 열지 못했습니다: %s\n", path);
        return;
    }
    const std::size_t wrote = std::fwrite(image.data(), 1, image.size(), f);
    const bool closed = std::fclose(f) == 0;
    if (wrote != image.size() || !closed) {
        std::printf("쓰다 말았습니다 (%zu / %zu 바이트): %s\n", wrote,
                    image.size(), path);
        return;
    }

    std::printf("%s 에 썼습니다 (%.1f MB)\n", path,
                static_cast<double>(image.size()) / 1048576.0);
    if (raw) {
        std::printf("평면 바이너리입니다. x86-64 로 열고 베이스를"
                    " 0x%llX 로 주세요.\n",
                    static_cast<unsigned long long>(r.module_base()));
    } else {
        std::printf("PE 로 열립니다. 섹션과 진입점이 그대로 잡히고"
                    " 주소는 0x%llX 기준입니다.\n",
                    static_cast<unsigned long long>(r.module_base()));
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 1;
    }

    Remote r;
    if (!r.attach(L"CrimsonDesert.exe")) {
        std::printf("붙기 실패: %s\n", r.last_error().c_str());
        return 2;
    }

    const std::string cmd = argv[1];

    if (cmd == "info") { cmd_info(r); return 0; }
    if (cmd == "regions") { cmd_regions(r); return 0; }
    if (cmd == "activity") {
        const unsigned ms = (argc > 2)
                                ? static_cast<unsigned>(std::strtoul(argv[2],
                                                                     nullptr, 10))
                                : 800;
        const std::size_t mb = (argc > 3) ? std::strtoull(argv[3], nullptr, 10)
                                          : 256;
        cmd_activity(r, ms, mb);
        return 0;
    }

    if (cmd == "dump") {
        if (argc < 3) { usage(); return 1; }
        const std::size_t n = (argc > 3) ? std::strtoull(argv[3], nullptr, 10)
                                         : 128;
        cmd_dump(r, parse_addr(argv[2]), n);
        return 0;
    }
    if (cmd == "floats") {
        if (argc < 3) { usage(); return 1; }
        const std::size_t n = (argc > 3) ? std::strtoull(argv[3], nullptr, 10)
                                         : 32;
        cmd_floats(r, parse_addr(argv[2]), n);
        return 0;
    }
    if (cmd == "poke") {
        cmd_poke(r, argc, argv);
        return 0;
    }
    if (cmd == "setf") {
        if (argc < 4) { usage(); return 1; }
        cmd_setf(r, parse_addr(argv[2]),
                 static_cast<float>(std::atof(argv[3])));
        return 0;
    }
    if (cmd == "findmat") {
        if (argc < 5) { usage(); return 1; }
        const float x = static_cast<float>(std::atof(argv[2]));
        const float y = static_cast<float>(std::atof(argv[3]));
        const float z = static_cast<float>(std::atof(argv[4]));
        const float eps = (argc > 5) ? static_cast<float>(std::atof(argv[5]))
                                     : 30.0f;
        const std::size_t mx = (argc > 6)
                                   ? std::strtoull(argv[6], nullptr, 10)
                                   : 40;
        cmd_findmat(r, x, y, z, eps, mx);
        return 0;
    }
    if (cmd == "holdmany") {
        if (argc < 7) { usage(); return 1; }
        const unsigned ms = (argc > 7)
                                ? static_cast<unsigned>(
                                      std::strtoul(argv[7], nullptr, 10))
                                : 6000;
        const std::size_t st = (argc > 8)
                                   ? std::strtoull(argv[8], nullptr, 10)
                                   : 0;
        const std::size_t cn = (argc > 9)
                                   ? std::strtoull(argv[9], nullptr, 10)
                                   : 0;
        cmd_holdmany(r, static_cast<float>(std::atof(argv[2])),
                     static_cast<float>(std::atof(argv[3])),
                     static_cast<float>(std::atof(argv[4])),
                     static_cast<float>(std::atof(argv[5])),
                     static_cast<float>(std::atof(argv[6])), ms, st, cn);
        return 0;
    }
    if (cmd == "findvec3d") {
        if (argc < 5) { usage(); return 1; }
        const double eps = (argc > 5) ? std::atof(argv[5]) : 1.0;
        const std::size_t mx = (argc > 6)
                                   ? std::strtoull(argv[6], nullptr, 10)
                                   : 60;
        cmd_findvec3d(r, std::atof(argv[2]), std::atof(argv[3]),
                      std::atof(argv[4]), eps, mx);
        return 0;
    }
    if (cmd == "hold") {
        if (argc < 6) { usage(); return 1; }
        const unsigned ms = (argc > 6)
                                ? static_cast<unsigned>(
                                      std::strtoul(argv[6], nullptr, 10))
                                : 3000;
        cmd_hold(r, parse_addr(argv[2]),
                 static_cast<float>(std::atof(argv[3])),
                 static_cast<float>(std::atof(argv[4])),
                 static_cast<float>(std::atof(argv[5])), ms);
        return 0;
    }
    if (cmd == "findquat") {
        const unsigned ms = (argc > 2)
                                ? static_cast<unsigned>(
                                      std::strtoul(argv[2], nullptr, 10))
                                : 3000;
        const std::size_t mx = (argc > 3)
                                   ? std::strtoull(argv[3], nullptr, 10)
                                   : 30;
        cmd_findquat(r, ms, mx);
        return 0;
    }
    if (cmd == "findvec3") {
        if (argc < 5) { usage(); return 1; }
        const float x = static_cast<float>(std::atof(argv[2]));
        const float y = static_cast<float>(std::atof(argv[3]));
        const float z = static_cast<float>(std::atof(argv[4]));
        const float eps = (argc > 5) ? static_cast<float>(std::atof(argv[5]))
                                     : 0.5f;
        const std::size_t mx = (argc > 6)
                                   ? std::strtoull(argv[6], nullptr, 10)
                                   : 200;
        cmd_findvec3(r, x, y, z, eps, mx);
        return 0;
    }
    if (cmd == "heapfind") {
        cmd_heapfind(r, argc, argv);
        return 0;
    }
    if (cmd == "heapdump") {
        cmd_heapdump(r, argc, argv);
        return 0;
    }
    if (cmd == "dumpmany") {
        cmd_dumpmany(r, argc, argv);
        return 0;
    }
    if (cmd == "hpscan") {
        cmd_hpscan(r, argc, argv);
        return 0;
    }
    if (cmd == "heapptr") {
        cmd_heapptr(r, argc, argv);
        return 0;
    }
    if (cmd == "scan") {
        cmd_scan(r, argc, argv);
        return 0;
    }
    if (cmd == "invfind") {
        cmd_invfind(r, argc, argv);
        return 0;
    }
    if (cmd == "findu32") {
        if (argc < 3) { usage(); return 1; }
        const std::uint32_t v =
            static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 0));
        const std::size_t mx = (argc > 3) ? std::strtoull(argv[3], nullptr, 10)
                                          : 40;
        cmd_findu32(r, v, mx);
        return 0;
    }
    if (cmd == "diff") {
        if (argc < 3) { usage(); return 1; }
        const std::size_t n = (argc > 3) ? std::strtoull(argv[3], nullptr, 10)
                                         : 64;
        const unsigned ms = (argc > 4)
                                ? static_cast<unsigned>(std::strtoul(argv[4],
                                                                     nullptr, 10))
                                : 500;
        cmd_diff(r, parse_addr(argv[2]), n, ms);
        return 0;
    }

    // 아래 명령들은 모듈 이미지가 필요하다.
    RemoteReader reader(r);
    mem::Rtti rt(reader);
    std::printf("모듈 이미지 로드 중 (%.1f MB)...\n",
                static_cast<double>(r.module_size()) / 1048576.0);
    mem::Rtti::ImageLoad img_stats;
    if (!rt.load_image(&img_stats)) {
        std::printf("이미지 로드 실패\n");
        return 3;
    }

    if (cmd == "dumpimage") {
        cmd_dumpimage(rt, r, img_stats, argc, argv);
        return 0;
    }

    if (cmd == "camera") {
        // 모드가 실행 중에 돌리는 것과 똑같은 탐색이다.
        // 배포하기 전에 여기서 결과를 확인한다.
        cdtb::game::CameraSet cs;
        cdtb::game::discover_with(rt, reader, &cs);
        const struct RowKV { const char* k; std::uintptr_t v; } rows[] = {
            {"manager  ", cs.manager},
            {"freeCam  ", cs.free_cam},
            {"photoCam ", cs.photo_cam},
            {"playerCmp", cs.player_component},
            {"active   ", cs.active},
        };
        for (const auto& kv : rows) {
            std::printf("%s 0x%llX\n", kv.k,
                        static_cast<unsigned long long>(kv.v));
            if (kv.v == 0) continue;
            std::string nm;
            if (cdtb::game::camera_name(reader, kv.v, &nm)) {
                std::printf("           이름 '%s'\n", nm.c_str());
            }
        }
        std::printf("완전한가  %s\n", cs.complete() ? "예" : "아니오");
        return 0;
    }
    if (cmd == "types") {
        if (argc < 3) { usage(); return 1; }
        const std::size_t max = (argc > 3) ? std::strtoull(argv[3], nullptr, 10)
                                           : 40;
        cmd_types(rt, argv[2], max);
        return 0;
    }
    if (cmd == "vtable") {
        if (argc < 3) { usage(); return 1; }
        cmd_vtable(rt, argv[2]);
        return 0;
    }
    if (cmd == "findstr") {
        if (argc < 3) { usage(); return 1; }
        const std::string needle = argv[2];
        const auto& img = rt.image();
        std::size_t shown = 0;
        std::printf("'%s' 위치\n", needle.c_str());
        for (std::size_t i = 0; i + needle.size() + 1 <= img.size(); ++i) {
            if (std::memcmp(img.data() + i, needle.data(), needle.size()) != 0) {
                continue;
            }
            // 널 종단인 것만 (부분 일치 제외)
            if (img[i + needle.size()] != 0) continue;
            // 앞이 널이거나 문자열 시작이어야 독립된 문자열이다.
            if (i > 0 && img[i - 1] != 0) continue;
            std::printf("  0x%llX  (RVA 0x%llX)\n",
                        static_cast<unsigned long long>(r.module_base() + i),
                        static_cast<unsigned long long>(i));
            if (++shown >= 20) break;
        }
        if (shown == 0) std::printf("  찾지 못했습니다\n");
        return 0;
    }
    if (cmd == "refs") {
        if (argc < 3) { usage(); return 1; }
        const std::size_t max = (argc > 3) ? std::strtoull(argv[3], nullptr, 10)
                                           : 30;
        const auto refs = rt.find_refs(parse_addr(argv[2]), max);
        std::printf("이 주소를 담고 있는 곳 %zu개\n", refs.size());
        for (const auto& x : refs) {
            std::printf("  slot 0x%llX", static_cast<unsigned long long>(x.slot));
            if (!x.owner_class.empty()) {
                std::printf("  <- %s + 0x%zX", x.owner_class.c_str(), x.offset);
            } else {
                std::printf("  <- (소유 객체 미식별)");
            }
            std::printf("\n");
        }
        return 0;
    }
    if (cmd == "findptr") {
        if (argc < 3) { usage(); return 1; }
        const std::uint64_t v = std::strtoull(argv[2], nullptr, 16);
        const std::size_t max = (argc > 3) ? std::strtoull(argv[3], nullptr, 10)
                                           : 20;
        const auto hits = rt.find_qword(v, max);
        std::printf("모듈 안에서 0x%llX 를 담은 위치 %zu개\n",
                    static_cast<unsigned long long>(v), hits.size());
        for (const auto h : hits) {
            std::printf("  0x%llX  (RVA 0x%llX)\n",
                        static_cast<unsigned long long>(h),
                        static_cast<unsigned long long>(h - r.module_base()));
        }
        return 0;
    }
    if (cmd == "xref") {
        if (argc < 3) { usage(); return 1; }
        const std::size_t max = (argc > 3) ? std::strtoull(argv[3], nullptr, 10)
                                           : 40;
        const auto refs = rt.find_xrefs(parse_addr(argv[2]), max);
        std::printf("RIP 상대 참조 %zu개\n", refs.size());
        static const char* kReg[16] = {"rax", "rcx", "rdx", "rbx",
                                       "rsp", "rbp", "rsi", "rdi",
                                       "r8",  "r9",  "r10", "r11",
                                       "r12", "r13", "r14", "r15"};
        for (const auto& x : refs) {
            std::printf("  0x%llX  %s %s, [rip+...]\n",
                        static_cast<unsigned long long>(x.at),
                        x.opcode == 0x8B ? "mov" : "lea", kReg[x.reg & 15]);
        }
        return 0;
    }
    if (cmd == "objects") {
        if (argc < 3) { usage(); return 1; }
        const std::size_t max = (argc > 3) ? std::strtoull(argv[3], nullptr, 10)
                                           : 60;
        const auto found = rt.find_objects(argv[2], max);
        std::printf("객체 %zu개\n", found.size());
        for (const auto& f : found) {
            std::printf("  0x%llX  %s\n",
                        static_cast<unsigned long long>(f.address),
                        f.cls.c_str());
        }
        return 0;
    }
    if (cmd == "whatis") {
        if (argc < 3) { usage(); return 1; }
        cmd_whatis(rt, parse_addr(argv[2]));
        return 0;
    }
    if (cmd == "fields") {
        if (argc < 3) { usage(); return 1; }
        const std::size_t n = (argc > 3) ? std::strtoull(argv[3], nullptr, 10)
                                         : 24;
        cmd_fields(rt, r, parse_addr(argv[2]), n);
        return 0;
    }
    if (cmd == "clan") { cmd_clan(rt, reader, argc, argv); return 0; }
    if (cmd == "charfind") { cmd_charfind(rt, reader, argc, argv); return 0; }
    if (cmd == "gate") { cmd_gate(rt, reader); return 0; }
    if (cmd == "playable") { cmd_playable(rt, reader); return 0; }
    if (cmd == "setspecies") { cmd_setspecies(rt, reader, r, argc, argv); return 0; }
    if (cmd == "unspawn") { cmd_unspawn(rt, reader, r, argc, argv); return 0; }
    if (cmd == "instcount") {
        cmd_instcount(rt, argc, argv);
        return 0;
    }
    if (cmd == "instances") {
        if (argc < 3) { usage(); return 1; }
        const std::size_t max = (argc > 3) ? std::strtoull(argv[3], nullptr, 10)
                                           : 20;
        cmd_instances(rt, r, argv[2], max);
        return 0;
    }
    if (cmd == "loc") {
        cmd_loc(rt, reader, r, argc, argv);
        return 0;
    }
    if (cmd == "invexport") {
        cmd_invexport(rt, reader, argc, argv);
        return 0;
    }
    if (cmd == "invlist") {
        cmd_invlist(rt, reader, r, argc, argv);
        return 0;
    }
    if (cmd == "itemvalue") {
        cmd_itemvalue(rt, reader, r, argc, argv);
        return 0;
    }
    if (cmd == "items") {
        cmd_items(rt, reader, argc, argv);
        return 0;
    }
    if (cmd == "nearby") {
        cmd_nearby(rt, reader, argc, argv);
        return 0;
    }
    if (cmd == "roster") {
        cmd_roster(rt, reader, argc, argv);
        return 0;
    }
    if (cmd == "aob") { cmd_aob(rt, reader, argc, argv); return 0; }
    if (cmd == "invsock") { cmd_invsock(rt, reader); return 0; }
    if (cmd == "equip") { cmd_equip(rt, reader, argc, argv); return 0; }
    if (cmd == "player") { cmd_player(rt, reader, r, argc, argv); return 0; }
    if (cmd == "itemmap") {
        cmd_itemmap(rt, reader, argc, argv);
        return 0;
    }
    if (cmd == "inv") {
        cmd_inv(rt, reader, argc, argv);
        return 0;
    }
    if (cmd == "cheat") {
        cmd_cheat(rt, reader, argc, argv);
        return 0;
    }

    usage();
    return 1;
}

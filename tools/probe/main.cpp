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
#include "remote_reader.h"
#include "findquat.h"
#include "game/camera.h"
#include "game/items.h"
#include "game/localization.h"

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
        "\n"
        "주소는 16진(0x 접두 선택)으로 준다.\n");
}

std::string ansi_to_utf8(const char* s);

std::uintptr_t parse_addr(const char* s) {
    return static_cast<std::uintptr_t>(std::strtoull(s, nullptr, 16));
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
                if (std::string_view(s).find(needle) == std::string_view::npos) {
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
        std::printf("\n엔티티 %u (0x%X)\n", ent, ent);
        for (std::uint32_t f = game::kLocFieldName;
             f <= game::kLocFieldName + 1; ++f) {
            const std::uint64_t k = game::loc_key(ent, f);
            std::string text;
            int cat = -1;
            std::printf("  필드 0x%X  키 %llu :  ", f,
                        static_cast<unsigned long long>(k));
            if (game::resolve(reader, sys, k, &text, &cat)) {
                std::printf("[카테고리 %d] '%s'\n", cat, text.c_str());
            } else {
                std::printf("없음\n");
            }
        }
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
        std::printf("%-10u %-20llu %s\n", it.key,
                    static_cast<unsigned long long>(it.name_key),
                    ok ? it.name.c_str() : "(이름 없음)");
    }
    std::printf("\n이름이 풀린 것 %zu / %zu\n", named, items.size());
    if (max != 0 && items.size() > shown) {
        std::printf("%zu개만 냈습니다. 전부 보려면 개수를 크게 주세요.\n", shown);
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
    if (!rt.load_image()) {
        std::printf("이미지 로드 실패\n");
        return 3;
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
    if (cmd == "items") {
        cmd_items(rt, reader, argc, argv);
        return 0;
    }

    usage();
    return 1;
}

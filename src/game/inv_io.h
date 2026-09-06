#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mem/reader.h"

namespace cdtb::game {

// 인벤토리 통째 Export / Import.
//
// 기존 보관함(stash)은 지급 경로로 **새 아이템을 꺼내는** 단방향
// 창고라 소켓 언락·박힌 보석을 되살리지 못했다. 이쪽은 다르다:
// 라이브 인벤토리를 통째로 읽어 파일로 남기고(export), 다시 읽을
// 때는 **이미 있는 아이템을 제자리로 복원**한다(import).
//
// 실측(2026-09-06, [[inventory-inplace-write]]): 인벤토리 레코드는
// 그 자체가 authoritative 라 담금질(+0x0A)·연마(+0x58)·열린 소켓
// 보석을 **단일 쓰기로 한 번만** 써도 저장/리로드까지 살아남는다
// (착용 장비와 달리 both-realms 불필요). 단 **소켓 언락(잠긴 칸
// 열기)은 게임/NPC 전용이라 불가** - 잠긴 칸은 정보만 남기고
// import 는 건드리지 않는다.

// 소켓 한 칸의 상태.
struct InvSocketSnap {
    std::uint32_t slot = 0;   // 소켓 칸 번호 (레코드 +0x60 배열의 k)
    int gem = -1;             // 박힌 보석의 아이템 표 순번. -1 = 빈 칸
    bool locked = false;      // 잠긴 칸(+0x04 == 0xFF). import 는 건너뛴다
};

// 아이템 한 개의 상태.
struct InvItemSnap {
    std::uint16_t kind = 0;         // 컨테이너 종류
    std::uint32_t slot = 0;         // 배열 칸 번호(정보·안정 매칭용)
    std::uint32_t index = 0;        // 아이템 표 순번 = 매칭 키
    std::int64_t count = 1;
    std::uint16_t temper = 0;       // 담금질
    std::uint16_t sharpness = 0;    // 장비 연마
    std::uint32_t endurance = 0xFFFF;  // 현재 내구도(0xFFFF = 없는 아이템)
    std::vector<InvSocketSnap> sockets;  // 열린·잠긴 모두, 슬롯 순
};

// --------------------------------------------------- 순수 직렬화(파일 형식)
//
// 사람이 읽고 손으로 고칠 수 있는 텍스트다. 모르는 줄은 조용히
// 버린다(보관함 파일과 같은 태도).
//
//   # CDToybox inventory export v2
//   [kind 1]
//   순번=6283 qty=1 temper=3 sharp=100 dur=30 sockets=3318:0,FFFF:1,-:2
//
// - dur 은 숫자, 없는 아이템은 `-`.
// - sockets 토큰 = `보석순번:슬롯`. 빈 열린칸 `FFFF:k`, 잠긴칸 `-:k`.
//   소켓이 아예 없으면 `sockets=` 를 생략한다.
std::string inv_serialize(const std::vector<InvItemSnap>& items);
bool inv_parse(const std::string& text, std::vector<InvItemSnap>* out);

// --------------------------------------------------- 라이브 읽기 / 쓰기

// 현재 인벤토리 컴포넌트(discover_inventory 로 캐시된 것)를 통째로
// 읽어 아이템 상태를 모은다. 컴포넌트가 없으면 false.
bool inventory_export(const mem::Reader& reader,
                      std::vector<InvItemSnap>* out);

// import 한 결과 요약. 무엇을 했고 무엇을 건너뛰었는지 화면에 그대로 보인다.
struct ImportResult {
    int matched = 0;         // 인벤토리에서 짝을 찾은 아이템
    int temper_set = 0;      // 담금질을 바꾼 아이템
    int sharp_set = 0;       // 연마를 바꾼 아이템
    int gems_set = 0;        // 채운 보석 칸
    int locked_skipped = 0;  // 잠긴 칸이라 못 채운 보석
    int not_found = 0;       // 인벤토리에 없어 건너뛴 아이템(지급 필요)
    int write_failed = 0;    // 쓰기/검증 실패
};

// 파일에서 읽은 상태를 현재 인벤토리에 제자리 복원한다.
//
// 매칭: (종류, 순번) 이 같은 현재 레코드를 슬롯 순서로 하나씩
// 소비한다. 같은 순번이 여럿이면 파일에 적힌 순서대로 짝짓는다.
// 담금질·연마를 제자리 쓰고, **열린 소켓에만** 보석을 채운다.
// 잠긴 칸·인벤토리에 없는 아이템은 건드리지 않고 결과에만 센다.
bool inventory_import(const mem::Reader& reader,
                      const std::vector<InvItemSnap>& items,
                      ImportResult* result);

}  // namespace cdtb::game

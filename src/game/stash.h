#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cdtb::game {

// 게임 밖에 두는 아이템 보관함.
//
// 인벤토리 창에서 골라 담는 "입고"(stash_add_entry) 와 지급 경로로
// 꺼내는 "출고" 둘 다 된다. 게임의 슬롯 제한과 무관하고 캐릭터를
// 새로 시작해도 남는다.

// 박힌 소켓 한 칸. 빈 칸은 담지 않는다 - 지급으로 새로 만든 아이템은
// 생성자가 빈 칸 기본값을 이미 깔아 둔다.
//
// 뜻을 모르는 네 바이트가 있어 원본을 그대로 들고 있는다
// (docs/superpowers/specs/2026-09-02-inventory.md 의 "소켓" 절).
struct StashSocket {
    std::uint32_t slot = 0;   // 소켓 번호
    std::uint32_t key = 0;    // 박힌 보석의 아이템 키
    std::uint8_t raw[6]{};    // 원본 6바이트
};

// `e` 토큰이 없는 줄. 옛 파일이 그렇고, 그때는 꺼낼 때 최대치를
// 준다 - 안 채우면 부서진 채로 나온다.
inline constexpr std::uint32_t kStashNoEndurance = 0xFFFFFFFFu;

struct StashEntry {
    std::uint32_t key = 0;
    std::int64_t count = 1;
    std::uint32_t temper = 0;              // 담금질

    // 장비 연마. 담금질처럼 0 이 기본이라 0 이면 안 적는다 -
    // 내구도와 다르다(그쪽은 0 이 "부서진" 이라 "없음" 과 갈라야 했다).
    std::uint32_t sharpness = 0;

    // 현재 내구도. kStashNoEndurance 면 파일에 안 적혀 있던 것이다.
    // 내구도가 없는 아이템(레코드 +0x40 이 0xFFFF)은 적지 않는다 -
    // 적어 봐야 뜻이 없다.
    std::uint32_t endurance = kStashNoEndurance;
    std::vector<StashSocket> sockets;      // 박힌 것만
};

struct StashSet {
    std::string name;
    std::vector<StashEntry> items;
};

class Stash {
public:
    // --- 즐겨찾기 ---
    bool is_favorite(std::uint32_t key) const;
    void toggle_favorite(std::uint32_t key);
    const std::vector<std::uint32_t>& favorites() const { return favs_; }

    // --- 세트 ---
    int set_count() const { return static_cast<int>(sets_.size()); }
    StashSet* set_at(int i);
    const StashSet* set_at(int i) const;
    int add_set(const std::string& name);   // 만든 자리 번호
    void remove_set(int i);

    // --- 저장 ---
    //
    // 사람이 읽고 고칠 수 있는 형식으로 쓴다. 깨져도 손으로 고칠 수
    // 있는 편이 낫다. 알 수 없는 줄은 조용히 버린다.
    //
    //   # 주석
    //   fav <키>
    //   set <이름 - 줄 끝까지>
    //   item <키> <개수> [t<담금질>] [s<슬롯>:<보석키>:<원본12hex>]...
    //
    // 담금질과 소켓은 있을 때만 붙는다. 옛 파일(`item <키> <개수>`)이
    // 그대로 읽힌다.
    std::string serialize() const;
    bool parse(const std::string& text);

private:
    std::vector<std::uint32_t> favs_;
    std::vector<StashSet> sets_;
};

}  // namespace cdtb::game

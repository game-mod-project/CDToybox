#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cdtb::game {

// 게임 밖에 두는 아이템 보관함.
//
// 게임의 인벤토리에서 빼 오는 "입고" 는 아직 못 한다 - 인벤토리
// 내용을 읽는 방법을 못 찾았다. 대신 **꺼내는 쪽**은 지급 경로가
// 동작하므로 지금 된다. 원하는 것을 담아 두고 언제든 꺼내는,
// 사실상 단방향 창고다. 게임의 슬롯 제한과 무관하고 캐릭터를
// 새로 시작해도 남는다.

struct StashEntry {
    std::uint32_t key = 0;
    std::int64_t count = 1;
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
    //   item <키> <개수>
    std::string serialize() const;
    bool parse(const std::string& text);

private:
    std::vector<std::uint32_t> favs_;
    std::vector<StashSet> sets_;
};

}  // namespace cdtb::game

// 가방 확장의 **상태 기계** 시험. 산술(plan_bag_expand)은 bag_capacity_tests.cpp 가
// 덮는다. 2026-09-13 리뷰의 치명 4건이 전부 이쪽 - 되돌리기 기록의 승계 규칙, 주소
// 갱신 시점, 자동 재적용의 세대 판정 - 에서 나왔는데 시험이 0줄이었다(리뷰 중대 5).
#include <vector>

#include "game/inventory.h"
#include "harness.h"

using cdtb::game::BagBackup;
using cdtb::game::BagSeen;
using cdtb::game::bag_backup_upsert;
using cdtb::game::bag_restore_already_original;
using cdtb::game::bag_restore_blocked;
using cdtb::game::should_auto_reapply;

namespace {
// 실측 가방(종류 1): 240/190/190/0. 목표 300 을 A 칸에 걸면 300/250/250/0 이 된다.
BagSeen bag_seen(std::uintptr_t addr) {
    BagSeen s;
    s.realm = 0;
    s.kind = 1;
    s.address = addr;
    s.cap = 240;
    s.sum = 190;
    s.a = 190;
    s.b = 0;
    return s;
}

BagBackup applied_bag(std::uintptr_t addr) {
    std::vector<BagBackup> v;
    BagSeen s = bag_seen(addr);
    s.changed = true;
    s.understood = true;
    s.known = true;
    s.want_cap = 300;
    s.want_sum = 250;
    bag_backup_upsert(v, s);
    return v.front();
}
}  // namespace

TEST(bag_backup_keeps_the_first_original_and_follows_the_address) {
    // 인벤토리가 새로 생기면 주소는 바뀌지만 realm·종류는 그대로다. 주소만 갈아
    // 끼우고 **최초** 원본은 지켜야 한다 - 안 그러면 자동 재적용이 "이미 확장된
    // 상태" 를 원본으로 삼아, 되돌리기가 240 이 아니라 300 으로 간다.
    std::vector<BagBackup> v;
    BagSeen first = bag_seen(0x1000);
    first.changed = true;
    first.known = true;
    first.want_cap = 300;
    first.want_sum = 250;
    bag_backup_upsert(v, first);
    CHECK(v.size() == 1);
    CHECK(v[0].cap == 240);
    CHECK(v[0].address == 0x1000);

    // 리로드 뒤 자동 재적용: 새 주소에, 지금 값은 **이미 확장된 상태**.
    BagSeen after = first;
    after.address = 0x2000;
    after.cap = 300;
    after.sum = 250;
    after.a = 250;
    bag_backup_upsert(v, after);
    CHECK(v.size() == 1);
    CHECK(v[0].cap == 240);        // 최초 원본
    CHECK(v[0].sum == 190);
    CHECK(v[0].a == 190);
    CHECK(v[0].address == 0x2000);  // 주소만 따라간다
}

TEST(bag_backup_follows_the_address_even_when_nothing_was_written) {
    // **리뷰 치명 1 의 회귀 시험.** "이미 그 값이다" 는 우리가 쓴 값이 세이브에 남아
    // 로드 뒤에도 그대로인 경우의 판정이다. 그때 주소를 안 고치면 이 기능이
    // **성공했을 때만** 되돌리기가 잠긴다 - 가장 나쁜 실패 방식이다.
    std::vector<BagBackup> v;
    BagSeen first = bag_seen(0x1000);
    first.changed = true;
    first.known = true;
    first.want_cap = 300;
    first.want_sum = 250;
    bag_backup_upsert(v, first);

    BagSeen skipped = bag_seen(0x2000);
    skipped.cap = 300;          // 저장에서 살아 돌아온 값
    skipped.sum = 250;
    skipped.a = 250;
    skipped.changed = false;    // 쓸 것이 없어 건너뛰었다
    skipped.known = true;
    skipped.want_cap = 300;
    skipped.want_sum = 250;
    bag_backup_upsert(v, skipped);
    CHECK(v.size() == 1);
    CHECK(v[0].address == 0x2000);
    CHECK(v[0].cap == 240);
}

TEST(bag_backup_does_not_record_what_we_never_changed) {
    // 우리가 바꾼 적 없는 컨테이너의 되돌리기 기록은 있을 이유가 없다.
    std::vector<BagBackup> v;
    bag_backup_upsert(v, bag_seen(0x1000));   // changed = false
    CHECK(v.empty());
}

TEST(bag_backup_rebaselines_when_someone_else_changed_the_container) {
    // **재검토 치명 1 의 회귀 시험.** want 만 갱신하면 혈통 검사가 리로드 한 번으로
    // 무력해진다 - 자동 재적용이 "마지막으로 써 놓은 값" 을 매번 새로 덮어쓰는데
    // 원본은 영영 그대로라, 그 사이 컨테이너가 정당하게 커져도 되돌리기가 옛 원본을
    // 쓴다(= 사용자가 돈 주고 산 칸을 지운다).
    std::vector<BagBackup> v;
    BagSeen first = bag_seen(0x1000);          // 240/190/190/0
    first.changed = true;
    first.understood = true;
    first.known = true;
    first.want_cap = 300;
    first.want_sum = 250;
    bag_backup_upsert(v, first);

    // (1) 우리 값이 저장에서 그대로 살아 돌아왔다 - 원본을 다시 잡지 않는다.
    BagSeen survived = bag_seen(0x2000);
    survived.cap = 300;
    survived.sum = 250;
    survived.a = 250;
    survived.understood = true;
    survived.known = true;
    survived.want_cap = 300;
    survived.want_sum = 250;
    bag_backup_upsert(v, survived);
    CHECK(v.size() == 1);
    CHECK(v[0].cap == 240);

    // (2) 그 사이 사용자가 확장권을 써서 290 이 됐다(기본 50 + 원래 190 + 산 50).
    //     되돌아갈 자리는 옛 240 이 아니라 지금 이 290 이다.
    BagSeen bought = bag_seen(0x3000);
    bought.cap = 290;
    bought.sum = 240;
    bought.a = 240;
    bought.changed = true;
    bought.understood = true;
    bought.known = true;
    bought.want_cap = 300;
    bought.want_sum = 250;
    bag_backup_upsert(v, bought);
    CHECK(v.size() == 1);
    CHECK(v[0].cap == 290);   // **구매한 50칸을 지키는 줄**
    CHECK(v[0].sum == 240);
    CHECK(v[0].a == 240);
    // 같은 seen 으로 한 번 더 불려도(쓰기 전/후 두 번 부른다) 결과가 같다.
    bag_backup_upsert(v, bought);
    CHECK(v[0].cap == 290);
}

TEST(bag_backup_does_not_rebaseline_before_it_knows_what_it_wrote) {
    // 아직 무엇을 썼는지 모르는 항목(want 0)은 다시 잡지 않는다 - 그러면 쓰기가
    // 반쯤 실패한 상태가 "원본" 으로 굳는다.
    std::vector<BagBackup> v;
    BagSeen first = bag_seen(0x1000);
    first.changed = true;   // known = false: 쓰기 결과를 아직 모른다
    bag_backup_upsert(v, first);
    CHECK(v.size() == 1);
    CHECK(v[0].want_cap == 0);

    BagSeen weird = bag_seen(0x1000);
    weird.cap = 999;
    weird.sum = 900;
    weird.understood = true;
    bag_backup_upsert(v, weird);
    CHECK(v[0].cap == 240);   // 다시 잡지 않았다
}

TEST(bag_backup_separates_realms_and_kinds) {
    std::vector<BagBackup> v;
    for (int realm = 0; realm < 2; ++realm) {
        for (const std::uint16_t kind : {std::uint16_t{1}, std::uint16_t{7}}) {
            BagSeen s = bag_seen(0x1000 + realm * 0x100 + kind);
            s.realm = realm;
            s.kind = kind;
            s.changed = true;
            bag_backup_upsert(v, s);
        }
    }
    CHECK(v.size() == 4);
    // 같은 (realm, 종류)가 **다른 주소로** 다시 와도 항목이 늘지 않는다. 이것이
    // 리로드를 넘기는 방식이다 - 인벤토리가 새로 생기면 주소만 바뀌고 realm·종류는
    // 그대로다. 한 컴포넌트 안에서 종류가 유일하다는 실측(18개, 종류 0~19 가 한
    // 번씩)에 기대며, 그 가정이 깨졌을 때 쓰기를 막는 것은 열쇠가 아니라
    // bag_restore_blocked 의 혈통 검사다.
    BagSeen again = bag_seen(0x9999);
    again.changed = true;
    bag_backup_upsert(v, again);
    CHECK(v.size() == 4);
    for (const auto& x : v) {
        if (x.realm == 0 && x.kind == 1) CHECK(x.address == 0x9999);
    }
}

TEST(bag_restore_allows_exactly_what_we_wrote) {
    const BagBackup s = applied_bag(0x1000);
    CHECK(s.want_cap == 300);
    CHECK(bag_restore_blocked(s, 300, 250, 100) == nullptr);
}

TEST(bag_restore_refuses_when_someone_else_changed_it) {
    // **리뷰 치명 3.** 열쇠가 (realm, 종류)뿐이라 캐릭터가 바뀌거나 사용자가 정당한
    // 확장을 더 사면, 남의 원본을 남의 가방에 **줄이는 방향으로** 쓸 수 있다.
    const BagBackup s = applied_bag(0x1000);

    // 게임 안에서 확장권을 더 썼다: 우리가 만든 300/250 이 아니다.
    CHECK(bag_restore_blocked(s, 350, 300, 100) != nullptr);
    // 캐릭터 교체: 기본 슬롯(cap - sum)이 다르다.
    BagBackup other = s;
    other.want_cap = 300;
    other.want_sum = 200;   // 기본 100 - 우리 가방(기본 50)이 아니다
    CHECK(bag_restore_blocked(other, 300, 200, 100) != nullptr);
    // 무엇을 써 놓았는지 모르는 기록은 되돌리지 않는다.
    BagBackup unknown = s;
    unknown.want_cap = 0;
    CHECK(bag_restore_blocked(unknown, 300, 250, 100) != nullptr);
    // 값을 못 읽었다.
    CHECK(bag_restore_blocked(s, -1, 250, 100) != nullptr);
}

TEST(bag_restore_refuses_to_push_items_out_of_the_bag) {
    // **리뷰 치명 4.** plan_bag_expand 는 "이미 목표보다 큰 칸을 깎으면 용량 밖으로
    // 밀려난 아이템이 어떻게 되는지 모른다" 며 거부하는데, 복원만 그 원칙 밖에 있었다.
    const BagBackup s = applied_bag(0x1000);
    CHECK(bag_restore_blocked(s, 300, 250, 240) == nullptr);   // 딱 원래 용량
    CHECK(bag_restore_blocked(s, 300, 250, 241) != nullptr);   // 한 칸 넘으면 막는다
    CHECK(bag_restore_blocked(s, 300, 250, 260) != nullptr);
}

TEST(bag_backup_does_not_rebaseline_on_a_shape_it_does_not_understand) {
    // **게이트 경미 3.** plan_bag_expand 가 거부한 모양(전이 상태 등)의 값을 원본으로
    // 채택하면, 그것을 한 번 본 것만으로 멀쩡한 원본이 갈린다.
    std::vector<BagBackup> v;
    BagSeen first = bag_seen(0x1000);
    first.changed = true;
    first.understood = true;
    first.known = true;
    first.want_cap = 300;
    first.want_sum = 250;
    bag_backup_upsert(v, first);

    BagSeen weird = bag_seen(0x1000);
    weird.cap = 100;          // 용량이 확장 합계보다 작다 - 우리 모델이 아니다
    weird.sum = 200;
    weird.understood = false;
    bag_backup_upsert(v, weird);
    CHECK(v[0].cap == 240);   // 원본은 그대로

    // 같은 값이라도 모양을 이해했다면 그것이 새 원본이다.
    weird.understood = true;
    bag_backup_upsert(v, weird);
    CHECK(v[0].cap == 100);
}

TEST(bag_restore_sees_when_there_is_nothing_to_undo) {
    // **게이트 경미 5.** 지금 값이 원본 그대로면 되돌릴 것이 없다. 막힘으로 치면
    // 버튼이 켜진 채 "적용한 뒤 값이 바뀌었습니다" 만 반복한다.
    const BagBackup s = applied_bag(0x1000);
    CHECK(bag_restore_already_original(s, 240, 190, 190, 0));
    CHECK(!bag_restore_already_original(s, 300, 250, 250, 0));
    CHECK(!bag_restore_already_original(s, 240, 190, 0, 190));   // 갈래가 다르다
}

TEST(auto_reapply_waits_for_a_new_generation_and_both_realms) {
    CHECK(!should_auto_reapply(false, 2, 1, true));   // 무장 안 됨
    CHECK(!should_auto_reapply(true, 1, 1, true));    // 같은 세대 - 이미 했다
    // 한쪽 realm 만 잡힌 채로 쓰면 그 뒤로 "이미 했다" 가 돼 클라가 영영 안 걸린다.
    CHECK(!should_auto_reapply(true, 2, 1, false));
    CHECK(should_auto_reapply(true, 2, 1, true));
}

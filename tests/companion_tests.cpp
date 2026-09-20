#include <cstdint>
#include <cstring>
#include <string>

#include "game/companion.h"
#include "harness.h"

using cdtb::game::decode_hire_to_target;
using cdtb::game::decode_message_header;
using cdtb::game::hex_bytes;

TEST(companion_header_decodes_id_and_body_len) {
    // [ID 2959 = 0x0B8F LE][u8][본문길이 5 LE]  (2850 까지 2338 = 0x0922)
    const std::uint8_t p[] = {0x8F, 0x0B, 0x00, 0x05, 0x00};
    std::uint16_t id = 0, body = 0;
    CHECK(decode_message_header(p, sizeof(p), &id, &body));
    CHECK_EQ(id, static_cast<std::uint16_t>(2959));
    CHECK_EQ(body, static_cast<std::uint16_t>(5));
    CHECK(!decode_message_header(p, 4, &id, &body));
    CHECK(!decode_message_header(nullptr, 5, &id, &body));
}

TEST(companion_hire_to_target_decodes_handle_and_flag) {
    // 머리 5 + 본문 5: 핸들 0x2000012C, 플래그 1
    const std::uint8_t p[] = {0x8F, 0x0B, 0x00, 0x05, 0x00,
                              0x2C, 0x01, 0x00, 0x20, 0x01};
    std::uint32_t handle = 0;
    std::uint8_t flag = 0;
    CHECK(decode_hire_to_target(p, sizeof(p), &handle, &flag));
    CHECK_EQ(handle, 0x2000012Cu);
    CHECK_EQ(flag, static_cast<std::uint8_t>(1));
}

TEST(companion_hire_to_target_rejects_other_body_len) {
    // 본문길이 4 라고 적힌 머리 -> 정적 분석(5)과 다르므로 거부
    const std::uint8_t p[] = {0x8F, 0x0B, 0x00, 0x04, 0x00,
                              0x2C, 0x01, 0x00, 0x20, 0x01};
    std::uint32_t handle = 0;
    std::uint8_t flag = 0;
    CHECK(!decode_hire_to_target(p, sizeof(p), &handle, &flag));
    // 머리는 맞는데 바이트가 모자람
    const std::uint8_t q[] = {0x8F, 0x0B, 0x00, 0x05, 0x00, 0x2C, 0x01};
    CHECK(!decode_hire_to_target(q, sizeof(q), &handle, &flag));
}

TEST(companion_hex_bytes_caps_and_marks_overflow) {
    const std::uint8_t p[] = {0x00, 0xAB, 0xFF, 0x10};
    CHECK_EQ(hex_bytes(p, 4, 8), std::string("00 AB FF 10"));
    CHECK_EQ(hex_bytes(p, 4, 2), std::string("00 AB \xE2\x80\xA6"));
    CHECK_EQ(hex_bytes(p, 0, 8), std::string(""));
}

TEST(companion_use_item_wire_layout) {
    using cdtb::game::build_use_item_wire;
    std::uint8_t w[32]{};
    std::size_t len = 0;
    CHECK(build_use_item_wire(1003843, 0, 0x0D, 0, w, sizeof(w), &len));
    CHECK_EQ(len, static_cast<std::size_t>(18));
    // 머리: ID 2269 = 0x08DD, 본문길이 13  (2850 까지 2976 = 0x0BA0)
    CHECK_EQ(w[0], static_cast<std::uint8_t>(0xDD));
    CHECK_EQ(w[1], static_cast<std::uint8_t>(0x08));
    CHECK_EQ(w[2], static_cast<std::uint8_t>(0));
    CHECK_EQ(w[3], static_cast<std::uint8_t>(13));
    CHECK_EQ(w[4], static_cast<std::uint8_t>(0));
    // 본문: A=1003843 (0x000F5143), B=0, C=0x0D, D=0
    CHECK_EQ(w[5], static_cast<std::uint8_t>(0x43));
    CHECK_EQ(w[6], static_cast<std::uint8_t>(0x51));
    CHECK_EQ(w[7], static_cast<std::uint8_t>(0x0F));
    CHECK_EQ(w[8], static_cast<std::uint8_t>(0x00));
    CHECK_EQ(w[13], static_cast<std::uint8_t>(0x0D));
    std::uint16_t id = 0, body = 0;
    CHECK(cdtb::game::decode_message_header(w, len, &id, &body));
    CHECK_EQ(id, static_cast<std::uint16_t>(2269));
    CHECK_EQ(body, static_cast<std::uint16_t>(13));
    // 버퍼가 작으면 거부
    CHECK(!build_use_item_wire(1, 0, 0x0D, 0, w, 10, &len));
}

TEST(companion_command_parser_rejects_unknown) {
    std::string reply;
    CHECK(!cdtb::game::companion_run_command("frobnicate 1", &reply));
    CHECK(!cdtb::game::companion_run_command("", &reply));
    // 인자 부족
    CHECK(!cdtb::game::companion_run_command("useitem", &reply));
    CHECK(!cdtb::game::companion_run_command("give", &reply));
}

TEST(companion_parse_hex_bytes) {
    using cdtb::game::parse_hex_bytes;
    std::uint8_t w[8]{};
    std::size_t n = 0;
    CHECK(parse_hex_bytes("A0 0B 00", w, sizeof(w), &n));
    CHECK_EQ(n, static_cast<std::size_t>(3));
    CHECK_EQ(w[0], static_cast<std::uint8_t>(0xA0));
    CHECK_EQ(w[1], static_cast<std::uint8_t>(0x0B));
    CHECK(parse_hex_bytes("a00b00", w, sizeof(w), &n));
    CHECK_EQ(n, static_cast<std::size_t>(3));
    CHECK(!parse_hex_bytes("A0 0", w, sizeof(w), &n));      // 홀수 자릿수
    CHECK(!parse_hex_bytes("zz", w, sizeof(w), &n));        // 16진 아님
    CHECK(!parse_hex_bytes("", w, sizeof(w), &n));          // 비어 있음
    CHECK(!parse_hex_bytes("00112233445566778899", w, 4, &n));  // 버퍼 초과
}

TEST(companion_hire_wire_layout) {
    using cdtb::game::build_hire_wire;
    std::uint8_t w[16]{};
    std::size_t len = 0;
    CHECK(build_hire_wire(0xB0100153, 0, w, sizeof(w), &len));
    CHECK_EQ(len, static_cast<std::size_t>(10));
    // 머리: ID 2959 = 0x0B8F, 본문길이 5  (2850 까지 2338 = 0x0922)
    CHECK_EQ(w[0], static_cast<std::uint8_t>(0x8F));
    CHECK_EQ(w[1], static_cast<std::uint8_t>(0x0B));
    CHECK_EQ(w[3], static_cast<std::uint8_t>(5));
    // 본문: 핸들 LE + 플래그
    CHECK_EQ(w[5], static_cast<std::uint8_t>(0x53));
    CHECK_EQ(w[6], static_cast<std::uint8_t>(0x01));
    CHECK_EQ(w[7], static_cast<std::uint8_t>(0x10));
    CHECK_EQ(w[8], static_cast<std::uint8_t>(0xB0));
    CHECK_EQ(w[9], static_cast<std::uint8_t>(0));
    std::uint16_t id = 0, body = 0;
    CHECK(cdtb::game::decode_message_header(w, len, &id, &body));
    CHECK_EQ(id, static_cast<std::uint16_t>(2959));
    // 실측한 진돗개 획득 와이어와 바이트가 같아야 한다(ID 만 2944 값으로 옮겼다)
    std::uint32_t handle = 0;
    std::uint8_t flag = 0;
    CHECK(cdtb::game::decode_hire_to_target(w, len, &handle, &flag));
    CHECK_EQ(handle, 0xB0100153u);
    CHECK(!build_hire_wire(1, 0, w, 4, &len));
}

// 등록 뒤 소환을 마무리하는 요청. 역직렬화(RVA 0x2965E40)가 8바이트
// 그리고 12바이트를 읽고 커서가 본문 길이와 같은지 확인하므로,
// 본문은 정확히 20바이트여야 한다.
TEST(complete_summon_wire_is_number_then_position) {
    const float pos[3] = {1.5f, -2.25f, 3.0f};
    std::uint8_t wire[32]{};
    std::size_t len = 0;
    CHECK(cdtb::game::build_complete_summon_wire(0x000F4412ull, pos, wire,
                                                 sizeof(wire), &len));
    CHECK_EQ(len, static_cast<std::size_t>(25));
    std::uint16_t id = 0, body = 0;
    cdtb::game::decode_message_header(wire, len, &id, &body);
    CHECK_EQ(id, static_cast<std::uint16_t>(2244));
    CHECK_EQ(body, static_cast<std::uint16_t>(20));
    std::uint64_t no = 0;
    std::memcpy(&no, wire + 5, sizeof(no));
    CHECK_EQ(no, 0x000F4412ull);
    float back[3]{};
    std::memcpy(back, wire + 13, sizeof(back));
    CHECK(back[0] == 1.5f && back[1] == -2.25f && back[2] == 3.0f);
}

TEST(complete_summon_wire_rejects_a_small_buffer) {
    const float pos[3] = {0.0f, 0.0f, 0.0f};
    std::uint8_t wire[8]{};
    std::size_t len = 0;
    CHECK(!cdtb::game::build_complete_summon_wire(1, pos, wire, sizeof(wire),
                                                  &len));
}

TEST(complete_summon_wire_rejects_a_null_position) {
    std::uint8_t wire[32]{};
    std::size_t len = 0;
    CHECK(!cdtb::game::build_complete_summon_wire(1, nullptr, wire,
                                                  sizeof(wire), &len));
}

// 게임이 실제로 보낸 와이어다. 사용자가 정상 플레이로 야생 개체를
// 잡는 동안 캡처됐다(실측 2026-09-07 17:49:38, exe 1.0.0.2850).
//
//   52 09 00 08 00 | 01 00 10 A0 | 0D 37 10 B0
//
// 이 표본이 우리 조립·해석의 기준이다.
//
// **머리의 ID 두 바이트만 상수에서 만든다.** 1.0.0.2944 가 메시지 ID 공간을
// 통째로 재번호해 이 종류가 2386 -> 2592 로 바뀌었다. 캡처를 2944 값으로
// 고쳐 적으면 "실측한 와이어" 라는 말이 거짓이 되므로, 표본은 잡은 그대로
// 두고 ID 자리만 지금 상수로 덮어 **본문 배치**(본문길이 8, u32 자신 + u32
// 대상)를 못박는다 - 그것이 이 시험이 지키려는 것이고 재번호를 안 탄다.
TEST(catch_wire_matches_the_bytes_the_game_sent) {
    // 2850 에 잡은 그대로.
    const std::uint8_t captured[] = {0x52, 0x09, 0x00, 0x08, 0x00,
                                     0x01, 0x00, 0x10, 0xA0,
                                     0x0D, 0x37, 0x10, 0xB0};
    std::uint8_t sample[sizeof(captured)];
    std::memcpy(sample, captured, sizeof(captured));
    const std::uint16_t id = cdtb::game::kCatchBySummonId;
    std::memcpy(sample, &id, sizeof(id));   // ID 두 바이트만 지금 값으로

    std::uint32_t self = 0, target = 0;
    CHECK(cdtb::game::decode_catch(sample, sizeof(sample), &self, &target));
    CHECK_EQ(self, 0xA0100001u);
    CHECK_EQ(target, 0xB010370Du);

    std::uint8_t wire[16]{};
    std::size_t len = 0;
    CHECK(cdtb::game::build_catch_wire(self, target, wire, sizeof(wire), &len));
    CHECK_EQ(len, sizeof(sample));
    CHECK(std::memcmp(wire, sample, sizeof(sample)) == 0);
    // 본문 여덟 바이트는 캡처와 글자 하나 안 틀려야 한다(ID 만 달라진다).
    CHECK(std::memcmp(wire + 5, captured + 5, sizeof(captured) - 5) == 0);
}

TEST(catch_decode_rejects_a_wrong_length) {
    // ID 는 맞아야 한다 - 안 그러면 "길이 때문에" 거부한 것인지 알 수 없다.
    std::uint8_t shortw[] = {0x00, 0x00, 0x00, 0x08, 0x00, 0x01, 0x00};
    const std::uint16_t cid = cdtb::game::kCatchBySummonId;
    std::memcpy(shortw, &cid, sizeof(cid));
    std::uint32_t self = 0, target = 0;
    CHECK(!cdtb::game::decode_catch(shortw, sizeof(shortw), &self, &target));
}

TEST(catch_decode_rejects_another_message_id) {
    // 획득(2959) 와이어를 붙잡기로 읽으면 안 된다.
    std::uint8_t other[13]{};
    const std::uint16_t id = 2959, body = 8;
    std::memcpy(other, &id, 2);
    std::memcpy(other + 3, &body, 2);
    std::uint32_t self = 0, target = 0;
    CHECK(!cdtb::game::decode_catch(other, sizeof(other), &self, &target));
}

TEST(catch_wire_rejects_a_small_buffer) {
    std::uint8_t wire[8]{};
    std::size_t len = 0;
    CHECK(!cdtb::game::build_catch_wire(1, 2, wire, sizeof(wire), &len));
}

TEST(hire_inv_wire_round_trips) {
    std::uint8_t wire[16]{};
    std::size_t len = 0;
    CHECK(cdtb::game::build_hire_inv_wire(18578, 7, wire, sizeof(wire), &len));
    CHECK_EQ(len, cdtb::game::kHireInvWireLen);
    // 머리: ID 3021 = 0x0BCD, 본문길이 4  (2850 까지 2454 = 0x0996)
    CHECK_EQ(wire[0], 0xCD);
    CHECK_EQ(wire[1], 0x0B);
    CHECK_EQ(wire[2], 0x00);
    CHECK_EQ(wire[3], 0x04);
    CHECK_EQ(wire[4], 0x00);
    std::uint16_t a = 0, b = 0;
    CHECK(cdtb::game::decode_hire_inv(wire, len, &a, &b));
    CHECK_EQ(a, 18578u);
    CHECK_EQ(b, 7u);
}

TEST(hire_inv_decode_rejects_a_wrong_length) {
    std::uint8_t wire[16]{};
    std::size_t len = 0;
    CHECK(cdtb::game::build_hire_inv_wire(1, 2, wire, sizeof(wire), &len));
    std::uint16_t a = 0, b = 0;
    CHECK(!cdtb::game::decode_hire_inv(wire, len - 1, &a, &b));
}

TEST(hire_inv_wire_rejects_a_small_buffer) {
    std::uint8_t wire[4]{};
    std::size_t len = 0;
    CHECK(!cdtb::game::build_hire_inv_wire(1, 2, wire, sizeof(wire), &len));
}

// ---------------------------------------------------------------------------
// 종 바꾸기 준비 상태 (2026-09-20)
//
// 종 바꾸기는 **클라·서버 두 쪽 명부가 다 있어야** 한다. 한쪽만 없어도
// `resolve_species_write` 가 false 를 내는데, 예전에는 화면이 그것을
// "자리를 못 찾았습니다 - 월드 안인지 보세요" **하나로** 냈다. 사용자가
// **월드 안에서** 그 문구를 보고 엉뚱한 곳을 봤다 - 실제로는 아이템을 쓰면서
// 게임이 명부를 새로 만들었고(TROUBLESHOOTING §1.4) 클라 쪽을 48초짜리 힙
// 훑기로 다시 찾는 중이었다.
// ---------------------------------------------------------------------------
#include "game/clan.h"

using cdtb::game::ClanRealmState;
using cdtb::game::clan_realm_state_of;
using cdtb::game::species_no_target_message;
using cdtb::game::SpeciesTargetGap;

TEST(clan_realm_state_prefers_ready_over_everything) {
    // 캐시가 차 있으면 훑는 중이든 그만뒀든 쓸 수 있다.
    CHECK(clan_realm_state_of(true, false, false) == ClanRealmState::Ready);
    CHECK(clan_realm_state_of(true, true, false) == ClanRealmState::Ready);
    CHECK(clan_realm_state_of(true, true, true) == ClanRealmState::Ready);
}

TEST(clan_realm_state_says_scanning_before_gave_up) {
    // 도는 중이면 "그만뒀다" 보다 그것을 먼저 말한다 - 기다리면 되기 때문이다.
    CHECK(clan_realm_state_of(false, true, false) == ClanRealmState::Scanning);
    CHECK(clan_realm_state_of(false, true, true) == ClanRealmState::Scanning);
}

TEST(clan_realm_state_distinguishes_gave_up_from_not_yet) {
    // 이 둘을 섞으면 화면이 "곧 찾습니다" 를 영영 띄운다(TROUBLESHOOTING 2.10.1).
    CHECK(clan_realm_state_of(false, false, true) == ClanRealmState::GaveUp);
    CHECK(clan_realm_state_of(false, false, false) == ClanRealmState::Missing);
}

// 둘 다 없을 때만 "월드 안인지" 를 묻는다. 한쪽만 없으면 그 한쪽을 말한다 -
// 그것이 2026-09-20 에 사용자를 헤매게 한 지점이다.
TEST(species_message_asks_about_the_world_only_when_both_are_missing) {
    SpeciesTargetGap g;   // 전부 false
    const std::string m = species_no_target_message(g);
    CHECK(m.find("월드") != std::string::npos);
}

TEST(species_message_names_the_missing_realm) {
    SpeciesTargetGap only_server;
    only_server.server_comp = true;
    only_server.server_rec = true;
    const std::string a = species_no_target_message(only_server);
    CHECK(a.find("클라") != std::string::npos);
    CHECK(a.find("월드") == std::string::npos);   // 월드 탓으로 돌리지 않는다

    SpeciesTargetGap only_client;
    only_client.client_comp = true;
    only_client.client_rec = true;
    const std::string b = species_no_target_message(only_client);
    CHECK(b.find("서버") != std::string::npos);
    CHECK(b.find("월드") == std::string::npos);
}

// 컴포넌트는 둘 다 잡혔는데 그 번호의 레코드만 없는 경우. "명부를 찾는 중"
// 이라고 하면 영영 기다리게 된다 - 다른 말이어야 한다.
TEST(species_message_separates_a_missing_record_from_a_missing_roster) {
    SpeciesTargetGap comps_ok;
    comps_ok.server_comp = true;
    comps_ok.client_comp = true;   // 레코드는 둘 다 없음
    const std::string m = species_no_target_message(comps_ok);
    CHECK(m.find("찾는 중") == std::string::npos);
    CHECK(m.find("없습니다") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 탈것 호출 거부 사유 (2026-09-20)
//
// 보스룸에서 왜 안 불리는지를 **추측하지 않고 찍기** 위한 것이다. 오류 코드는
// 런타임에 등록되므로 상수가 아니다 - 전역 슬롯에서 읽은 값과 견준다.
// ---------------------------------------------------------------------------
#include "game/callcheck.h"

using cdtb::game::callcheck_reason_name;
using cdtb::game::kCallCheckReasonCount;

TEST(callcheck_names_the_reason_by_matching_the_registered_value) {
    const std::uint32_t v[] = {11, 22, 33, 44, 55};
    CHECK(std::string(callcheck_reason_name(33, v, 5)) ==
          "eErrNoCallVehicleMercenaryRideLimit");
    CHECK(std::string(callcheck_reason_name(55, v, 5)) ==
          "eErrNoCallVehicleInvalidPosition");
}

// **이름을 지어내지 않는다.** 모르는 코드는 nullptr 이어야 로그가 슬롯값을
// 같이 찍어 밖에서 대조할 수 있다.
TEST(callcheck_returns_null_for_an_unknown_code) {
    const std::uint32_t v[] = {11, 22, 33, 44, 55};
    CHECK(callcheck_reason_name(99, v, 5) == nullptr);
    CHECK(callcheck_reason_name(0, v, 5) == nullptr);     // 0 은 성공이다
    CHECK(callcheck_reason_name(11, nullptr, 5) == nullptr);
}

// **0 은 성공이고, 등록 전 슬롯도 0 이다.** 둘이 만나면 "성공인데 첫 칸
// 이름이 붙는" 거짓말이 된다 - `err == 0` 을 먼저 걸러야 한다.
//
// (처음에 "0 인 슬롯은 건너뛴다" 를 시험으로 썼는데, RED 검증에서 그 가드를
//  빼도 시험이 통과했다 - `err == 0` 이 앞서 걸리므로 **정의상 성립하는
//  항등식**이었다. §7.7 그대로다. 진짜 위험을 재도록 고쳤다.)
TEST(callcheck_never_names_success_even_when_slots_are_zero) {
    const std::uint32_t none[] = {0, 0, 0, 0, 0};
    CHECK(callcheck_reason_name(0, none, 5) == nullptr);   // <- 여기가 위험한 곳
    CHECK(callcheck_reason_name(7, none, 5) == nullptr);
    const std::uint32_t half[] = {0, 0, 7, 0, 0};
    CHECK(std::string(callcheck_reason_name(7, half, 5)) ==
          "eErrNoCallVehicleMercenaryRideLimit");
}

TEST(callcheck_reason_table_has_the_six_sites_five_distinct_errors) {
    CHECK(kCallCheckReasonCount == static_cast<std::size_t>(5));
}

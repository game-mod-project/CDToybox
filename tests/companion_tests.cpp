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

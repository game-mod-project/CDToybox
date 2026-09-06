#include <cstdint>
#include <string>

#include "game/companion.h"
#include "harness.h"

using cdtb::game::decode_hire_to_target;
using cdtb::game::decode_message_header;
using cdtb::game::hex_bytes;

TEST(companion_header_decodes_id_and_body_len) {
    // [ID 2338 = 0x0922 LE][u8][본문길이 5 LE]
    const std::uint8_t p[] = {0x22, 0x09, 0x00, 0x05, 0x00};
    std::uint16_t id = 0, body = 0;
    CHECK(decode_message_header(p, sizeof(p), &id, &body));
    CHECK_EQ(id, static_cast<std::uint16_t>(2338));
    CHECK_EQ(body, static_cast<std::uint16_t>(5));
    CHECK(!decode_message_header(p, 4, &id, &body));
    CHECK(!decode_message_header(nullptr, 5, &id, &body));
}

TEST(companion_hire_to_target_decodes_handle_and_flag) {
    // 머리 5 + 본문 5: 핸들 0x2000012C, 플래그 1
    const std::uint8_t p[] = {0x22, 0x09, 0x00, 0x05, 0x00,
                              0x2C, 0x01, 0x00, 0x20, 0x01};
    std::uint32_t handle = 0;
    std::uint8_t flag = 0;
    CHECK(decode_hire_to_target(p, sizeof(p), &handle, &flag));
    CHECK_EQ(handle, 0x2000012Cu);
    CHECK_EQ(flag, static_cast<std::uint8_t>(1));
}

TEST(companion_hire_to_target_rejects_other_body_len) {
    // 본문길이 4 라고 적힌 머리 -> 정적 분석(5)과 다르므로 거부
    const std::uint8_t p[] = {0x22, 0x09, 0x00, 0x04, 0x00,
                              0x2C, 0x01, 0x00, 0x20, 0x01};
    std::uint32_t handle = 0;
    std::uint8_t flag = 0;
    CHECK(!decode_hire_to_target(p, sizeof(p), &handle, &flag));
    // 머리는 맞는데 바이트가 모자람
    const std::uint8_t q[] = {0x22, 0x09, 0x00, 0x05, 0x00, 0x2C, 0x01};
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
    // 머리: ID 2976 = 0x0BA0, 본문길이 13
    CHECK_EQ(w[0], static_cast<std::uint8_t>(0xA0));
    CHECK_EQ(w[1], static_cast<std::uint8_t>(0x0B));
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
    CHECK_EQ(id, static_cast<std::uint16_t>(2976));
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
    // 머리: ID 2338 = 0x0922, 본문길이 5
    CHECK_EQ(w[0], static_cast<std::uint8_t>(0x22));
    CHECK_EQ(w[1], static_cast<std::uint8_t>(0x09));
    CHECK_EQ(w[3], static_cast<std::uint8_t>(5));
    // 본문: 핸들 LE + 플래그
    CHECK_EQ(w[5], static_cast<std::uint8_t>(0x53));
    CHECK_EQ(w[6], static_cast<std::uint8_t>(0x01));
    CHECK_EQ(w[7], static_cast<std::uint8_t>(0x10));
    CHECK_EQ(w[8], static_cast<std::uint8_t>(0xB0));
    CHECK_EQ(w[9], static_cast<std::uint8_t>(0));
    std::uint16_t id = 0, body = 0;
    CHECK(cdtb::game::decode_message_header(w, len, &id, &body));
    CHECK_EQ(id, static_cast<std::uint16_t>(2338));
    // 실측한 진돗개 획득 와이어와 바이트가 같아야 한다
    std::uint32_t handle = 0;
    std::uint8_t flag = 0;
    CHECK(cdtb::game::decode_hire_to_target(w, len, &handle, &flag));
    CHECK_EQ(handle, 0xB0100153u);
    CHECK(!build_hire_wire(1, 0, w, 4, &len));
}

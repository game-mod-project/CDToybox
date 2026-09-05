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

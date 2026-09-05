#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "mem/reader.h"
#include "mem/rtti.h"

namespace cdtb::game {

// 동반자(탈것·반려동물) 획득 경로의 메시지 캡처 (Phase 1, 진단).
//
// 설계: docs/superpowers/specs/2026-09-05-companion-summon-acquire-design.md
// 길들이기·등록·부르기 때 게임이 실제로 보내는 메시지의 역직렬화
// 함수(서술자 vtable[2])를 후킹해 와이어 본문을 로그로 낸다. 형식을
// 배우는 것이 목적이라 아무것도 바꾸지 않는다.
//
// 와이어: 패킷 +0x10 u16 전체길이 · +0x18 페이로드 포인터.
// 페이로드 머리 5바이트 = [메시지ID u16][u8][본문길이 u16] (역직렬화가
// `word[payload+3] == 전체길이-5` 를 검사한다). 본문은 그 뒤.
//
// 획득 대상 메시지 `TrocTrHireMercenaryToTargetReq`(ID 2338) 본문은
// 정적 분석으로 `u32 대상 액터 핸들 + u8 플래그` 5바이트다.

// 페이로드 머리를 푼다. 길이가 모자라면 false.
bool decode_message_header(const std::uint8_t* payload, std::size_t len,
                           std::uint16_t* id_out, std::uint16_t* body_len_out);

// 2338 본문(머리 뒤 5바이트)을 푼다. 머리의 본문길이가 5가 아니면 false.
bool decode_hire_to_target(const std::uint8_t* payload, std::size_t len,
                           std::uint32_t* handle_out, std::uint8_t* flag_out);

// 16진 덤프. cap 바이트까지만 찍고 넘치면 "…" 를 붙인다.
std::string hex_bytes(const std::uint8_t* p, std::size_t n, std::size_t cap);

// 마지막으로 잡힌 획득 대상. valid 가 false 면 아직 없다.
struct HireTargetCapture {
    bool valid = false;
    std::uint32_t handle = 0;
    std::uint8_t flag = 0;
};
HireTargetCapture last_hire_target();

// 잡힌 메시지 수(전체).
int companion_capture_count();

// 역직렬화 훅을 건다. 한 번만. 하나라도 걸리면 true.
bool companion_capture_install(const mem::Rtti& rtti,
                               const mem::Reader& reader);
bool companion_capture_installed();

}  // namespace cdtb::game

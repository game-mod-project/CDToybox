#include "harness.h"
#include "input/filter.h"

using cdtb::input::decode_raw_mouse;
using cdtb::input::legacy_gate_step;
using cdtb::input::LegacyGateState;
using cdtb::input::mask_key_state;
using cdtb::input::raw_answered;
using cdtb::input::RawMouseDecoded;
using cdtb::input::Swallow;
using cdtb::input::swallow_message;

namespace {
constexpr unsigned kWmInput = 0x00FF, kWmKeyDown = 0x0100, kWmChar = 0x0102,
                   kWmSysKeyDown = 0x0104, kWmSysChar = 0x0106,
                   kWmMouseMove = 0x0200, kWmLButtonDown = 0x0201,
                   kWmMouseWheel = 0x020A, kWmMouseHWheel = 0x020E,
                   kWmAfterMouse = 0x020F;
}  // namespace

TEST(input_filter_passes_everything_when_hidden) {
    CHECK(swallow_message(kWmInput, false, true, 0) == Swallow::No);
    CHECK(swallow_message(kWmMouseMove, false, true, -1) == Swallow::No);
    CHECK(swallow_message(kWmKeyDown, false, true, -1) == Swallow::No);
    CHECK(swallow_message(kWmChar, false, true, -1) == Swallow::No);
    CHECK(!mask_key_state(0x01, false, true));
}

TEST(input_filter_raw_input_by_device_type) {
    // 마우스는 늘
    CHECK(swallow_message(kWmInput, true, false, 0) == Swallow::DefWindow);
    // 키보드는 포커스 없으면 통과
    CHECK(swallow_message(kWmInput, true, false, 1) == Swallow::No);
    CHECK(swallow_message(kWmInput, true, true, 1) == Swallow::DefWindow);
    // HID 는 손대지 않음
    CHECK(swallow_message(kWmInput, true, true, 2) == Swallow::No);
    CHECK(swallow_message(kWmInput, true, true, -1) == Swallow::No);
}

TEST(input_filter_mouse_messages_always_swallowed) {
    CHECK(swallow_message(kWmMouseMove, true, false, -1) == Swallow::Zero);
    CHECK(swallow_message(kWmLButtonDown, true, false, -1) == Swallow::Zero);
    CHECK(swallow_message(kWmMouseWheel, true, false, -1) == Swallow::Zero);
    CHECK(swallow_message(kWmMouseHWheel, true, false, -1) == Swallow::Zero);
    CHECK(swallow_message(kWmAfterMouse, true, false, -1) == Swallow::No);
}

TEST(input_filter_keys_only_with_text_focus_chars_always) {
    CHECK(swallow_message(kWmKeyDown, true, false, -1) == Swallow::No);
    CHECK(swallow_message(kWmKeyDown, true, true, -1) == Swallow::Zero);
    CHECK(swallow_message(kWmSysKeyDown, true, true, -1) == Swallow::Zero);
    CHECK(swallow_message(kWmChar, true, false, -1) == Swallow::Zero);
    CHECK(swallow_message(kWmSysChar, true, false, -1) == Swallow::Zero);
}

TEST(input_filter_key_state_mask) {
    CHECK(mask_key_state(0x01, true, false));    // VK_LBUTTON
    CHECK(mask_key_state(0x02, true, false));    // VK_RBUTTON
    CHECK(mask_key_state(0x06, true, false));    // VK_XBUTTON2
    CHECK(!mask_key_state(0x57, true, false));   // 'W' - 포커스 없으면 걷는다
    CHECK(mask_key_state(0x57, true, true));
    // VK_INSERT 는 메시지로 처리되므로 무관
    CHECK(!mask_key_state(0x2D, true, false));
}

TEST(input_filter_ime_and_unichar) {
    CHECK(swallow_message(0x0109, true, false, -1) == Swallow::Zero);       // WM_UNICHAR 는 글자
    CHECK(swallow_message(0x010F, true, true, -1) == Swallow::Zero);        // WM_IME_COMPOSITION - 백엔드가 DefWindowProc 을 부른다
    CHECK(swallow_message(0x010D, true, true, -1) == Swallow::DefWindow);   // WM_IME_STARTCOMPOSITION - 조합 창
    CHECK(swallow_message(0x010E, true, true, -1) == Swallow::DefWindow);   // WM_IME_ENDCOMPOSITION
    CHECK(swallow_message(0x0286, true, true, -1) == Swallow::DefWindow);   // WM_IME_CHAR - WM_CHAR 를 만들어야 ImGui 가 받는다
    CHECK(swallow_message(0x010F, true, false, -1) == Swallow::No);         // 포커스 없으면 통과
    CHECK(swallow_message(0x0286, true, false, -1) == Swallow::No);
    CHECK(swallow_message(0x010F, false, true, -1) == Swallow::No);
}

// ---- 창 메시지 끊김 판정 (마우스룩에서 WM_MOUSEMOVE 가 안 오는 굳음, 2026-09-12)

TEST(legacy_gate_stays_alive_while_window_messages_arrive) {
    LegacyGateState s;
    // 움직이는 동안 창 메시지가 바로바로 온다 - 몇 초를 움직여도 살아 있음
    for (unsigned long long t = 1000; t <= 4000; t += 16) {
        CHECK(!legacy_gate_step(s, t, true, t, 300));
    }
    // 펌프 스레드가 적은 시각이 now 보다 클 수도 있다 - "방금" 으로 본다
    CHECK(!legacy_gate_step(s, 4016, true, 4020, 300));
}

TEST(legacy_gate_dies_after_grace_of_motion_without_messages) {
    LegacyGateState s;
    const unsigned long long legacy = 500;   // 마지막 창 메시지는 오래전
    CHECK(!legacy_gate_step(s, 1000, true, legacy, 300));
    CHECK(!legacy_gate_step(s, 1200, true, legacy, 300));   // 200ms - 아직
    CHECK(!legacy_gate_step(s, 1299, true, legacy, 300));
    CHECK(legacy_gate_step(s, 1300, true, legacy, 300));    // 300ms 이어서 움직임
    // 멈춰도 판정은 남는다 - 가만히 있다 클릭해도 합성이 이어진다
    CHECK(legacy_gate_step(s, 5000, false, legacy, 300));
    CHECK(legacy_gate_step(s, 9000, false, legacy, 300));
    // 창 메시지가 오면 곧바로 산다(같은 프레임의 raw 는 버려져 휠이 두 번 안 간다)
    CHECK(!legacy_gate_step(s, 9016, false, 9010, 300));
    CHECK(!legacy_gate_step(s, 9032, true, 9010, 300));
    // 다시 끊기면 다시 죽는다 - 새 움직임부터 센다
    CHECK(!legacy_gate_step(s, 9500, true, 9010, 300));
    CHECK(!legacy_gate_step(s, 9700, true, 9010, 300));
    CHECK(legacy_gate_step(s, 9800, true, 9010, 300));
}

TEST(legacy_gate_restarts_the_count_after_a_pause) {
    LegacyGateState s;
    // 한 번도 창 메시지가 안 왔어도(0) 짧게 움직인 것만으로는 안 죽는다
    CHECK(!legacy_gate_step(s, 1000, true, 0, 300));
    CHECK(!legacy_gate_step(s, 1100, true, 0, 300));
    // 400ms 쉬고 다시 - 새 움직임이라 처음부터 센다
    CHECK(!legacy_gate_step(s, 1500, true, 0, 300));
    CHECK(!legacy_gate_step(s, 1700, true, 0, 300));
    CHECK(legacy_gate_step(s, 1800, true, 0, 300));
}

TEST(legacy_gate_boundaries_at_exactly_grace) {
    LegacyGateState s;
    // 마지막 창 메시지가 정확히 grace 전이면 아직 살아 있음(<=)
    CHECK(!legacy_gate_step(s, 1300, true, 1000, 300));
    // 1300 부터 이어진 움직임이 정확히 grace 가 되는 1600 에 끊김(>=), 창 메시지는 1000 (300 넘음)
    CHECK(!legacy_gate_step(s, 1450, true, 1000, 300));
    CHECK(legacy_gate_step(s, 1600, true, 1000, 300));
    // 정확히 grace 만큼 쉰 것은 같은 움직임(> 가 아님) - 1000 시작, 1300 에 이어진 지 300
    LegacyGateState t;
    CHECK(!legacy_gate_step(t, 1000, true, 0, 300));
    CHECK(legacy_gate_step(t, 1300, true, 0, 300));
    // 한 ms 라도 더 쉬면 새 움직임 - 처음부터
    LegacyGateState u;
    CHECK(!legacy_gate_step(u, 1000, true, 0, 300));
    CHECK(!legacy_gate_step(u, 1301, true, 0, 300));
}

TEST(raw_answered_by_a_window_message_around_its_arrival) {
    CHECK(raw_answered(1000, 1000, 32));    // 같은 틱
    CHECK(raw_answered(1000, 1016, 32));    // 다음 틱
    CHECK(raw_answered(1000, 5000, 32));    // 뒤에 온 창 메시지는 모두 답이다
    CHECK(raw_answered(1000, 968, 32));     // 틱 오차 안에서 앞서도 답이다(창 메시지가 먼저 풀린 경우)
    CHECK(!raw_answered(1000, 967, 32));    // 오차 밖의 옛 메시지는 답이 아니다
    CHECK(!raw_answered(1000, 0, 32));      // 한 번도 안 옴
}

TEST(legacy_gate_idle_keeps_alive) {
    LegacyGateState s;
    // 한 번도 안 움직였으면 살아 있음 - 창 메시지가 없어도 죽일 근거가 없다
    CHECK(!legacy_gate_step(s, 1000, false, 0, 300));
    CHECK(!legacy_gate_step(s, 9000, false, 0, 300));
    CHECK(!legacy_gate_step(s, 9000, false, 500, 300));
}

TEST(decode_raw_mouse_buttons_and_wheel) {
    // 왼 눌림(0x1), 오른 뗌(0x8), 가운데 눌림(0x10)
    RawMouseDecoded d = decode_raw_mouse(0x0001u | 0x0008u | 0x0010u, 0);
    CHECK_EQ(d.down, 0b00101u);
    CHECK_EQ(d.up, 0b00010u);
    CHECK_EQ(d.wheel, 0);
    CHECK_EQ(d.hwheel, 0);
    // X1 눌림(0x40), X2 뗌(0x200) → ImGui 버튼 3·4
    d = decode_raw_mouse(0x0040u | 0x0200u, 0);
    CHECK_EQ(d.down, 0b01000u);
    CHECK_EQ(d.up, 0b10000u);
    // 세로 휠 뒤로: 데이터는 부호 있는 16비트
    d = decode_raw_mouse(0x0400u, static_cast<unsigned short>(-120));
    CHECK_EQ(d.wheel, -120);
    CHECK_EQ(d.hwheel, 0);
    CHECK_EQ(d.down, 0u);
    CHECK_EQ(d.up, 0u);
    // 가로 휠
    d = decode_raw_mouse(0x0800u, 120);
    CHECK_EQ(d.hwheel, 120);
    CHECK_EQ(d.wheel, 0);
    // 이동만 있는 보고: 플래그가 없으면 데이터는 무시
    d = decode_raw_mouse(0, 120);
    CHECK_EQ(d.wheel, 0);
    CHECK_EQ(d.hwheel, 0);
    CHECK_EQ(d.down, 0u);
}

// ------------------------------------------------- 좌표 주입은 백엔드에 양보한다
// 실측(2026-09-16 전투): 백엔드가 **모든 프레임**에 좌표를 넣고 있었고, 우리가
// 덧붙인 좌표와 2136 프레임 중 433 프레임이 어긋났다(최대 59.8px). 둘 다 커서를
// 재지만 **잰 순간이 달라** 매 프레임 다른 값이 나오고, 어느 쪽이 최종값이 될지는
// 큐 도착 순서가 정한다 - 그래서 떨림·굳음·의도치 않은 드래그가 함께 났다.
TEST(pos_injection_yields_when_backend_already_queued) {
    CHECK(!cdtb::input::should_inject_mouse_pos(true));
}

// 백엔드가 조용한 프레임(마우스룩에서 WM_MOUSEMOVE 가 끊긴 경우)에는 우리가
// 넣어야 한다 - 그게 2026-09-12 에 고친 "좌표가 멎는다" 의 해결이다.
TEST(pos_injection_covers_a_silent_backend) {
    CHECK(cdtb::input::should_inject_mouse_pos(false));
}

// ---------------------------------------- 숨김 동안 백엔드에 입력을 주지 않는다
// 오버레이가 꺼져 있어도 백엔드 핸들러를 부르면 ImGui 이벤트 큐에 **쌓이기만**
// 한다 - NewFrame 이 안 돌아 아무도 안 비운다. 켜는 순간 그 밀린 것이 재생돼
// 켜기 전의 손놀림과 클릭이 되풀이된다(사용자 보고 2026-09-16, 전투에서 최악).
TEST(backend_is_denied_input_while_hidden) {
    using cdtb::input::backend_should_see;
    CHECK(!backend_should_see(0x0200, false));   // WM_MOUSEMOVE
    CHECK(!backend_should_see(0x0201, false));   // WM_LBUTTONDOWN
    CHECK(!backend_should_see(0x020A, false));   // WM_MOUSEWHEEL
    CHECK(!backend_should_see(0x02A3, false));   // WM_MOUSELEAVE
    CHECK(!backend_should_see(0x0100, false));   // WM_KEYDOWN
    CHECK(!backend_should_see(0x0102, false));   // WM_CHAR
    CHECK(!backend_should_see(0x010F, false));   // WM_IME_COMPOSITION
    CHECK(!backend_should_see(0x0286, false));   // WM_IME_CHAR
}

// 창 살림 메시지는 숨김 동안에도 넘겨야 한다 - 안 넘기면 백엔드의 크기·포커스
// 상태가 실제 창과 어긋난 채로 오버레이가 열린다.
TEST(backend_still_sees_window_housekeeping_while_hidden) {
    using cdtb::input::backend_should_see;
    CHECK(backend_should_see(0x0005, false));   // WM_SIZE
    CHECK(backend_should_see(0x0007, false));   // WM_SETFOCUS
    CHECK(backend_should_see(0x0008, false));   // WM_KILLFOCUS
    CHECK(backend_should_see(0x02E0, false));   // WM_DPICHANGED
}

// 켜져 있으면 전부 넘긴다 - 그때는 NewFrame 이 돌아 큐가 비워진다.
TEST(backend_sees_everything_while_visible) {
    using cdtb::input::backend_should_see;
    CHECK(backend_should_see(0x0200, true));
    CHECK(backend_should_see(0x0100, true));
    CHECK(backend_should_see(0x0005, true));
}

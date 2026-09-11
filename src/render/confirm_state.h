#pragma once

// 게임 메모리에 쓰는 버튼의 2단 확인. 첫 클릭에 무장(라벨이 "정말? (N초)" 로
// 바뀜), window 초 안에 다시 누르면 발화, 지나면 스스로 풀린다. 한 번에
// 하나만 무장한다 - 다른 버튼을 누르면 그쪽이 무장하고 앞의 것은 풀린다.
namespace cdtb::render {

struct ConfirmState {
    unsigned id = 0;          // 무장한 버튼. 0 이면 없음
    double armed_at = -1.0;
};

enum class ConfirmStep { Idle, Armed, Fired };

// clicked: 이 프레임에 그 버튼(id)이 눌렸는가.
ConfirmStep confirm_step(ConfirmState* s, unsigned id, bool clicked, double now,
                         double window = 3.0);

// 그 버튼이 무장 중이면 남은 초, 아니면 0.
double confirm_left(const ConfirmState& s, unsigned id, double now,
                    double window = 3.0);

}  // namespace cdtb::render

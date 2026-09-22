#include "render/item_style.h"

#include <cstdio>
#include <string_view>

#include "game/item_text.h"
#include "game/items.h"

namespace cdtb::render {

ImVec4 grade_color(std::uint8_t grade) {
    switch (grade) {
        case 1: return ImVec4(155 / 255.f, 155 / 255.f, 155 / 255.f, 1.f);
        case 2: return ImVec4(94 / 255.f, 170 / 255.f, 94 / 255.f, 1.f);
        case 3: return ImVec4(91 / 255.f, 141 / 255.f, 217 / 255.f, 1.f);
        case 4: return ImVec4(168 / 255.f, 85 / 255.f, 247 / 255.f, 1.f);
        case 5: return ImVec4(245 / 255.f, 158 / 255.f, 11 / 255.f, 1.f);
        default: return ImVec4(0.55f, 0.55f, 0.55f, 1.f);
    }
}

// 분류 번호(+0xA3, 74종)의 이름.
//
// crimsondb.gg 의 카드 유형과 대조해 얻었다. 표본 1,379개를 아이템
// 표와 이름으로 맞춘 뒤, 분류값마다 어떤 유형이 걸리는지 셌다.
// 37종은 한 유형이 100% 를 차지했고, 몇 종은 여러 유형이 섞였다.
// 섞인 것은 묶어서 적는다 - 게임의 분류가 사이트보다 거칠어서다.
//
// **나머지 30종은 번호로 둔다.** 재료·소비·기타 카테고리는 사이트
// 카드에 유형 자체가 실려 있지 않아 대조할 것이 없다. 이름 표본으로
// 짐작할 수는 있으나(19=약초, 26=곤충, 48=조리법 ...) 추측으로 붙이면
// 조용히 틀린 표가 된다.
const char* category_name(std::uint8_t c) {
    switch (c) {
        // --- 방어구 ---
        case 3: return "갑옷·망토";      // 갑옷155 / 망토97 이 섞인다
        case 24: return "투구";
        case 22: return "장갑";
        case 9: return "신발";
        case 21: return "안경";
        case 30: return "복면";
        // --- 무기 ---
        case 5: return "한손도끼";
        case 10: return "활";
        case 11: return "석궁";
        case 12: return "단검";
        case 23: return "양손대포";
        case 29: return "한손둔기";
        case 33: return "장총";
        case 40: return "피스톨";
        case 47: return "레이피어";
        case 53: return "샷건";
        case 56: return "한손검";
        case 64: return "양손할버드";
        case 65: return "양손도끼";
        case 68: return "거대양손검";
        case 69: return "양손망치";
        case 72: return "양손검";
        case 73: return "양손 워해머";
        case 18: return "한손 특수";     // 대포·부채·주먹·드릴·건틀렛
        case 70: return "양손창·파이크";
        case 71: return "방사기";
        // --- 방패 ---
        case 52: return "한손방패";
        case 60: return "대형방패";
        // --- 악세서리 ---
        case 15: return "귀걸이";
        case 34: return "목걸이";
        case 49: return "반지";
        // --- 탈것·펫 ---
        case 25: return "탈것 장비";     // 마갑·등자·안장·편자·마면
        case 14: return "드래곤갑옷";
        case 38: return "펫의상";
        case 39: return "펫투구";
        case 104: return "펫악세사리";
        // --- 소비·재료 (게임 안 항목을 직접 확인해 붙였다) ---
        case 0: return "탄환";          // 편전·화살·포탄·총탄
        case 16: return "낚시";         // 송사리·미꾸라지·참서대
        case 19: return "재료·요리";    // 약초·고기·곡물
        case 26: return "곤충";         // 잠자리·나비·거미
        case 28: return "투척품";       // 연막 폭탄·디코이 소환 볼
        case 41: return "비약";         // 성수·하급/중급 비약
        case 48: return "조리법·제작법";
        case 61: return "포장 교역품";  // 포장된 치즈·밀가루·양모
        case 62: return "교역품";       // 치즈·밀가루·소금·후추
        // --- 그 외 ---
        case 8: return "서적";          // 세계의 무기 일람·제작법
        case 27: return "열쇠";
        case 31: return "하우징";       // 평작·걸작·습작, 요리용 솥
        case 63: return "보물 지도";
        case 74: return "심연 장비";    // 파괴 I·간파 I. 개수 190 이 사이트와 같다
        case 102: return "어비스 장치"; // 전송 장치·유적 기둥·동력핵
        case 50: return "A.T.A.G.";
        case 54: return "가방·보금자리";
        // --- 남은 것도 내용을 보고 붙였다. 번호만 남으면 무엇인지
        //     알 수 없어 목록에서 쓸모가 없다. ---
        case 1: return "양서류";        // 독개구리·청개구리·두꺼비
        case 2: return "동물";          // 다람쥐·두더지·도마뱀·새
        case 4: return "어비스 아티팩트";
        case 7: return "기억";          // 망국의 기억
        case 13: return "어비스 효과";  // 어비스의 숨결·생명 증폭
        case 32: return "화폐·자원";    // 동화·캠프 자금·톱니
        case 35: return "묶음·주머니";  // 화살 묶음·동화 주머니
        case 36: return "편지·일지";
        case 37: return "허가증";       // 에르난드 통행 허가증
        case 42: return "의뢰서";       // 잃어버린 소·초대장
        case 43: return "기록";         // 대서고의 기록·관찰일지
        case 44: return "지역 열쇠";    // 저택·감옥·요새 열쇠
        case 45: return "게시물";       // 토벌 소식·목격담·경고문
        case 46: return "서신·장부";    // 영수증·보고서·증명서
        case 103: return "제작 부품";   // 동력핵·드릴 부품
        // --- 도구·기타 ---
        case 6: return "가방";
        case 17: return "낚싯대";
        case 57: return "랜턴";
        case 58: return "도구";          // 나팔·도끼·갈퀴 등
        case 59: return "횃불";
        case 55: return "분무기 등짐";
        default: return nullptr;
    }
}

void desc_cell(const std::string& desc) {
    if (desc.empty()) return;
    const std::string_view line = game::desc_first_line(desc);
    ImGui::TextUnformatted(line.data(), line.data() + line.size());
    // BeginItemTooltip 은 잠깐 멈췄을 때만 뜬다(ImGuiHoveredFlags_ForTooltip) - 줄을 훑으며
    // 지나갈 때마다 큰 툴팁이 깜빡이지 않는다. 설명은 최장 503바이트라 줄바꿈이 필요하다.
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(desc.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}


// --- 필터 줄 ---------------------------------------------------------

const char* const kGradeLabels = "전체\0" "등급 없음\0" "T1\0" "T2\0"
                                 "T3\0" "T4\0" "T5\0";

// game::EquipOwner 차례 그대로다. 색인 0 만 "전체" 로 앞에 붙는다.
const char* const kOwnerLabels = "전체\0" "공용\0" "클리프\0" "데미안\0"
                                 "웅카\0";

float text_width(const char* s) { return ImGui::CalcTextSize(s).x; }

void flow_same_line(float next_width) {
    // 오른쪽 끝은 창이 아니라 지금 그리는 영역(표 칸이면 그 칸)으로 잰다 -
    // 장비 창 소켓 칸이 버튼 다섯을 칸 안에서 흘린다.
    const float right = ImGui::GetCursorScreenPos().x +
                        ImGui::GetContentRegionAvail().x;
    const float end = ImGui::GetItemRectMax().x +
                      ImGui::GetStyle().ItemSpacing.x + next_width;
    if (end < right) ImGui::SameLine();
}

void build_category_labels(std::vector<std::uint8_t>* values,
                           std::string* labels) {
    if (values == nullptr || labels == nullptr) return;
    values->clear();
    labels->clear();
    labels->append("전체").push_back('\0');

    bool seen[256] = {};
    for (const auto& e : game::item_catalog()) seen[e.category] = true;
    for (int c = 0; c < 256; ++c) {
        if (!seen[c]) continue;
        values->push_back(static_cast<std::uint8_t>(c));
        char buf[48];
        const char* nm = category_name(static_cast<std::uint8_t>(c));
        if (nm != nullptr) {
            std::snprintf(buf, sizeof(buf), "%d (%s)", c, nm);
        } else {
            std::snprintf(buf, sizeof(buf), "%d", c);
        }
        labels->append(buf).push_back('\0');
    }
    labels->push_back('\0');
}

}  // namespace cdtb::render

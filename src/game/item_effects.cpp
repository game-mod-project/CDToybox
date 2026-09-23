#include "game/item_effects.h"

#include <cmath>
#include <functional>
#include <unordered_map>
#include <utility>

#include "game/buff_param_table.h"
#include "game/effect_format.h"
#include "game/items.h"   // find_item_manager
#include "game/roster.h"  // find_static_manager

namespace cdtb::game {
namespace {

// 매니저 머리(개수 · 레코드 포인터 배열).
struct Table {
    std::uint32_t count = 0;
    std::uintptr_t records = 0;

    bool ok() const { return count != 0 && records != 0; }
};

bool read_table(const mem::Reader& r, std::uintptr_t manager,
                std::size_t count_off, Table* out) {
    std::uint32_t n = 0;
    std::uint64_t recs = 0;
    if (!r.read_value(manager + count_off, &n)) return false;
    if (!r.read_value(manager + kEffectMgrRecords, &recs)) return false;
    // 후보를 잘못 집으면 개수가 쓰레기값이다. 그대로 믿고 돌면 안 끝난다.
    if (n == 0 || n > kMaxEffectRows || recs == 0) return false;
    out->count = n;
    out->records = static_cast<std::uintptr_t>(recs);
    return true;
}

// 배열 길이가 말이 되는가. 길이 칸이 쓰레기면 그 목록만 버린다.
bool sane_len(std::uint32_t n) { return n != 0 && n <= kMaxEffectListLen; }

// 패턴 하나(형식 문자열 + 파라미터 목록). 행마다 한 번만 푼다.
struct PatternParam {
    std::uint8_t type = 0;
    // `_isDisplayAbsoluteNumber`. 절대값 표시는 형식 문자열의 토큰 꼴
    // (`{|ParamN|}` = 종류 N+4)이 이미 말해 주므로 우리는 안 쓴다 - 읽는 것은
    // 항목이 2바이트임을 코드에 남겨 두기 위해서다(§4.7-E).
    std::uint8_t show_abs = 0;
};

struct Pattern {
    bool ok = false;
    std::string format;
    std::vector<PatternParam> params;
};

// 표 다섯을 걷는 일꾼. 캐시를 들고 있어 한 번 만들고 버린다.
//
// **비용**(§4.7-G): 6816 아이템을 한 번 걷는다. 같은 스킬 레벨·버프 레벨이
// 아이템마다 되풀이되고(한 아이템 안에서도 문맥 3벌이 같은 스킬을 가리킨다)
// 형식 문자열은 352개뿐이라, 캐시 셋으로 실제 역참조를 줄인다:
//   1. 매니저 머리 다섯은 처음에 한 번만 읽는다.
//   2. 패턴(현지화 조회 + 파라미터 목록)은 **행마다 한 번** 푼다. 현지화
//      조회가 카테고리 54개를 훑는 이분 탐색이라 제일 비싸다.
//   3. (스킬행, 레벨) · (버프행, 레벨) -> 완성된 줄들을 통째로 캐시한다.
//      바깥쪽 BuffData 가 그 쌍으로 정해지므로 지속시간까지 같다.
// 셋 다 지연 생성이라 안 쓰는 행은 손대지 않는다.
class Walker {
public:
    Walker(const mem::Reader& reader, const LocSystem& loc,
           const StatNames& names, const EffectManagers& mgr)
        : r_(reader), loc_(loc) {
        name_of_ = [&names](std::string_view table, std::string_view key) {
            return names.name_of(table, key);
        };
        ok_ = read_table(r_, mgr.item, kItemMgrCount, &item_) &&
              read_table(r_, mgr.item_use, kEffectMgrCount, &use_) &&
              read_table(r_, mgr.skill, kEffectMgrCount, &skill_) &&
              read_table(r_, mgr.buff, kEffectMgrCount, &buff_) &&
              read_table(r_, mgr.pattern, kEffectMgrCount, &pattern_);
    }

    bool ok() const { return ok_; }

    bool run(std::vector<ItemEffects>* out) {
        std::vector<ItemEffects> result(item_.count);
        for (std::uint32_t row = 0; row < item_.count; ++row) {
            const std::uintptr_t rec = record_at(item_, row);
            if (rec == 0) continue;   // 빈 슬롯. 나머지는 계속 걷는다.
            walk_uses(rec, &result[row]);
            walk_enchants(rec, &result[row]);
        }
        *out = std::move(result);
        return true;
    }

private:
    // 레코드 포인터 배열에서 한 행. 범위 밖이거나 널이면 0.
    std::uintptr_t record_at(const Table& t, std::uint32_t row) const {
        if (row >= t.count) return 0;
        std::uint64_t rec = 0;
        if (!r_.read_value(t.records + static_cast<std::uintptr_t>(row) * 8,
                           &rec)) {
            return 0;
        }
        return static_cast<std::uintptr_t>(rec);
    }

    // --- 소모품 (§4.7-B) ---
    void walk_uses(std::uintptr_t record, ItemEffects* out) {
        std::uint64_t list = 0;
        std::uint32_t n = 0;
        if (!r_.read_value(record + kItemUseList, &list) || list == 0) return;
        // +0x88 은 하위 u32 가 개수다(상위는 용량).
        if (!r_.read_value(record + kItemUseCount, &n) || !sane_len(n)) return;

        for (std::uint32_t i = 0; i < n; ++i) {
            std::uint32_t use_row = 0;
            if (!r_.read_value(static_cast<std::uintptr_t>(list) + i * 4,
                               &use_row)) {
                continue;
            }
            const std::uintptr_t use_rec = record_at(use_, use_row);
            if (use_rec == 0) continue;

            std::uint64_t data = 0;
            if (!r_.read_value(use_rec + kUseRecData, &data) || data == 0) {
                continue;
            }
            const auto use_data = static_cast<std::uintptr_t>(data);

            // 스킬 사용이 아닌 것(먹이 주기 · 슬롯 등록)은 효과로 치지 않는다.
            std::uint8_t kind = 0;
            if (!r_.read_value(use_data + kUseDataKind, &kind) ||
                kind != kUseKindSkill) {
                continue;
            }
            // 같은 스킬이 문맥만 달리해 세 번 등록된다(0x00/0x07/0x0C).
            // **0x00 만 툴팁 줄이다** - 안 거르면 줄이 세 벌씩 나온다.
            std::uint8_t ctx = 0;
            if (!r_.read_value(use_data + kUseDataContext, &ctx) ||
                ctx != kUseContextTooltip) {
                continue;
            }

            std::uint64_t pairs = 0;
            std::uint32_t pn = 0;
            if (!r_.read_value(use_data + kUseDataPairs, &pairs) ||
                pairs == 0) {
                continue;
            }
            if (!r_.read_value(use_data + kUseDataPairCount, &pn) ||
                !sane_len(pn)) {
                continue;
            }
            for (std::uint32_t j = 0; j < pn; ++j) {
                const std::uintptr_t at =
                    static_cast<std::uintptr_t>(pairs) + j * 8;
                std::uint32_t skill_row = 0;
                std::uint32_t level = 0;   // 1-기반
                if (!r_.read_value(at, &skill_row)) continue;
                if (!r_.read_value(at + 4, &level)) continue;
                append(out, skill_lines(skill_row, level));
            }
        }
    }

    // --- 어비스 기어 (§4.7-C) ---
    void walk_enchants(std::uintptr_t record, ItemEffects* out) {
        std::uint64_t list = 0;
        std::uint32_t n = 0;
        if (!r_.read_value(record + kItemEnchantList, &list) || list == 0) {
            return;
        }
        if (!r_.read_value(record + kItemEnchantCount, &n) || !sane_len(n)) {
            return;
        }
        for (std::uint32_t i = 0; i < n; ++i) {
            // EnchantData 는 포인터가 아니라 **통째로** 들어 있다(0x70 스트라이드).
            const std::uintptr_t ed =
                static_cast<std::uintptr_t>(list) + i * kEnchantStride;
            std::uint64_t buffs = 0;
            std::uint32_t bn = 0;
            if (!r_.read_value(ed + kEnchantBuffs, &buffs) || buffs == 0) {
                continue;   // `_equipBuffs` 가 없는 장비는 여기 안 걸린다(§4.7-H 4번)
            }
            if (!r_.read_value(ed + kEnchantBuffCount, &bn) || !sane_len(bn)) {
                continue;
            }
            for (std::uint32_t j = 0; j < bn; ++j) {
                const std::uintptr_t at = static_cast<std::uintptr_t>(buffs) +
                                          j * kEnchantBuffStride;
                // **하위 u16 만 행 번호다** - 상위 바이트는 잔여물이다.
                std::uint16_t buff_row = 0;
                std::int32_t level = 0;
                if (!r_.read_value(at + kEnchantBuffRow, &buff_row)) continue;
                if (!r_.read_value(at + kEnchantBuffLevel, &level)) continue;
                append(out, buff_lines(buff_row, level));
            }
        }
    }

    // --- 캐시 (스킬행, 레벨) -> 줄들 ---
    const ItemEffects& skill_lines(std::uint32_t row, std::uint32_t level) {
        const std::uint64_t key =
            (static_cast<std::uint64_t>(row) << 32) | level;
        const auto it = skill_cache_.find(key);
        if (it != skill_cache_.end()) return it->second;

        ItemEffects e;
        const std::uintptr_t rec = record_at(skill_, row);
        std::uint64_t list = 0;
        std::uint32_t n = 0;
        if (rec != 0 && level != 0 &&
            r_.read_value(rec + kSkillLevels, &list) && list != 0 &&
            r_.read_value(rec + kSkillLevelCount, &n) && sane_len(n) &&
            level <= n) {
            // 레벨은 1-기반이라 칸은 (레벨 - 1) 이다.
            const std::uintptr_t slot = static_cast<std::uintptr_t>(list) +
                                        (level - 1) * kSkillLevelStride;
            std::uint64_t arr = 0;
            std::uint32_t cnt = 0;
            if (r_.read_value(slot, &arr) && arr != 0 &&
                r_.read_value(slot + 8, &cnt) && sane_len(cnt)) {
                for (std::uint32_t k = 0; k < cnt; ++k) {
                    std::uint64_t bd = 0;
                    if (!r_.read_value(
                            static_cast<std::uintptr_t>(arr) + k * 8, &bd) ||
                        bd == 0) {
                        continue;
                    }
                    emit(static_cast<std::uintptr_t>(bd), &e);
                }
            }
        }
        return skill_cache_.emplace(key, std::move(e)).first->second;
    }

    // --- 캐시 (버프행, 레벨) -> 줄들 ---
    const ItemEffects& buff_lines(std::uint16_t row, std::int32_t level) {
        const std::uint64_t key = (static_cast<std::uint64_t>(row) << 32) |
                                  static_cast<std::uint32_t>(level);
        const auto it = buff_cache_.find(key);
        if (it != buff_cache_.end()) return it->second;

        ItemEffects e;
        if (row != kNoPatternRow) {
            const std::uintptr_t bd = buff_level_data(row, level);
            if (bd != 0) {
                emit(bd, &e);
            } else {
                // 목록에 있는데 그 레벨을 못 찾았다 - 게임은 줄을 보인다.
                ++e.unresolved;
            }
        }
        return buff_cache_.emplace(key, std::move(e)).first->second;
    }

    // BuffInfo 의 레벨 목록에서 그 레벨의 BuffData. 없으면 0.
    // **레벨이 음수일 수 있어 첨자가 아니라 값으로 찾는다.**
    std::uintptr_t buff_level_data(std::uint16_t row, std::int32_t level) const {
        const std::uintptr_t rec = record_at(buff_, row);
        if (rec == 0) return 0;
        std::uint64_t list = 0;
        std::uint32_t n = 0;
        if (!r_.read_value(rec + kBuffLevels, &list) || list == 0) return 0;
        if (!r_.read_value(rec + kBuffLevelCount, &n) || !sane_len(n)) return 0;
        for (std::uint32_t i = 0; i < n; ++i) {
            const std::uintptr_t slot =
                static_cast<std::uintptr_t>(list) + i * kBuffLevelStride;
            std::int32_t lv = 0;
            if (!r_.read_value(slot, &lv) || lv != level) continue;
            std::uint64_t bd = 0;
            if (!r_.read_value(slot + kBuffLevelData, &bd)) return 0;
            return static_cast<std::uintptr_t>(bd);
        }
        return 0;
    }

    // --- BuffData 한 칸 -> 한 줄 (§4.7-D · E) ---
    void emit(std::uintptr_t outer, ItemEffects* out) {
        // **지속시간은 바깥쪽 것이다** - 사슬을 따라가기 전에 읽어 둔다.
        std::uint32_t duration = 0;
        r_.read_value(outer + kBuffDataDuration, &duration);

        std::uintptr_t cur = outer;
        int depth = 1;
        std::uint16_t pattern_row = kNoPatternRow;
        for (;;) {
            // 하위 u16 만 행 번호다(상위 바이트는 잔여물).
            if (!r_.read_value(cur + kBuffDataPattern, &pattern_row)) return;
            if (pattern_row != kNoPatternRow) break;

            // 패턴이 없다. `ChangeBuffLevel` 이면 링크이므로 한 칸 더 간다.
            // 그 밖의 클래스는 툴팁 줄이 아니다(퀵슬롯 등록 등) - 조용히 뺀다.
            std::uint64_t vt = 0;
            if (!r_.read_value(cur + kBuffDataVtable, &vt)) return;
            if (vt != kChangeBuffLevelVtable) return;

            if (depth >= kMaxChainDepth) {
                ++out->unresolved;   // 너무 깊다(실측 최대 2단)
                return;
            }
            std::uint16_t link_row = 0;
            std::int32_t link_level = 0;
            if (!r_.read_value(cur + kBuffDataLinkRow, &link_row) ||
                !r_.read_value(cur + kBuffDataLinkLevel, &link_level)) {
                ++out->unresolved;
                return;
            }
            const std::uintptr_t next = buff_level_data(link_row, link_level);
            if (next == 0) {
                ++out->unresolved;
                return;
            }
            cur = next;
            ++depth;
        }

        const Pattern& p = pattern_of(pattern_row);
        if (!p.ok) {
            ++out->unresolved;   // 형식 문자열이 아직 안 풀렸다
            return;
        }

        EffectParams v;
        v.duration_ms = duration;
        for (const PatternParam& pp : p.params) {
            if (!fill(cur, pp.type, &v)) {
                // 배율을 모르는 조합이다. **줄을 통째로 버린다** -
                // 틀린 숫자를 보이는 것보다 낫다.
                ++out->unresolved;
                return;
            }
        }
        std::string text = effect_line(p.format, v, name_of_);
        if (text.empty()) {
            ++out->unresolved;
            return;
        }
        out->lines.push_back(ItemEffect{std::move(text), duration});
    }

    // 파라미터 한 종류의 값을 채운다. 규칙을 모르면 false.
    bool fill(std::uintptr_t bd, std::uint8_t type, EffectParams* v) const {
        if (type >= kEffectParamTypeCount) return false;

        if (type == kRepeatTickParamType) {
            // 종류 8 은 배율 표 밖이다 - 주기 ms 를 초로 바꾼다. 여기는
            // 실수 나눗셈이라 자르지 않는다(§4.7-H'' RVA 0x1F29A51).
            std::uint32_t ms = 0;
            if (!r_.read_value(bd + kBuffDataTick, &ms)) return false;
            v->set(type, static_cast<double>(ms) / kRepeatTickDivisor);
            return true;
        }

        std::uint64_t vtable = 0;
        std::uint8_t flag = 0;
        if (!r_.read_value(bd + kBuffDataVtable, &vtable)) return false;
        if (!r_.read_value(bd + kBuffDataFlag3A, &flag)) return false;
        const BuffParamRule* rule = buff_param_rule(vtable, type, flag);
        if (rule == nullptr || rule->divisor == 0.0) return false;

        // **4바이트로 읽는다**(8바이트로 읽으면 상위 절반이 잔여물이다).
        // 부호는 있다 - 게임이 `imul`/`sar` 로 나누고(§4.7-H''), 절대값 표시
        // 자리(`{|ParamN|}`)가 있는 것 자체가 음수가 온다는 뜻이다.
        std::int32_t raw = 0;
        if (!r_.read_value(bd + rule->offset, &raw)) return false;
        // 나눈 뒤 **0 쪽으로 자른다**: 실측 25000/10⁴ -> 2 · 75000/10⁴ -> 7
        // (반올림이면 3 · 8 이 됐을 것이다, §4.7-H 3번).
        v->set(type, std::trunc(static_cast<double>(raw) / rule->divisor));
        return true;
    }

    // --- 캐시 패턴 행 -> 형식 문자열 + 파라미터 목록 ---
    const Pattern& pattern_of(std::uint16_t row) {
        const auto it = patterns_.find(row);
        if (it != patterns_.end()) return it->second;

        Pattern p;
        const std::uintptr_t rec = record_at(pattern_, row);
        std::uint32_t entity = 0;
        if (rec != 0 && r_.read_value(rec + kPatternEntity, &entity) &&
            entity != 0 &&
            resolve(r_, loc_, loc_key(entity, kPatternFormatField), &p.format,
                    nullptr) &&
            !p.format.empty()) {
            p.ok = true;
            std::uint64_t list = 0;
            std::uint32_t n = 0;
            if (r_.read_value(rec + kPatternParams, &list) && list != 0 &&
                r_.read_value(rec + kPatternParamCount, &n) && sane_len(n)) {
                p.params.reserve(n);
                for (std::uint32_t i = 0; i < n; ++i) {
                    // 항목은 **2바이트** {종류, 절대값 표시}다.
                    std::uint8_t pair[kPatternParamStride] = {0, 0};
                    if (!r_.read(static_cast<std::uintptr_t>(list) +
                                     i * kPatternParamStride,
                                 pair, sizeof(pair))) {
                        continue;
                    }
                    p.params.push_back(PatternParam{pair[0], pair[1]});
                }
            }
        }
        return patterns_.emplace(row, std::move(p)).first->second;
    }

    static void append(ItemEffects* dst, const ItemEffects& src) {
        dst->lines.insert(dst->lines.end(), src.lines.begin(), src.lines.end());
        dst->unresolved += src.unresolved;
    }

    const mem::Reader& r_;
    const LocSystem& loc_;
    std::function<std::string(std::string_view, std::string_view)> name_of_;
    bool ok_ = false;
    Table item_;
    Table use_;
    Table skill_;
    Table buff_;
    Table pattern_;
    std::unordered_map<std::uint16_t, Pattern> patterns_;
    std::unordered_map<std::uint64_t, ItemEffects> skill_cache_;
    std::unordered_map<std::uint64_t, ItemEffects> buff_cache_;
};

}  // namespace

bool build_item_effects_from_managers(const mem::Reader& reader,
                                      const LocSystem& loc,
                                      const StatNames& names,
                                      const EffectManagers& mgr,
                                      std::vector<ItemEffects>* out) {
    if (out == nullptr) return false;
    if (!mgr.valid()) return false;
    // 현지화가 아직이면 형식 문자열이 하나도 안 풀린다. 조용히 물러나
    // 부르는 쪽이 재시도하게 한다(staged-data-load-retry).
    if (!loc.valid()) return false;

    Walker w(reader, loc, names, mgr);
    if (!w.ok()) return false;
    return w.run(out);
}

bool find_effect_managers(const mem::Rtti& rtti, const mem::Reader& reader,
                          EffectManagers* out) {
    if (out == nullptr) return false;
    EffectManagers m;
    if (!find_item_manager(rtti, reader, &m.item)) return false;
    if (!find_static_manager(reader, rtti, kItemUseManagerClass, &m.item_use)) {
        return false;
    }
    if (!find_static_manager(reader, rtti, kSkillManagerClass, &m.skill)) {
        return false;
    }
    if (!find_static_manager(reader, rtti, kBuffManagerClass, &m.buff)) {
        return false;
    }
    if (!find_static_manager(reader, rtti, kPatternManagerClass, &m.pattern)) {
        return false;
    }
    *out = m;
    return true;
}

bool build_item_effects(const mem::Rtti& rtti, const mem::Reader& reader,
                        const LocSystem& loc, const StatNames& names,
                        std::vector<ItemEffects>* out) {
    if (out == nullptr) return false;
    // 싼 관문부터 본다 - RTTI 힙 훑기가 제일 비싸다.
    if (!loc.valid()) return false;

    EffectManagers mgr;
    if (!find_effect_managers(rtti, reader, &mgr)) return false;
    return build_item_effects_from_managers(reader, loc, names, mgr, out);
}

}  // namespace cdtb::game

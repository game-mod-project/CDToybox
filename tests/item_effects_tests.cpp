#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "fake_memory.h"
#include "game/item_effects.h"
#include "game/localization.h"
#include "game/stat_names.h"
#include "harness.h"
#include "mem/rtti.h"

// 효과 걷기를 가짜 메모리 위에서 못박는다. 게임 없이 돈다.
//
// 세계는 명세 §4.7 의 실측 사슬을 그대로 본뜬다 - 매니저 다섯(+0x08 개수 ·
// +0x58 레코드 배열, 아이템만 +0x30 개수) · 소모품 갈래 · 어비스 갈래 ·
// PatternDescriptionInfo · 현지화. vtable 은 실측값을 그대로 써서 커밋된
// 배율 표(`buff_param_table.inc`)가 실제로 걸리게 한다.
//
// 여기서 나오는 세 줄은 게임 툴팁과 글자까지 맞춘 골든이다(§4.7-H'):
//   생명 최대치 45 증가(1분) · 생명 : 1 초마다 15 감소(20초) · 냉기 저항 Lv6(1분)
// 어비스 셋(2 · 5 · 7%)은 사용자 툴팁으로 확인된 값이다(§4.7-H 3번).

namespace {

using cdtb::game::EffectManagers;
using cdtb::game::ItemEffects;
using cdtb::game::LocSystem;
using cdtb::game::StatNames;
using cdtb::tests::FakeMemory;

// --- 실측 vtable (§4.7-H' 2번 · H''' · buff_param_table.inc) ---
constexpr std::uint64_t kVaryStatMaxValue = 0x1458FBD20ull;    // 종류 1 -> +0x98 ÷1000
constexpr std::uint64_t kDamage = 0x1458FC370ull;              // 종류 5 -> +0x98 ÷1000
constexpr std::uint64_t kSocketCritical = 0x1458FA908ull;      // 종류 1 -> +0x98 ÷10⁴
constexpr std::uint64_t kVaryStaticStatLevel = 0x1458FC1C0ull;  // 종류 1 -> +0xA0 ÷1
constexpr std::uint64_t kChangeBuffLevel = cdtb::game::kChangeBuffLevelVtable;
constexpr std::uint64_t kUnknownClass = 0xDEADBEEFull;   // 배율 표에 없는 클래스

// --- 패턴 행 (실측 행 번호를 그대로 쓴다) ---
constexpr std::uint16_t kPatHpMax = 5;       // '{Staticinfo:SubLevel:Hp} 최대치 {Param1} 증가'
constexpr std::uint16_t kPatHpTick = 4;      // 종류 8 + 종류 5 - 한 줄에 둘이다
constexpr std::uint16_t kPatIceLevel = 7;    // '냉기 저항 Lv{Param1}'
constexpr std::uint16_t kPatNoString = 12;   // 현지화에 없는 패턴
constexpr std::uint16_t kPatSocketCrit = 61;  // '천 갑옷 타격 시 치명타 확률 {Param1}% 증가'

// --- 버프 행 ---
constexpr std::uint16_t kBuffSocketCrit = 242;  // 관통 I·II·III (레벨 1·2·3)
constexpr std::uint16_t kBuffIce = 300;         // 링크가 닿는 곳. 레벨 **-2** 다
constexpr std::uint16_t kBuffChain1 = 301;      // 너무 깊은 사슬
constexpr std::uint16_t kBuffChain2 = 302;
constexpr std::uint16_t kBuffChain3 = 303;
constexpr std::uint16_t kBuffHop1 = 310;        // 두 칸짜리 사슬(허용 한계)
constexpr std::uint16_t kBuffHop2 = 311;

// 가짜 세계를 세우는 범프 할당기.
struct World {
    FakeMemory mem;
    std::size_t next = 0x100;   // 0 은 널과 헷갈리니 비워 둔다

    struct Tbl {
        std::size_t mgr = 0;
        std::size_t ptrs = 0;
        std::uint32_t count = 0;
    };

    // 현지화는 카테고리마다 **키 오름차순**이라야 이분 탐색이 산다.
    struct LocRow {
        int cat;
        std::uint64_t key;
        std::string text;
    };
    std::vector<LocRow> loc_rows;
    std::size_t loc_sys = 0;
    std::size_t loc_pool = 0;
    static constexpr std::uint32_t kLocPoolSize = 0x1000;

    World() { mem.heap.assign(0x80000, 0); }

    std::size_t alloc(std::size_t n) {
        const std::size_t at = (next + 15) & ~static_cast<std::size_t>(15);
        next = at + n;
        return at;
    }
    std::uintptr_t addr(std::size_t off) const { return mem.heap_addr(off); }

    // 매니저 하나. count_off 는 효과 매니저 넷이 0x08, 아이템 매니저가 0x30 이다.
    Tbl make_table(std::uint32_t count, std::size_t count_off) {
        Tbl t;
        t.mgr = alloc(0x80);
        t.ptrs = alloc(static_cast<std::size_t>(count) * 8);
        t.count = count;
        mem.put_u32(t.mgr + count_off, count);
        mem.put_u64(t.mgr + cdtb::game::kEffectMgrRecords, addr(t.ptrs));
        return t;
    }
    std::size_t make_record(const Tbl& t, std::uint32_t row, std::size_t size) {
        const std::size_t rec = alloc(size);
        mem.put_u64(t.ptrs + static_cast<std::size_t>(row) * 8, addr(rec));
        return rec;
    }

    // BuffData 한 칸. pattern_row 0xFFFF 면 패턴이 없다(링크이거나 툴팁 줄이 아니다).
    std::size_t buff_data(std::uint64_t vtable, std::uint32_t duration_ms,
                          std::uint16_t pattern_row) {
        const std::size_t bd = alloc(0x120);
        mem.put_u64(bd + cdtb::game::kBuffDataVtable, vtable);
        mem.put_u32(bd + cdtb::game::kBuffDataDuration, duration_ms);
        mem.put_u16(bd + cdtb::game::kBuffDataPattern, pattern_row);
        // +0x3A 는 배율 분기 선택자, +0x3B 는 잔여물이다. 상위 바이트를 채워
        // **하위 u16 만** 행 번호로 쓰는지 못박는다.
        mem.put_u8(bd + 0x3B, 0xFB);
        return bd;
    }
    void put_value(std::size_t bd, std::size_t off, std::int32_t v) {
        mem.put_u32(bd + off, static_cast<std::uint32_t>(v));
    }
    void put_link(std::size_t bd, std::uint16_t row, std::int32_t level) {
        // 상위 절반은 잔여물이다(§4.7-D).
        mem.put_u32(bd + cdtb::game::kBuffDataLinkRow,
                    (0xFBCDu << 16) | row);
        mem.put_u32(bd + cdtb::game::kBuffDataLinkLevel,
                    static_cast<std::uint32_t>(level));
    }

    // SkillInfo 레벨 목록. 칸 16바이트 {BuffData** 배열, u32 개수, u32 채움}.
    void set_skill(const Tbl& t, std::uint32_t row,
                   const std::vector<std::vector<std::size_t>>& levels) {
        const std::size_t rec = make_record(t, row, 0x40);
        const std::size_t list =
            alloc(levels.size() * cdtb::game::kSkillLevelStride);
        mem.put_u64(rec + cdtb::game::kSkillLevels, addr(list));
        mem.put_u32(rec + cdtb::game::kSkillLevelCount,
                    static_cast<std::uint32_t>(levels.size()));
        for (std::size_t i = 0; i < levels.size(); ++i) {
            const std::size_t arr = alloc(levels[i].size() * 8 + 8);
            for (std::size_t k = 0; k < levels[i].size(); ++k) {
                mem.put_u64(arr + k * 8, addr(levels[i][k]));
            }
            const std::size_t slot = list + i * cdtb::game::kSkillLevelStride;
            mem.put_u64(slot, addr(arr));
            mem.put_u32(slot + 8, static_cast<std::uint32_t>(levels[i].size()));
        }
    }

    // BuffInfo 레벨 목록. 칸 16바이트 {i32 레벨, u32 잔여물, BuffData*}.
    void set_buff(const Tbl& t, std::uint16_t row,
                  const std::vector<std::pair<std::int32_t, std::size_t>>& levels) {
        const std::size_t rec = make_record(t, row, 0x40);
        const std::size_t list =
            alloc(levels.size() * cdtb::game::kBuffLevelStride);
        mem.put_u64(rec + cdtb::game::kBuffLevels, addr(list));
        mem.put_u32(rec + cdtb::game::kBuffLevelCount,
                    static_cast<std::uint32_t>(levels.size()));
        for (std::size_t i = 0; i < levels.size(); ++i) {
            const std::size_t slot = list + i * cdtb::game::kBuffLevelStride;
            mem.put_u32(slot, static_cast<std::uint32_t>(levels[i].first));
            mem.put_u32(slot + 4, 0xDEADu);   // 잔여물
            mem.put_u64(slot + cdtb::game::kBuffLevelData, addr(levels[i].second));
        }
    }

    // PatternDescriptionInfo. text 가 널이면 현지화에 안 넣는다(안 풀리는 패턴).
    void set_pattern(const Tbl& t, std::uint16_t row, std::uint32_t entity,
                     const char* text,
                     const std::vector<std::pair<std::uint8_t, std::uint8_t>>& params) {
        const std::size_t rec = make_record(t, row, 0x60);
        mem.put_u32(rec + cdtb::game::kPatternEntity, entity);
        if (!params.empty()) {
            const std::size_t list =
                alloc(params.size() * cdtb::game::kPatternParamStride + 8);
            mem.put_u64(rec + cdtb::game::kPatternParams, addr(list));
            mem.put_u32(rec + cdtb::game::kPatternParamCount,
                        static_cast<std::uint32_t>(params.size()));
            for (std::size_t i = 0; i < params.size(); ++i) {
                // 항목은 2바이트 {종류, 절대값 표시}다.
                const std::size_t at =
                    list + i * cdtb::game::kPatternParamStride;
                mem.put_u8(at, params[i].first);
                mem.put_u8(at + 1, params[i].second);
            }
        }
        if (text != nullptr) {
            add_loc(15, cdtb::game::loc_key(
                            entity, cdtb::game::kPatternFormatField), text);
        }
    }

    // ItemInfo 의 소모품 입구. +0x88 은 **하위 u32 가 개수**다(상위는 용량).
    void set_uses(std::size_t item_rec, const std::vector<std::uint32_t>& rows) {
        const std::size_t list = alloc(rows.size() * 4 + 8);
        mem.put_u64(item_rec + cdtb::game::kItemUseList, addr(list));
        mem.put_u32(item_rec + cdtb::game::kItemUseCount,
                    static_cast<std::uint32_t>(rows.size()));
        mem.put_u32(item_rec + cdtb::game::kItemUseCount + 4, 999);   // 용량
        for (std::size_t i = 0; i < rows.size(); ++i) {
            mem.put_u32(list + i * 4, rows[i]);
        }
    }

    // ItemUseInfo 레코드 하나를 만들어 그 행 번호를 돌려준다.
    std::uint32_t add_use(const Tbl& t, std::uint32_t row, std::uint8_t kind,
                          std::uint8_t context,
                          const std::vector<std::pair<std::uint32_t, std::uint32_t>>& pairs) {
        const std::size_t rec = make_record(t, row, 0x20);
        const std::size_t data = alloc(0x40);
        mem.put_u64(rec + cdtb::game::kUseRecData, addr(data));
        mem.put_u8(data + cdtb::game::kUseDataKind, kind);
        mem.put_u8(data + cdtb::game::kUseDataContext, context);
        const std::size_t arr = alloc(pairs.size() * 8 + 8);
        mem.put_u64(data + cdtb::game::kUseDataPairs, addr(arr));
        mem.put_u32(data + cdtb::game::kUseDataPairCount,
                    static_cast<std::uint32_t>(pairs.size()));
        for (std::size_t i = 0; i < pairs.size(); ++i) {
            mem.put_u32(arr + i * 8, pairs[i].first);
            mem.put_u32(arr + i * 8 + 4, pairs[i].second);
        }
        return row;
    }

    // 어비스 입구. EnchantData 는 포인터가 아니라 0x70 스트라이드 인라인이다.
    void set_enchants(
        std::size_t item_rec,
        const std::vector<std::vector<std::pair<std::uint16_t, std::int32_t>>>& enchants) {
        const std::size_t list =
            alloc(enchants.size() * cdtb::game::kEnchantStride);
        mem.put_u64(item_rec + cdtb::game::kItemEnchantList, addr(list));
        mem.put_u32(item_rec + cdtb::game::kItemEnchantCount,
                    static_cast<std::uint32_t>(enchants.size()));
        for (std::size_t i = 0; i < enchants.size(); ++i) {
            const std::size_t ed = list + i * cdtb::game::kEnchantStride;
            if (enchants[i].empty()) continue;
            const std::size_t buffs =
                alloc(enchants[i].size() * cdtb::game::kEnchantBuffStride);
            mem.put_u64(ed + cdtb::game::kEnchantBuffs, addr(buffs));
            mem.put_u32(ed + cdtb::game::kEnchantBuffCount,
                        static_cast<std::uint32_t>(enchants[i].size()));
            for (std::size_t j = 0; j < enchants[i].size(); ++j) {
                const std::size_t at =
                    buffs + j * cdtb::game::kEnchantBuffStride;
                // **상위 바이트는 잔여물이다** - 하위 u16 만 행 번호다.
                mem.put_u32(at + cdtb::game::kEnchantBuffRow,
                            (0xFBCDu << 16) | enchants[i][j].first);
                mem.put_u32(at + cdtb::game::kEnchantBuffLevel,
                            static_cast<std::uint32_t>(enchants[i][j].second));
            }
        }
    }

    // --- 현지화 ---
    void add_loc(int cat, std::uint64_t key, const std::string& text) {
        loc_rows.push_back(LocRow{cat, key, text});
    }

    // 카테고리별로 키 순으로 세운다. 항목 +0x10 키 · +0x18 풀 오프셋 · +0x1C 카테고리.
    void finish_loc() {
        loc_sys = alloc(0x80);
        const std::size_t cats = alloc(cdtb::game::kLocCategoryCount * 16);
        loc_pool = alloc(kLocPoolSize);
        mem.put_u64(loc_sys + 0x48, addr(cats));
        mem.put_u64(loc_sys + 0x58, addr(loc_pool));
        mem.put_u32(loc_sys + 0x60, kLocPoolSize);

        std::sort(loc_rows.begin(), loc_rows.end(),
                  [](const LocRow& a, const LocRow& b) {
                      return a.cat != b.cat ? a.cat < b.cat : a.key < b.key;
                  });
        std::size_t slot = 0;   // 풀 칸 번호
        std::size_t i = 0;
        while (i < loc_rows.size()) {
            const int cat = loc_rows[i].cat;
            std::size_t j = i;
            while (j < loc_rows.size() && loc_rows[j].cat == cat) ++j;
            const std::size_t ptrs = alloc((j - i) * 8);
            for (std::size_t k = i; k < j; ++k) {
                const std::size_t entry = alloc(0x20);
                mem.put_u64(ptrs + (k - i) * 8, addr(entry));
                mem.put_u64(entry + 0x10, loc_rows[k].key);
                mem.put_u32(entry + 0x18, static_cast<std::uint32_t>(slot * 0x80));
                mem.put_u8(entry + 0x1C, static_cast<std::uint8_t>(cat));
                mem.put_str(loc_pool + slot * 0x80, loc_rows[k].text.c_str());
                ++slot;
            }
            mem.put_u64(cats + cat * 16, addr(ptrs));
            mem.put_u32(cats + cat * 16 + 8, static_cast<std::uint32_t>(j - i));
            i = j;
        }
    }

    LocSystem loc() const {
        LocSystem s;
        s.object = addr(loc_sys);
        s.pool = addr(loc_pool);
        s.pool_size = kLocPoolSize;
        return s;
    }

    // 엔진 문자열 객체 {char* +0x00, u32 길이 +0x08, u32 해시 +0x0C}.
    std::size_t engine_string(const char* s, std::uint32_t hash) {
        const std::size_t obj = alloc(0x20);
        const std::size_t chars = alloc(std::strlen(s) + 1);
        mem.put_u64(obj, addr(chars));
        mem.put_u32(obj + 0x08, static_cast<std::uint32_t>(std::strlen(s)));
        mem.put_u32(obj + 0x0C, hash);
        mem.put_str(chars, s);
        return obj;
    }
};

// 표준 세계. 아이템 행마다 다른 갈래를 태운다.
struct Fixture {
    enum Row : std::uint32_t {
        kRowConsumable = 0,   // 문맥 3벌 · 줄 둘
        kRowLinked = 1,       // ChangeBuffLevel 한 칸 · 바깥쪽 지속시간
        kRowAbyss = 2,        // _equipBuffs 셋 + 둘째 EnchantData
        kRowUnknown = 3,      // 배율 모르는 클래스
        kRowNone = 4,         // 효과 없음
        kRowDeep = 5,         // 사슬이 너무 깊다
        kRowTwoHop = 6,       // 두 칸(허용 한계)
        kRowNonSkill = 7,     // 스킬이 아닌 사용
        kRowBadPattern = 8,   // 형식 문자열이 안 풀린다
        kRowShared = 9,       // kRowConsumable 과 같은 스킬(캐시)
        kItemCount = 10,
    };

    World w;
    StatNames names;
    EffectManagers mgr;

    Fixture() {
        World::Tbl item = w.make_table(kItemCount, cdtb::game::kItemMgrCount);
        World::Tbl use = w.make_table(16, cdtb::game::kEffectMgrCount);
        World::Tbl skill = w.make_table(8, cdtb::game::kEffectMgrCount);
        World::Tbl buff = w.make_table(320, cdtb::game::kEffectMgrCount);
        World::Tbl pattern = w.make_table(64, cdtb::game::kEffectMgrCount);

        // --- 패턴 ---
        w.set_pattern(pattern, kPatHpMax, 0x5000,
                      "{Staticinfo:SubLevel:Hp} 최대치 {Param1} 증가", {{1, 0}});
        // 한 줄에 파라미터 종류가 **둘**이다(실측 행 4 = [(8,0),(5,1)]).
        w.set_pattern(pattern, kPatHpTick, 0x4000,
                      "{Staticinfo:SubLevel:Hp} : {RepeatTick} 초마다 {|Param1|} 감소",
                      {{8, 0}, {5, 1}});
        w.set_pattern(pattern, kPatIceLevel, 0x7000, "냉기 저항 Lv{Param1}",
                      {{1, 0}});
        w.set_pattern(pattern, kPatSocketCrit, 0x6100,
                      "천 갑옷 타격 시 치명타 확률 {Param1}% 증가", {{1, 0}});
        w.set_pattern(pattern, kPatNoString, 0xC000, nullptr, {{1, 0}});

        // --- 소모품 사슬: 스킬 1 레벨 1 = 줄 둘 ---
        const std::size_t hp_max = w.buff_data(kVaryStatMaxValue, 90000, kPatHpMax);
        w.put_value(hp_max, 0x98, 45000);        // ÷1000 -> 45
        const std::size_t hp_tick = w.buff_data(kDamage, 20000, kPatHpTick);
        w.put_value(hp_tick, 0x98, -15000);      // ÷1000 -> -15, {|Param1|} 로 15
        w.mem.put_u32(hp_tick + cdtb::game::kBuffDataTick, 1000);   // 1초
        w.set_skill(skill, 1, {{hp_max, hp_tick}});

        // --- 링크: ChangeBuffLevel -> 버프 300 레벨 **-2** ---
        const std::size_t ice = w.buff_data(kVaryStaticStatLevel, 5000, kPatIceLevel);
        w.put_value(ice, 0xA0, 6);               // ÷1 -> Lv6
        const std::size_t ice_decoy =
            w.buff_data(kVaryStaticStatLevel, 5000, kPatIceLevel);
        w.put_value(ice_decoy, 0xA0, 99);
        // 첫 칸이 미끼다 - 첨자로 찍으면 99 가 나온다.
        w.set_buff(buff, kBuffIce, {{3, ice_decoy}, {-2, ice}});

        const std::size_t link = w.buff_data(kChangeBuffLevel, 60000,
                                             cdtb::game::kNoPatternRow);
        w.put_link(link, kBuffIce, -2);
        w.set_skill(skill, 2, {{link}});

        // --- 배율 모르는 클래스 ---
        const std::size_t unknown = w.buff_data(kUnknownClass, 0, kPatHpMax);
        w.put_value(unknown, 0x98, 1000);
        w.set_skill(skill, 3, {{unknown}});

        // --- 너무 깊은 사슬: 바깥 + 세 칸 ---
        const std::size_t deep_end =
            w.buff_data(kVaryStatMaxValue, 0, kPatHpMax);
        w.put_value(deep_end, 0x98, 1000);
        w.set_buff(buff, kBuffChain3, {{1, deep_end}});
        const std::size_t deep3 =
            w.buff_data(kChangeBuffLevel, 0, cdtb::game::kNoPatternRow);
        w.put_link(deep3, kBuffChain3, 1);
        w.set_buff(buff, kBuffChain2, {{1, deep3}});
        const std::size_t deep2 =
            w.buff_data(kChangeBuffLevel, 0, cdtb::game::kNoPatternRow);
        w.put_link(deep2, kBuffChain2, 1);
        w.set_buff(buff, kBuffChain1, {{1, deep2}});
        const std::size_t deep1 =
            w.buff_data(kChangeBuffLevel, 0, cdtb::game::kNoPatternRow);
        w.put_link(deep1, kBuffChain1, 1);
        w.set_skill(skill, 4, {{deep1}});

        // --- 두 칸짜리 사슬(허용 한계) ---
        const std::size_t hop_end =
            w.buff_data(kVaryStaticStatLevel, 0, kPatIceLevel);
        w.put_value(hop_end, 0xA0, 4);
        w.set_buff(buff, kBuffHop2, {{1, hop_end}});
        const std::size_t hop_mid =
            w.buff_data(kChangeBuffLevel, 0, cdtb::game::kNoPatternRow);
        w.put_link(hop_mid, kBuffHop2, 1);
        w.set_buff(buff, kBuffHop1, {{1, hop_mid}});
        const std::size_t hop_out =
            w.buff_data(kChangeBuffLevel, 30000, cdtb::game::kNoPatternRow);
        w.put_link(hop_out, kBuffHop1, 1);
        w.set_skill(skill, 5, {{hop_out}});

        // --- 안 풀리는 형식 문자열 ---
        const std::size_t bad = w.buff_data(kVaryStatMaxValue, 0, kPatNoString);
        w.put_value(bad, 0x98, 1000);
        w.set_skill(skill, 6, {{bad}});

        // --- 어비스: 관통 I·II·III (레벨 1·2·3) ---
        const std::size_t crit1 = w.buff_data(kSocketCritical, 0, kPatSocketCrit);
        w.put_value(crit1, 0x98, 25000);   // ÷10⁴ = 2.5 -> **2**
        const std::size_t crit2 = w.buff_data(kSocketCritical, 0, kPatSocketCrit);
        w.put_value(crit2, 0x98, 50000);   // 5
        const std::size_t crit3 = w.buff_data(kSocketCritical, 0, kPatSocketCrit);
        w.put_value(crit3, 0x98, 75000);   // 7.5 -> **7**
        w.set_buff(buff, kBuffSocketCrit,
                   {{1, crit1}, {2, crit2}, {3, crit3}});

        // --- 사용 레코드 ---
        // 같은 스킬이 문맥만 달리해 세 번 등록된다(0x00/0x07/0x0C).
        w.add_use(use, 0, cdtb::game::kUseKindSkill, 0x00, {{1, 1}});
        w.add_use(use, 1, cdtb::game::kUseKindSkill, 0x07, {{1, 1}});
        w.add_use(use, 2, cdtb::game::kUseKindSkill, 0x0C, {{1, 1}});
        w.add_use(use, 3, cdtb::game::kUseKindSkill, 0x00, {{2, 1}});
        w.add_use(use, 4, cdtb::game::kUseKindSkill, 0x00, {{3, 1}});
        w.add_use(use, 5, cdtb::game::kUseKindSkill, 0x00, {{4, 1}});
        w.add_use(use, 6, cdtb::game::kUseKindSkill, 0x00, {{5, 1}});
        w.add_use(use, 7, cdtb::game::kUseKindSkill, 0x00, {{6, 1}});
        // 스킬이 아닌 사용 - 문맥은 툴팁이지만 효과로 안 친다.
        w.add_use(use, 8, 0x08, 0x00, {{1, 1}});   // FeedToTarget
        w.add_use(use, 9, 0x10, 0x00, {{1, 1}});   // RegisterReserveSlot

        // --- 아이템 레코드 ---
        w.set_uses(w.make_record(item, kRowConsumable, 0x300), {0, 1, 2});
        w.set_uses(w.make_record(item, kRowLinked, 0x300), {3});
        w.set_uses(w.make_record(item, kRowUnknown, 0x300), {4});
        w.make_record(item, kRowNone, 0x300);   // 입구가 둘 다 비었다
        w.set_uses(w.make_record(item, kRowDeep, 0x300), {5});
        w.set_uses(w.make_record(item, kRowTwoHop, 0x300), {6});
        w.set_uses(w.make_record(item, kRowNonSkill, 0x300), {8, 9});
        w.set_uses(w.make_record(item, kRowBadPattern, 0x300), {7});
        w.set_uses(w.make_record(item, kRowShared, 0x300), {0});
        // 어비스는 EnchantData 둘 - 0x70 스트라이드를 못박는다.
        w.set_enchants(w.make_record(item, kRowAbyss, 0x300),
                       {{{kBuffSocketCrit, 1},
                         {kBuffSocketCrit, 2},
                         {kBuffSocketCrit, 3}},
                        {{kBuffIce, -2}}});

        build_stat_names();
        w.finish_loc();

        mgr.item = w.addr(item.mgr);
        mgr.item_use = w.addr(use.mgr);
        mgr.skill = w.addr(skill.mgr);
        mgr.buff = w.addr(buff.mgr);
        mgr.pattern = w.addr(pattern.mgr);

        names.build_from_managers(w.mem, w.loc(), w.addr(sub_mgr_),
                                  w.addr(status_mgr_), w.addr(know_mgr_));
    }

    bool build(std::vector<ItemEffects>* out) const {
        return cdtb::game::build_item_effects_from_managers(w.mem, w.loc(),
                                                            names, mgr, out);
    }

    // `{Staticinfo:SubLevel:Hp}` -> `생명` 만 세운다(사슬 전체는 stat_names_tests).
    void build_stat_names() {
        World::Tbl sub = w.make_table(1, 0x30);
        World::Tbl status = w.make_table(1, 0x30);
        World::Tbl know = w.make_table(2, cdtb::game::kEffectMgrCount);
        sub_mgr_ = sub.mgr;
        status_mgr_ = status.mgr;
        know_mgr_ = know.mgr;

        const std::size_t sub_rec = w.make_record(sub, 0, 0x80);
        w.mem.put_u64(sub_rec + cdtb::game::kStatRecStringKey,
                      w.addr(w.engine_string("Hp", 0)));
        w.mem.put_u16(sub_rec + cdtb::game::kSubLevelKnowRow, 0);
        const std::size_t status_rec = w.make_record(status, 0, 0x80);
        w.mem.put_u64(status_rec + cdtb::game::kStatRecStringKey,
                      w.addr(w.engine_string("IceResistance", 0)));
        w.mem.put_u16(status_rec + cdtb::game::kStatusKnowRow, 1);

        const std::size_t k0 = w.make_record(know, 0, 0x40);
        w.mem.put_u64(k0 + 0x08, w.addr(w.engine_string("Knowledge_Hp",
                                                        0x11111111u)));
        const std::size_t k1 = w.make_record(know, 1, 0x40);
        w.mem.put_u64(k1 + 0x08,
                      w.addr(w.engine_string("Knowledge_IceResistance",
                                             0x22222222u)));
        w.add_loc(9, cdtb::game::loc_key(0x11111111u,
                                         cdtb::game::kKnowledgeNameField),
                  "생명");
        w.add_loc(9, cdtb::game::loc_key(0x22222222u,
                                         cdtb::game::kKnowledgeNameField),
                  "냉기 저항");
    }

private:
    std::size_t sub_mgr_ = 0;
    std::size_t status_mgr_ = 0;
    std::size_t know_mgr_ = 0;
};

}  // namespace

// ------------------------------------------------------------------ 소모품

TEST(item_effects_takes_only_the_tooltip_context) {
    // 같은 스킬이 문맥 0x00/0x07/0x0C 로 세 번 등록돼 있다. 줄은 두 개뿐이라야
    // 한다 - 안 거르면 여섯 줄이 된다.
    Fixture f;
    std::vector<ItemEffects> all;
    CHECK(f.build(&all));
    CHECK_EQ(all.size(), static_cast<std::size_t>(Fixture::kItemCount));
    const ItemEffects& e = all[Fixture::kRowConsumable];
    CHECK_EQ(e.lines.size(), static_cast<std::size_t>(2));
    CHECK_EQ(e.unresolved, 0);
}

TEST(item_effects_reproduces_the_measured_consumable_lines) {
    // 게임 툴팁과 글자까지 맞춘 골든 두 줄(§4.7-H'). 둘째 줄은 한 줄에
    // 파라미터 종류가 둘이고(8 · 5), 값이 **음수**이며 절대값으로 보인다.
    Fixture f;
    std::vector<ItemEffects> all;
    CHECK(f.build(&all));
    const ItemEffects& e = all[Fixture::kRowConsumable];
    CHECK_EQ(e.lines.size(), static_cast<std::size_t>(2));
    if (e.lines.size() < 2) return;
    CHECK_EQ(e.lines[0].text, std::string("생명 최대치 45 증가(1분)"));
    CHECK_EQ(e.lines[0].duration_ms, 90000u);
    CHECK_EQ(e.lines[1].text, std::string("생명 : 1 초마다 15 감소(20초)"));
    CHECK_EQ(e.lines[1].duration_ms, 20000u);
}

TEST(item_effects_skips_non_skill_uses) {
    // 먹이 주기·슬롯 등록은 효과가 아니다(§4.4). 문맥은 툴팁이지만 종류로 걸린다.
    Fixture f;
    std::vector<ItemEffects> all;
    CHECK(f.build(&all));
    const ItemEffects& e = all[Fixture::kRowNonSkill];
    CHECK(e.lines.empty());
    CHECK_EQ(e.unresolved, 0);
}

TEST(item_effects_leaves_items_without_effects_empty) {
    Fixture f;
    std::vector<ItemEffects> all;
    CHECK(f.build(&all));
    CHECK(all[Fixture::kRowNone].empty());
    CHECK(all[Fixture::kRowNone].lines.empty());
    CHECK_EQ(all[Fixture::kRowNone].unresolved, 0);
}

TEST(item_effects_reuses_the_same_skill_for_another_item) {
    // 같은 (스킬행, 레벨)을 쓰는 아이템은 캐시에서 같은 줄을 받는다.
    Fixture f;
    std::vector<ItemEffects> all;
    CHECK(f.build(&all));
    CHECK_EQ(all[Fixture::kRowShared].lines.size(),
             all[Fixture::kRowConsumable].lines.size());
    if (all[Fixture::kRowShared].lines.empty()) return;
    CHECK_EQ(all[Fixture::kRowShared].lines[0].text,
             all[Fixture::kRowConsumable].lines[0].text);
}

// -------------------------------------------------------------- 링크 · 사슬

TEST(item_effects_follows_change_buff_level_one_hop) {
    // 패턴이 없는 ChangeBuffLevel 은 링크다. `+0x90` 하위 u16 · `+0x94` 레벨로
    // 한 칸 더 간다.
    Fixture f;
    std::vector<ItemEffects> all;
    CHECK(f.build(&all));
    const ItemEffects& e = all[Fixture::kRowLinked];
    CHECK_EQ(e.lines.size(), static_cast<std::size_t>(1));
    CHECK_EQ(e.unresolved, 0);
    if (e.lines.empty()) return;
    CHECK_EQ(e.lines[0].text, std::string("냉기 저항 Lv6(1분)"));
}

TEST(item_effects_finds_negative_buff_levels_by_value) {
    // 링크가 가리키는 레벨은 **-2** 이고, 목록 첫 칸에는 레벨 3 짜리 미끼가
    // 있다. 첨자로 찍으면 Lv99 가 나온다.
    Fixture f;
    std::vector<ItemEffects> all;
    CHECK(f.build(&all));
    const ItemEffects& e = all[Fixture::kRowLinked];
    if (e.lines.empty()) {
        CHECK(!e.lines.empty());
        return;
    }
    CHECK(e.lines[0].text.find("99") == std::string::npos);
    CHECK(e.lines[0].text.find("Lv6") != std::string::npos);
}

TEST(item_effects_uses_the_outer_duration) {
    // 지속시간은 **바깥쪽** BuffData 의 것이다. 링크 칸은 60000(1분)이고
    // 사슬 끝은 5000(5초)이다 - 같은 끝 칸을 어비스 쪽에서 바깥으로 쓰면
    // 그때는 5초로 나온다. 두 줄을 나란히 본다.
    Fixture f;
    std::vector<ItemEffects> all;
    CHECK(f.build(&all));
    const ItemEffects& linked = all[Fixture::kRowLinked];
    CHECK_EQ(linked.lines.size(), static_cast<std::size_t>(1));
    if (linked.lines.empty()) return;
    CHECK_EQ(linked.lines[0].duration_ms, 60000u);
    CHECK_EQ(linked.lines[0].text, std::string("냉기 저항 Lv6(1분)"));

    const ItemEffects& abyss = all[Fixture::kRowAbyss];
    CHECK_EQ(abyss.lines.size(), static_cast<std::size_t>(4));
    if (abyss.lines.size() < 4) return;
    CHECK_EQ(abyss.lines[3].duration_ms, 5000u);
    CHECK_EQ(abyss.lines[3].text, std::string("냉기 저항 Lv6(5초)"));
}

TEST(item_effects_allows_two_hops) {
    // 실측 최대 2단(바깥 + 한 칸)이지만 한 칸 더까지는 따라간다.
    Fixture f;
    std::vector<ItemEffects> all;
    CHECK(f.build(&all));
    const ItemEffects& e = all[Fixture::kRowTwoHop];
    CHECK_EQ(e.lines.size(), static_cast<std::size_t>(1));
    CHECK_EQ(e.unresolved, 0);
    if (e.lines.empty()) return;
    CHECK_EQ(e.lines[0].text, std::string("냉기 저항 Lv4(30초)"));
}

TEST(item_effects_gives_up_on_a_chain_that_is_too_deep) {
    // 바깥 + 세 칸이면 포기한다. 줄은 안 내고 해석 못 한 것으로 센다 -
    // 순환하는 자료를 만나도 여기서 멈춘다.
    Fixture f;
    std::vector<ItemEffects> all;
    CHECK(f.build(&all));
    const ItemEffects& e = all[Fixture::kRowDeep];
    CHECK(e.lines.empty());
    CHECK_EQ(e.unresolved, 1);
}

// ------------------------------------------------------------------ 어비스

TEST(item_effects_reads_enchant_equip_buffs) {
    // `_enchantDataList`(0x70 인라인) -> `_equipBuffs`(0x20) -> BuffInfo 레벨.
    // 값은 ÷10⁴ 뒤 **자른다**: 25000 -> 2 · 50000 -> 5 · 75000 -> 7
    // (반올림이면 3 · 8 이 됐을 것이다, §4.7-H 3번).
    Fixture f;
    std::vector<ItemEffects> all;
    CHECK(f.build(&all));
    const ItemEffects& e = all[Fixture::kRowAbyss];
    CHECK_EQ(e.lines.size(), static_cast<std::size_t>(4));
    CHECK_EQ(e.unresolved, 0);
    if (e.lines.size() < 3) return;
    CHECK_EQ(e.lines[0].text,
             std::string("천 갑옷 타격 시 치명타 확률 2% 증가"));
    CHECK_EQ(e.lines[1].text,
             std::string("천 갑옷 타격 시 치명타 확률 5% 증가"));
    CHECK_EQ(e.lines[2].text,
             std::string("천 갑옷 타격 시 치명타 확률 7% 증가"));
    CHECK_EQ(e.lines[0].duration_ms, 0u);   // 즉발이면 접미가 없다
}

TEST(item_effects_survives_a_garbage_handle_upper_half) {
    // `_equipBuffs` 항목의 `+0x00` 과 링크의 `+0x90` 은 상위 절반이 잔여물
    // (0xFBCD)이다. 마스크를 안 하면 행을 못 찾는다.
    Fixture f;
    std::vector<ItemEffects> all;
    CHECK(f.build(&all));
    CHECK(!all[Fixture::kRowAbyss].lines.empty());
    CHECK(!all[Fixture::kRowLinked].lines.empty());
    CHECK_EQ(all[Fixture::kRowAbyss].unresolved, 0);
}

// ------------------------------------------------------- 해석 못 한 것 세기

TEST(item_effects_counts_unknown_classes_as_unresolved) {
    // 배율 표에 없는 클래스는 그 줄을 **버리고** 센다. 틀린 숫자를 보이는
    // 것보다 낫다.
    Fixture f;
    std::vector<ItemEffects> all;
    CHECK(f.build(&all));
    const ItemEffects& e = all[Fixture::kRowUnknown];
    CHECK(e.lines.empty());
    CHECK_EQ(e.unresolved, 1);
    CHECK(!e.empty());   // "효과 없음" 과 "해석 못 함" 은 다르다
}

TEST(item_effects_counts_unresolved_format_strings) {
    // 패턴 행은 있는데 현지화에 형식 문자열이 없다(아직 안 찼거나 빠진 행).
    Fixture f;
    std::vector<ItemEffects> all;
    CHECK(f.build(&all));
    const ItemEffects& e = all[Fixture::kRowBadPattern];
    CHECK(e.lines.empty());
    CHECK_EQ(e.unresolved, 1);
}

// ------------------------------------------------------------------ 실패 경로

TEST(item_effects_fails_when_a_manager_is_missing) {
    Fixture f;
    std::vector<ItemEffects> all;
    EffectManagers m = f.mgr;
    m.pattern = 0;
    CHECK(!cdtb::game::build_item_effects_from_managers(f.w.mem, f.w.loc(),
                                                        f.names, m, &all));
    m = f.mgr;
    m.buff = 0;
    CHECK(!cdtb::game::build_item_effects_from_managers(f.w.mem, f.w.loc(),
                                                        f.names, m, &all));
}

TEST(item_effects_fails_without_localization) {
    // 현지화가 아직이면 형식 문자열이 하나도 안 풀린다 - 조용히 물러나
    // 부르는 쪽이 재시도하게 한다(staged-data-load-retry).
    Fixture f;
    std::vector<ItemEffects> all;
    CHECK(!cdtb::game::build_item_effects_from_managers(f.w.mem, LocSystem{},
                                                        f.names, f.mgr, &all));
}

TEST(item_effects_fails_when_a_table_is_not_up_yet) {
    // 매니저는 있는데 개수가 0 이다(아직 안 찼다).
    Fixture f;
    std::vector<ItemEffects> all;
    const EffectManagers m = f.mgr;
    // 패턴 매니저의 개수 칸을 0 으로 만든다.
    const std::size_t off =
        static_cast<std::size_t>(m.pattern - f.w.mem.heap_addr(0));
    f.w.mem.put_u32(off + cdtb::game::kEffectMgrCount, 0);
    CHECK(!cdtb::game::build_item_effects_from_managers(f.w.mem, f.w.loc(),
                                                        f.names, m, &all));
}

TEST(item_effects_finds_nothing_without_rtti) {
    // RTTI 로 찾는 쪽. 가짜 모듈에는 매니저 클래스가 없다.
    Fixture f;
    f.w.mem.image.assign(0x1000, 0);
    cdtb::mem::Rtti rt(f.w.mem);
    std::vector<ItemEffects> all;
    CHECK(!cdtb::game::build_item_effects(rt, f.w.mem, f.w.loc(), f.names,
                                          &all));
    CHECK(all.empty());
}

#include "game/localization.h"

#include <cstring>
#include <optional>

#include "mem/scanner.h"

namespace cdtb::game {
namespace {

// --- 시스템 객체 (전역이 가리키는 것) ---
constexpr std::size_t kCatTablePtr = 0x48;   // void*  카테고리 표
constexpr std::size_t kPoolPtr = 0x58;       // char*  문자열 풀 기준
constexpr std::size_t kPoolSizeField = 0x60; // u32    풀 크기

// --- 카테고리 표 항목 (16바이트) ---
constexpr std::size_t kCatStride = 16;
constexpr std::size_t kCatArrayPtr = 0x00;   // void** 포인터 배열
constexpr std::size_t kCatCountField = 0x08; // u32    개수

// --- 항목 (LocalizationStringBase) ---
constexpr std::size_t kEntryKey = 0x10;      // u64    키
constexpr std::size_t kEntryOffset = 0x18;   // u32    풀 오프셋

constexpr std::uint32_t kUnresolved = 0xFFFFFFFFu;

// LocalizationStringBase::str() 본문 28바이트. ?? 는 disp32 두 개다.
//
//   8B 41 18              mov  eax, [rcx+0x18]
//   48 8B 0D ?? ?? ?? ??  mov  rcx, [rip+disp]   <- disp 가 전역
//   3B 41 60              cmp  eax, [rcx+0x60]
//   72 08                 jb   +8
//   48 8D 05 ?? ?? ?? ??  lea  rax, [rip+disp]   (범위 밖 기본 문자열)
//   C3                    ret
//   48 03 41 58           add  rax, [rcx+0x58]
//   C3                    ret
constexpr const char* kStrBodyPattern =
    "8B 41 18 48 8B 0D ?? ?? ?? ?? 3B 41 60 72 08 "
    "48 8D 05 ?? ?? ?? ?? C3 48 03 41 58 C3";

// 패턴 안에서 전역을 가리키는 disp32 의 위치와, 그 명령의 끝.
constexpr std::size_t kGlobalDispAt = 6;
constexpr std::size_t kGlobalInsnEnd = 10;

}  // namespace

bool find_loc_global_rva(const std::vector<std::uint8_t>& image,
                         std::uint64_t* rva_out) {
    if (rva_out == nullptr || image.size() < kGlobalInsnEnd) return false;

    const auto pattern = mem::parse_pattern(kStrBodyPattern);
    if (!pattern) return false;

    // 두 개까지만 모은다. 하나를 넘으면 어차피 고를 수 없다.
    const mem::Range range{image.data(), image.size()};
    const auto hits = mem::find_all(range, *pattern, 2);
    if (hits.size() != 1) return false;

    const std::size_t off = static_cast<std::size_t>(hits[0] - image.data());
    std::int32_t disp = 0;
    std::memcpy(&disp, image.data() + off + kGlobalDispAt, sizeof(disp));

    const std::int64_t rva =
        static_cast<std::int64_t>(off + kGlobalInsnEnd) + disp;
    if (rva < 0 || static_cast<std::uint64_t>(rva) + 8 > image.size()) {
        return false;
    }
    *rva_out = static_cast<std::uint64_t>(rva);
    return true;
}

bool find_loc_system(const mem::Rtti& rtti, const mem::Reader& reader,
                     LocSystem* out) {
    if (out == nullptr || !rtti.loaded()) return false;

    std::uint64_t rva = 0;
    if (!find_loc_global_rva(rtti.image(), &rva)) return false;

    LocSystem s;
    s.global = reader.module_base() + static_cast<std::uintptr_t>(rva);

    // 전역은 실행 중에 채워지므로 캐시한 이미지가 아니라 실제
    // 메모리에서 읽는다. 비어 있으면 시스템이 아직 없는 것이다.
    std::uint64_t object = 0;
    if (!reader.read_value(s.global, &object) || object == 0) return false;
    s.object = static_cast<std::uintptr_t>(object);

    std::uint64_t pool = 0;
    std::uint32_t pool_size = 0;
    if (!reader.read_value(s.object + kPoolPtr, &pool)) return false;
    if (!reader.read_value(s.object + kPoolSizeField, &pool_size)) return false;
    if (pool == 0 || pool_size == 0) return false;

    s.pool = static_cast<std::uintptr_t>(pool);
    s.pool_size = pool_size;
    *out = s;
    return true;
}

bool loc_categories(const mem::Reader& reader, const LocSystem& sys,
                    std::vector<LocCategory>* out) {
    if (out == nullptr) return false;

    std::uint64_t table = 0;
    if (!reader.read_value(sys.object + kCatTablePtr, &table) || table == 0) {
        return false;
    }

    std::vector<LocCategory> cats(kLocCategoryCount);
    for (int c = 0; c < kLocCategoryCount; ++c) {
        const std::uintptr_t slot =
            static_cast<std::uintptr_t>(table) + c * kCatStride;
        std::uint64_t array = 0;
        std::uint32_t count = 0;
        if (!reader.read_value(slot + kCatArrayPtr, &array)) continue;
        if (!reader.read_value(slot + kCatCountField, &count)) continue;
        cats[c].array = static_cast<std::uintptr_t>(array);
        cats[c].count = count;
    }
    *out = std::move(cats);
    return true;
}

namespace {

// 정렬된 포인터 배열에서 키를 이분 탐색한다. 게임이 0x1410d7230 에서
// 하는 것과 같다. 인덱스는 부호 있는 값으로 둔다 - mid 가 0 일 때
// hi = mid - 1 이 부호 없는 값이면 되돌아 감긴다.
bool search_category(const mem::Reader& reader, std::uintptr_t array,
                     std::uint32_t count, std::uint64_t key,
                     std::uintptr_t* entry_out) {
    std::int64_t lo = 0;
    std::int64_t hi = static_cast<std::int64_t>(count) - 1;
    while (lo <= hi) {
        const std::int64_t mid = lo + (hi - lo) / 2;
        std::uint64_t entry = 0;
        if (!reader.read_value(
                array + static_cast<std::uintptr_t>(mid) * 8, &entry) ||
            entry == 0) {
            return false;
        }
        std::uint64_t found = 0;
        if (!reader.read_value(static_cast<std::uintptr_t>(entry) + kEntryKey,
                               &found)) {
            return false;
        }
        if (found == key) {
            *entry_out = static_cast<std::uintptr_t>(entry);
            return true;
        }
        if (found < key) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return false;
}

bool read_pool_text(const mem::Reader& reader, const LocSystem& sys,
                    std::uint32_t offset, std::size_t max_len,
                    std::string* out) {
    if (offset == kUnresolved || offset >= sys.pool_size) return false;

    std::size_t n = sys.pool_size - offset;
    if (n > max_len) n = max_len;

    std::string buf(n, '\0');
    if (!reader.read(sys.pool + offset, buf.data(), n)) return false;

    const std::size_t end = buf.find('\0');
    if (end == std::string::npos) return false;   // 널 종단이 없으면 믿지 않는다
    buf.resize(end);
    *out = std::move(buf);
    return true;
}

}  // namespace

bool resolve(const mem::Reader& reader, const LocSystem& sys,
             std::uint64_t key, std::string* text_out, int* category_out,
             std::size_t max_len) {
    if (text_out == nullptr || !sys.valid()) return false;

    std::uint64_t cat_table = 0;
    if (!reader.read_value(sys.object + kCatTablePtr, &cat_table) ||
        cat_table == 0) {
        return false;
    }

    for (int cat = 0; cat < kLocCategoryCount; ++cat) {
        const std::uintptr_t slot =
            static_cast<std::uintptr_t>(cat_table) + cat * kCatStride;

        std::uint64_t array = 0;
        std::uint32_t count = 0;
        if (!reader.read_value(slot + kCatArrayPtr, &array)) continue;
        if (!reader.read_value(slot + kCatCountField, &count)) continue;
        // 개수가 0 이면 배열은 널일 수 있다. 만지지 않는다.
        if (count == 0 || array == 0) continue;

        std::uintptr_t entry = 0;
        if (!search_category(reader, static_cast<std::uintptr_t>(array), count,
                             key, &entry)) {
            continue;
        }

        // 키는 표 전체에서 고유하다. 찾았으면 여기서 끝난다 - 오프셋이
        // 아직 안 풀렸어도 다른 카테고리를 더 뒤지지 않는다.
        std::uint32_t offset = 0;
        if (!reader.read_value(entry + kEntryOffset, &offset)) return false;
        if (!read_pool_text(reader, sys, offset, max_len, text_out)) return false;
        if (category_out != nullptr) *category_out = cat;
        return true;
    }
    return false;
}

}  // namespace cdtb::game

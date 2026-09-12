#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "mem/reader.h"

namespace cdtb::mem {

// MSVC x64 RTTI를 따라 클래스 이름에서 인스턴스까지 내려간다.
//
// 값 스캔과 달리 사람이 게임 상태를 바꿔 줄 필요가 없다. 메모리
// 구조만 따라가는 결정론적 탐색이므로 모드가 실행될 때마다
// 스스로 객체를 찾을 수 있다. 런타임 주소는 실행마다 바뀌므로
// 오프셋을 코드에 박는 대신 이 방법을 쓴다.
//
// 구조 (x64):
//   vtable[-1]           -> RTTICompleteObjectLocator*
//   COL +0   signature (0|1)
//       +12  pTypeDescriptor (모듈 베이스 기준 RVA)
//   TypeDescriptor { void* vftable; void* spare; char name[]; }
//   name 은 ".?AVFreeCamCamera@pa@@" 형태
class Rtti {
public:
    explicit Rtti(const Reader& reader) : r_(reader) {}

    // 모듈 이미지를 읽어 캐시한다. 나머지 조회는 전부 캐시에서 한다.
    //
    // 접근 불가 페이지가 섞여 있으면 그 자리는 0 으로 남는다. 얼마나
    // 남았는지는 stats 로 받는다 - 구멍이 크면 스캔 결과를 그대로
    // 믿으면 안 된다.
    struct ImageLoad {
        std::size_t chunks = 0;         // 시도한 청크 수
        std::size_t failed_chunks = 0;  // 읽지 못해 0 으로 남은 청크
        std::size_t failed_bytes = 0;
    };
    bool load_image(ImageLoad* stats = nullptr);
    bool loaded() const { return !image_.empty(); }

    // 캐시한 이미지를 넘겨준다. 부른 뒤에는 loaded() 가 거짓이다.
    // 이미지를 고쳐 파일로 낼 때 364MB 복사를 피하려고 둔다.
    std::vector<std::uint8_t> take_image() {
        std::vector<std::uint8_t> out = std::move(image_);
        image_.clear();
        return out;
    }
    const std::vector<std::uint8_t>& image() const { return image_; }

    struct TypeInfo {
        std::string name;
        std::uintptr_t descriptor = 0;
    };
    std::vector<TypeInfo> find_types(const std::string& substring,
                                     std::size_t max) const;

    std::vector<std::uintptr_t> vtables_for(std::uintptr_t descriptor) const;

    std::string class_of_vtable(std::uintptr_t vtable) const;
    std::string class_of_object(std::uintptr_t object) const;

    // 이름이 정확히 일치하는 클래스의 살아있는 인스턴스를 찾는다.
    // 다중 상속이면 서브객체 주소도 함께 나오므로, 호출자가 가장
    // 작은 주소를 객체 시작으로 보면 된다.
    std::vector<std::uintptr_t> instances_of_class(const std::string& name,
                                                   std::size_t max) const;

    // 힙을 한 번만 훑어 이름이 맞는 객체를 전부 찾는다. 클래스마다
    // 따로 스캔하면 힙 전체를 매번 읽어야 하므로 모아서 온다.
    struct Found {
        std::uintptr_t address = 0;
        std::string cls;
    };
    std::vector<Found> find_objects(const std::string& substring,
                                    std::size_t max) const;

    // 이름이 정확히 일치하는 **여러** 클래스의 인스턴스를 힙 한 번 훑기로 모두 찾는다.
    // instances_of_class 를 클래스마다 부르면 힙 전체를 그 수만큼 읽는다 - 카메라 탐색이
    // 네 클래스로 월드 안에서 122초였다(2026-09-12). Found::cls 로 가른다.
    std::vector<Found> find_objects_of(const std::vector<std::string>& names,
                                       std::size_t max) const;

    // 모듈 이미지에서 8바이트 값이 저장된 위치. 전역 포인터 탐색용.
    std::vector<std::uintptr_t> find_qword(std::uint64_t value,
                                           std::size_t max) const;

    // 주어진 주소를 RIP 상대로 참조하는 명령을 찾는다.
    //   REX.W 8B /r disp32   mov r64, [rip+disp32]
    //   REX.W 8D /r disp32   lea r64, [rip+disp32]
    // ModRM 의 mod=00, rm=101 이면 RIP 상대이고,
    // 대상은 (명령 주소 + 7) + disp32 다.
    struct Xref {
        std::uintptr_t at = 0;
        std::uint8_t opcode = 0;   // 0x8B(mov) 또는 0x8D(lea)
        std::uint8_t reg = 0;
    };
    std::vector<Xref> find_xrefs(std::uintptr_t target, std::size_t max) const;

    // 힙에서 이 주소를 담고 있는 곳과, 추정한 소유 객체.
    struct Ref {
        std::uintptr_t slot = 0;
        std::uintptr_t owner = 0;
        std::size_t offset = 0;
        std::string owner_class;
    };
    std::vector<Ref> find_refs(std::uintptr_t target, std::size_t max) const;

    // 이미지 색인을 미리 만든다. 안 부르면 첫 조회에서 저절로 만들어지므로
    // 부를 의무는 없다 - 만드는 데 몇 초 걸리는 것을 로그로 보고 싶을 때,
    // 그리고 그 비용을 첫 조회가 아니라 여기로 옮기고 싶을 때 쓴다.
    // 만들었으면 true(이미 있었으면 false).
    bool build_index() const;

    struct IndexStats {
        std::size_t types = 0;     // `.?AV...@@` 타입 서술자 수
        std::size_t vtables = 0;   // 클래스가 붙은 vtable 수
        bool ready = false;
    };
    IndexStats index_stats() const;

private:
    std::vector<std::uintptr_t> instances_of_vtable(std::uintptr_t vtable,
                                                    std::size_t max) const;
    // 힙을 한 번 훑어 matching 의 vtable 값을 담은 자리를 모은다(find_objects 계열의 공통 몸통).
    std::vector<Found> scan_heap(
        const std::vector<std::pair<std::uintptr_t, const std::string*>>& matching,
        std::size_t max) const;

    // --- 이미지 색인 (한 번만 만든다) ---
    //
    // `image_` 는 한 번 읽고 안 바뀌므로 아래 둘은 **이미지의 순수
    // 함수**다. 예전에는 조회마다 다시 만들었다: `find_types` 가 350MB 를
    // **1바이트씩**(약 3.7억 회) 훑고, `instances_of_vtable` 도 이미지
    // 전체를 8바이트씩 훑어 `class_of_vtable` 을 불렀다. 탐색(아이템표·
    // 인벤토리·로스터·액터매니저…)마다 그 짓을 되풀이해서 시작이 분 단위로
    // 걸렸다 - 실측 2026-09-08: 한 번의 탐색 통과에 2분 30초.
    //
    // 색인은 이미지를 **한 번** 훑어 만들고 그 뒤로는 전부 여기서 찾는다.
    void ensure_index() const;

    mutable std::once_flag index_once_;
    mutable std::vector<TypeInfo> types_;                 // 이름 순 아님
    mutable std::vector<std::pair<std::uintptr_t, std::size_t>> vtable_cls_;
    // (vtable 주소, types_ 색인). 주소 오름차순이라 이분 탐색이 된다.

    const Reader& r_;
    std::vector<std::uint8_t> image_;
};

}  // namespace cdtb::mem

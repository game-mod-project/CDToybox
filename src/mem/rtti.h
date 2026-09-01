#pragma once

#include <cstdint>
#include <string>
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
    bool load_image();
    bool loaded() const { return !image_.empty(); }
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

private:
    std::vector<std::uintptr_t> instances_of_vtable(std::uintptr_t vtable,
                                                    std::size_t max) const;

    const Reader& r_;
    std::vector<std::uint8_t> image_;
};

}  // namespace cdtb::mem

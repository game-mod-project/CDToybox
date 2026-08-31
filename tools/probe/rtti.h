#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "remote.h"

namespace cdtb::probe {

// MSVC x64 RTTI를 따라 클래스 이름에서 인스턴스까지 내려간다.
//
// 값 스캔처럼 사람이 게임 상태를 바꿔 주어야 하는 절차가 없다.
// 전부 메모리 구조를 따라가는 결정론적 탐색이다.
//
// 구조 (x64):
//   vtable[-1]                 -> RTTICompleteObjectLocator*
//   COL.pTypeDescriptor        -> TypeDescriptor 의 RVA (절대주소 아님)
//   TypeDescriptor { void* vftable; void* spare; char name[]; }
//   name 은 ".?AVCameraComponent@pa@@" 형태
class Rtti {
public:
    explicit Rtti(const Remote& remote) : r_(remote) {}

    // 모듈 이미지를 통째로 읽어 캐시한다. 이후 탐색은 전부 여기서 한다.
    bool load_image();
    const std::vector<std::uint8_t>& image() const { return image_; }

    // 데코레이트된 이름(".?AVFoo@pa@@")에 substring이 들어가는 클래스를
    // 전부 찾는다. 반환은 (이름, TypeDescriptor 절대주소).
    struct TypeInfo {
        std::string name;
        std::uintptr_t descriptor = 0;
    };
    std::vector<TypeInfo> find_types(const std::string& substring,
                                     std::size_t max) const;

    // TypeDescriptor를 가리키는 COL을 찾고, 그 COL을 가리키는
    // vtable[-1]을 찾아 vtable 주소를 돌려준다. 클래스 하나에 vtable이
    // 여러 개일 수 있어(다중 상속) 목록으로 준다.
    std::vector<std::uintptr_t> vtables_for(std::uintptr_t descriptor) const;

    // 힙에서 이 vtable을 첫 필드로 갖는 객체를 찾는다.
    std::vector<std::uintptr_t> instances_of(std::uintptr_t vtable,
                                             std::size_t max) const;

    // vtable 주소에서 클래스 이름을 되짚는다.
    // vtable[-1] -> COL -> pTypeDescriptor(RVA) -> name
    std::string class_of_vtable(std::uintptr_t vtable) const;

    // 객체 주소를 주면 그 vtable을 읽어 클래스 이름을 돌려준다.
    std::string class_of_object(std::uintptr_t object) const;

    // 힙을 한 번 훑어 클래스 이름에 substring이 들어가는 객체를 전부
    // 찾는다. 클래스마다 따로 스캔하면 힙 10GB를 매번 읽어야 하므로,
    // 한 번에 모아 온다.
    struct Found {
        std::uintptr_t address = 0;
        std::string cls;
    };
    std::vector<Found> find_objects(const std::string& substring,
                                    std::size_t max) const;

private:
    const Remote& r_;
    std::vector<std::uint8_t> image_;
};

}  // namespace cdtb::probe

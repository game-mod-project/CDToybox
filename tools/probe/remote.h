#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cdtb::probe {

// 게임 프로세스에 외부에서 붙어 메모리를 읽는다.
//
// 인게임 패널이 아니라 외부 도구로 만드는 이유는, 이 작업의 조작자가
// 사람이 아니라 에이전트이기 때문이다. 명령줄에서 반복 실행할 수
// 있어야 값을 찾는 일을 자율적으로 할 수 있다.
class Remote {
public:
    ~Remote();

    // 이름으로 프로세스를 찾아 연다. 실패 이유는 last_error()에 남는다.
    bool attach(const wchar_t* exe_name);
    bool attached() const { return handle_ != nullptr; }
    const std::string& last_error() const { return err_; }

    unsigned long pid() const { return pid_; }
    std::uintptr_t module_base() const { return base_; }
    std::size_t module_size() const { return size_; }

    // 원격 메모리를 읽는다. 부분 읽기는 실패로 친다.
    bool read(std::uintptr_t addr, void* out, std::size_t n) const;

    // 원격 메모리에 쓴다. 실행 중인 게임을 건드리므로, 무엇을 쓰는지
    // 알고 있는 경우에만 부른다. 되돌릴 수 있는 값부터 시험한다.
    bool write(std::uintptr_t addr, const void* src, std::size_t n) const;

    template <class T>
    bool read_value(std::uintptr_t addr, T* out) const {
        return read(addr, out, sizeof(T));
    }

    // 커밋되고 읽기 가능한 영역을 열거한다.
    struct Region {
        std::uintptr_t base = 0;
        std::size_t size = 0;
        bool writable = false;
        bool executable = false;
        bool is_image = false;
    };
    std::vector<Region> regions() const;

private:
    void* handle_ = nullptr;
    unsigned long pid_ = 0;
    std::uintptr_t base_ = 0;
    std::size_t size_ = 0;
    std::string err_;
};

}  // namespace cdtb::probe

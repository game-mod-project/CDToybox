"""이 함수를 `call rel32`(E8) 로 부르는 자리를 전부 찾는다.

    python tools/rtti/callers.py <exe> <함수RVA> [<함수RVA> ...]

게임을 켜지 않고 파일만 본다.

`xref_data.py` 는 `lea`/`mov [rip]` 같은 **데이터 참조**만 센다. 함수를 누가
부르는지는 못 본다 - 그 빈자리를 채운다.

**0곳이 나오면 그것도 답이다.** 가상 호출(`call [rax+N]`)이나 표에 등록한 뒤
간접으로 부르는 함수는 여기 안 잡힌다. 2026-09-19 보스룸 추적에서 메시지
`TrocTrUseItemReserveSlotReq` 의 vtable 을 쓰는 67바이트 함수를 찾았는데, 그것을
부르는 자리가 0곳이었다 - 메시지 팩토리가 표에 등록돼 간접 호출되기 때문이다.
그래서 "메시지 클래스에서 보내는 쪽으로 거슬러 오른다" 는 길이 막혔다는 것을
확정할 수 있었다(`specs/2026-09-16-vehicle-place-and-dismount.md` §8-3).
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from find_class import Image   # noqa: E402


def callers_of(img, target):
    hits = []
    d = img.data
    for lo, hi in img.exec_ranges():
        i = lo
        while True:
            i = d.find(b'\xE8', i, hi - 5)
            if i < 0:
                break
            here = img.off_to_rva(i)
            if here is not None:
                disp = struct.unpack_from('<i', d, i + 1)[0]
                if here + 5 + disp == target:
                    hits.append(here)
            i += 1
    return hits


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    img = Image(sys.argv[1])
    for a in sys.argv[2:]:
        target = int(a, 0)
        hits = callers_of(img, target)
        print('=== 0x%X 를 부르는 곳 %d' % (target, len(hits)))
        for h in hits:
            print('   call @ RVA 0x%X' % h)
        if not hits:
            print('   (없음 - 가상 호출이거나 표에 등록해 간접으로 부른다)')
    return 0


if __name__ == '__main__':
    sys.exit(main())

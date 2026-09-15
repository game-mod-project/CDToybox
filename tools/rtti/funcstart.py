"""주소가 속한 함수의 시작·끝을 **정확히** 찾는다.

    python tools/rtti/funcstart.py <exe> <RVA> [<RVA> ...]

게임을 켜지 않고 파일만 본다.

왜 이 도구가 있는가
-------------------
훅을 걸 자리를 잡을 때 "뒤로 훑어 `CC CC CC CC` 패딩 다음" 을 함수 시작으로
보는 어림짐작을 썼다가 **셋 중 둘을 틀렸다**(2026-09-15):

    +0x2AC9E58 -> 짐작 0x2AC87C0 · 실제 0x2AC9C10   (0x1450 빗나감)
    0x2941350  -> 함수 시작이 아니라 0x29411E0 **안쪽**

함수 중간에 훅을 걸면 명령을 덮어써 게임을 팅기게 할 수 있다. 어림짐작을 쓸
이유가 없다 - x64 PE 는 예외 디렉터리(`.pdata`)에 **모든 함수의 시작·끝**을
RUNTIME_FUNCTION{begin, end, unwind} 12바이트씩 담고 있다(이 실행 파일은
242,183개). 이건 짐작이 아니라 링커가 적어 둔 사실이다.
"""
import bisect
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from find_class import Image   # noqa: E402


def function_table(img):
    """(시작, 끝) RVA 목록을 시작 순으로 돌려준다."""
    d = img.data
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    opt = pe + 24
    magic = struct.unpack_from('<H', d, opt)[0]
    dd = opt + (112 if magic == 0x20B else 96)
    exc_rva, exc_size = struct.unpack_from('<II', d, dd + 3 * 8)
    if exc_rva == 0 or exc_size == 0:
        return []
    off = img.rva_to_off(exc_rva)
    n = exc_size // 12
    out = []
    for i in range(n):
        begin, end, _unwind = struct.unpack_from('<III', d, off + i * 12)
        if end > begin:
            out.append((begin, end))
    out.sort()
    return out


def owner(table, keys, rva):
    """rva 를 품은 (시작, 끝). 없으면 None."""
    i = bisect.bisect_right(keys, rva) - 1
    if i < 0:
        return None
    begin, end = table[i]
    return (begin, end) if begin <= rva < end else None


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    img = Image(sys.argv[1])
    table = function_table(img)
    if not table:
        print('예외 디렉터리가 없습니다.')
        return 1
    keys = [b for b, _ in table]
    print('함수 %d개' % len(table))
    for arg in sys.argv[2:]:
        rva = int(arg, 0)
        r = owner(table, keys, rva)
        if r is None:
            print('  0x%-9X -> 어느 함수에도 안 들어갑니다' % rva)
            continue
        begin, end = r
        mark = '시작이 맞다' if begin == rva else '안쪽 (+0x%X)' % (rva - begin)
        print('  0x%-9X -> 함수 0x%X ~ 0x%X (크기 %d) · %s'
              % (rva, begin, end, end - begin, mark))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

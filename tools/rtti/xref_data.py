"""주어진 RVA 구간을 건드리는 코드를 찾는다 (RIP 상대 참조).

    python tools/rtti/xref_data.py <exe> <시작RVA> [길이=8]

`map_cheats.py` 로 치트 메시지마다 전역 프로토타입 객체가 있다는 것을
알아냈다. 그 전역을 읽는 쪽이 디스패처다 - 그걸 찾으려고 만들었다.
"""
import re
import struct
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from find_class import Image   # noqa: E402

# (정규식, disp32 위치, 명령 길이, 표기)
FORMS = [
    (re.compile(rb'[\x48\x4c]\x8b[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]'), 3, 7,
     'mov r64,[rip]'),
    (re.compile(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]'), 3, 7,
     'lea'),
    (re.compile(rb'[\x48\x4c]\x89[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]'), 3, 7,
     'mov [rip],r64'),
    (re.compile(rb'[\x48\x4c]\x3b[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]'), 3, 7,
     'cmp'),
    (re.compile(rb'\xff\x15'), 2, 6, 'call [rip]'),
    (re.compile(rb'\xff\x25'), 2, 6, 'jmp [rip]'),
    (re.compile(rb'\x8b[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]'), 2, 6,
     'mov r32,[rip]'),
    (re.compile(rb'\x89[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]'), 2, 6,
     'mov [rip],r32'),
]


def scan(img, lo_rva, hi_rva):
    hits = []
    for lo, hi in img.exec_ranges():
        chunk = img.data[lo:hi]
        for pat, dpos, ilen, tag in FORMS:
            for m in pat.finditer(chunk):
                off = lo + m.start()
                here = img.off_to_rva(off)
                if here is None:
                    continue
                disp = struct.unpack_from('<i', img.data, off + dpos)[0]
                tgt = here + ilen + disp
                if lo_rva <= tgt < hi_rva:
                    hits.append((here, tag, tgt, ilen))
    # REX 형(7바이트)에 걸린 명령은 접두 없는 형(6바이트)에도 한 칸 뒤에서
    # 걸린다 - 같은 대상이면 한 명령이다. 두 번 세지 않는다(2차 리뷰 2026-09-11:
    # 0x6C2E148 "2곳" 이 실은 1곳, 세션 전역 "21/6곳" 이 실은 11/3곳이었다).
    rex = {(h, t) for h, _, t, l in hits if l == 7}
    out = [(h, tag, t) for h, tag, t, l in hits
           if not (l == 6 and (h - 1, t) in rex)]
    out.sort()
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    img = Image(sys.argv[1])
    lo = int(sys.argv[2], 0)
    length = int(sys.argv[3], 0) if len(sys.argv) > 3 else 8
    hits = scan(img, lo, lo + length)
    print('RVA 0x%X ~ 0x%X 를 건드리는 코드 %d곳' % (lo, lo + length, len(hits)))
    for here, tag, tgt in hits:
        print('  RVA 0x%08X  %-14s -> 0x%08X (+%d)'
              % (here, tag, tgt, tgt - lo))
    return 0


if __name__ == '__main__':
    sys.exit(main())

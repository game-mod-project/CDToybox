"""치트 메시지 클래스들의 vtable 과 그것을 쓰는 코드를 한꺼번에 훑는다.

    python tools/rtti/map_cheats.py <exe> [이름조각=Cheat]

`find_class.py` 로 한 클래스씩 보다가, 두 클래스의 참조가 80바이트
간격으로 같은 함수 안에 있는 것을 보고 만들었다. 등록 함수를 통째로
보려면 전부를 한 번에 훑어야 한다.
"""
import re
import struct
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from find_class import Image, find_col, find_vtables, LEA   # noqa: E402


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    img = Image(sys.argv[1])
    needle = sys.argv[2] if len(sys.argv) > 2 else 'Cheat'

    pat = re.compile(rb'\.\?AV[A-Za-z0-9_:@]*' + re.escape(needle.encode()) +
                     rb'[A-Za-z0-9_:@]*@@\x00')
    vt_name = {}
    classes = 0
    for m in pat.finditer(img.data):
        td_rva = img.off_to_rva(m.start() - 0x10)
        if td_rva is None:
            continue
        name = m.group(0).rstrip(b'\0').decode()
        short = name[4:].split('@')[0]
        classes += 1
        for col in find_col(img, td_rva):
            for vt in find_vtables(img, col):
                vt_name[vt] = short
    print('클래스 %d개, vtable %d개' % (classes, len(vt_name)))

    hits = []
    for lo, hi in img.exec_ranges():
        chunk = img.data[lo:hi]
        for m in LEA.finditer(chunk):
            off = lo + m.start()
            here = img.off_to_rva(off)
            if here is None:
                continue
            disp = struct.unpack_from('<i', img.data, off + 3)[0]
            tgt = here + 7 + disp
            if tgt in vt_name:
                hits.append((here, vt_name[tgt], tgt))
    hits.sort()
    print('참조하는 코드 %d곳\n' % len(hits))

    prev = None
    for here, name, tgt in hits:
        gap = '' if prev is None else '  (+%d)' % (here - prev)
        print('  RVA 0x%08X  %-52s%s' % (here, name, gap))
        prev = here
    return 0


if __name__ == '__main__':
    sys.exit(main())

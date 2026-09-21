"""vtable 주소 -> 클래스 이름. `find_class.py` 의 **반대 방향**이다.

    python tools/rtti/vtname.py <exe> <vtableVA 또는 RVA> [...]

게임을 켜지 않고 파일만 본다.

왜 이 도구가 있는가
-------------------
메모리에서 객체를 만나면 첫 8바이트가 vtable 이다. 그 vtable 이 **어느 클래스**
인지 알면 층이 곧바로 갈린다. 2026-09-18 탈것 체력 추적에서 "우리가 쓰던 배열"
과 "진짜 값을 든 배열" 을 각각 거슬러 올라가 vtable 넷을 얻었는데, 이 도구로
이름을 풀자 한 번에 정리됐다:

    0x145585770 -> ClientChildOnlyInGameActor      (우리가 쓰던 쪽)
    0x145B1E0C0 -> ServerChildOnlyInGameActor      (진짜 값을 든 쪽)
    0x14558D828 -> ClientStatusActorComponent      (예전 주석의 "마커")
    0x145B20208 -> ServerStatusActorComponent

런타임 `probe whatis` 로도 되지만 그쪽은 RTTI 색인을 세우느라 몇 분이 걸린다.
이쪽은 파일에서 COL 한 칸만 따라가므로 즉시 끝난다.

구조
----
vtable 바로 앞 8바이트가 CompleteObjectLocator 의 VA 이고, COL +0x0C 가
TypeDescriptor 의 RVA, TD +0x10 부터가 망글된 이름이다.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from find_class import Image   # noqa: E402


def u32(img, rva):
    off = img.rva_to_off(rva)
    return struct.unpack_from('<I', img.data, off)[0] if off is not None else 0


def u64(img, rva):
    off = img.rva_to_off(rva)
    return struct.unpack_from('<Q', img.data, off)[0] if off is not None else 0


def name_of_vtable(img, vt_rva):
    """(이름, 실패 사유). 이름을 못 찾으면 이름이 None 이다."""
    col_va = u64(img, vt_rva - 8)
    if col_va == 0:
        return None, 'vtable-8 이 비었다'
    col = col_va - img.base
    sig = u32(img, col)
    td = u32(img, col + 0x0C)
    if sig not in (0, 1) or td == 0:
        return None, 'COL 이 아니다 (sig %d)' % sig
    off = img.rva_to_off(td + 0x10)
    if off is None:
        return None, 'TypeDescriptor 를 못 읽었다'
    end = img.data.find(b'\0', off, off + 256)
    return img.data[off:end].decode('ascii', 'replace'), None


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    img = Image(sys.argv[1])
    print('이미지 베이스 0x%X' % img.base)
    for a in sys.argv[2:]:
        v = int(a, 0)
        rva = v - img.base if v >= img.base else v
        nm, why = name_of_vtable(img, rva)
        print('  vtable 0x%X (RVA 0x%X)  ->  %s'
              % (img.base + rva, rva, nm if nm else '? ' + why))
    return 0


if __name__ == '__main__':
    sys.exit(main())

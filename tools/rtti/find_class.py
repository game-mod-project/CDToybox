"""실행 파일에서 클래스의 vtable 과 그것을 참조하는 코드를 찾는다.

    python tools/rtti/find_class.py <exe> <이름조각> [<이름조각> ...]

게임을 켜지 않고 파일만 보고 한다. RTTI 구조는 이미지에 그대로
들어 있으므로 프로세스에 붙지 않아도 된다.

찾는 순서
---------
1. `.?AV<이름>@...@@` 형태의 TypeDescriptor 이름 문자열
2. 그 TD 를 가리키는 CompleteObjectLocator (서명 1, pSelf 자기 RVA)
3. COL 주소를 담은 8바이트 - 그 바로 뒤가 vtable 이다
4. vtable 주소를 만드는 `lea reg, [rip+disp]` - 대개 생성자다
"""
import re
import struct
import sys


class Image:
    def __init__(self, path):
        self.data = open(path, 'rb').read()
        d = self.data
        pe = struct.unpack_from('<I', d, 0x3C)[0]
        assert d[pe:pe + 4] == b'PE\0\0', 'PE 헤더가 아닙니다'
        nsec = struct.unpack_from('<H', d, pe + 6)[0]
        opt = struct.unpack_from('<H', d, pe + 20)[0]
        self.base = struct.unpack_from('<Q', d, pe + 24 + 24)[0]
        self.sections = []
        o = pe + 24 + opt
        for _ in range(nsec):
            name = d[o:o + 8].rstrip(b'\0')
            vsize, va, rawsize, rawptr = struct.unpack_from('<IIII', d, o + 8)
            flags = struct.unpack_from('<I', d, o + 36)[0]
            self.sections.append((name, va, vsize, rawptr, rawsize, flags))
            o += 40

    def off_to_rva(self, off):
        for _n, va, _vs, rp, rs, _f in self.sections:
            if rp <= off < rp + rs:
                return va + (off - rp)
        return None

    def rva_to_off(self, rva):
        for _n, va, vs, rp, rs, _f in self.sections:
            if va <= rva < va + max(vs, rs):
                d = rva - va
                # 난독화 빌드는 VirtSize 가 RawSize 보다 크다. 잘라야 한다.
                return rp + d if d < rs else None
        return None

    def exec_ranges(self):
        out = []
        for _n, _va, _vs, rp, rs, f in self.sections:
            if f & 0x20000000 and rs:      # IMAGE_SCN_MEM_EXECUTE
                out.append((rp, rp + rs))
        return out


def find_type_descriptors(img, needle):
    pat = re.compile(rb'\.\?AV[A-Za-z0-9_:@]*' +
                     re.escape(needle.encode()) +
                     rb'[A-Za-z0-9_:@]*@@\x00')
    out = []
    for m in pat.finditer(img.data):
        name_off = m.start()
        # TypeDescriptor: void* pVFTable; void* spare; char name[]
        td_off = name_off - 0x10
        td_rva = img.off_to_rva(td_off)
        if td_rva is not None:
            out.append((m.group(0).rstrip(b'\0').decode(), td_rva, td_off))
    return out


def find_col(img, td_rva):
    """TD 를 가리키는 CompleteObjectLocator 의 RVA 들."""
    want = struct.pack('<I', td_rva)
    out, start = [], 0
    d = img.data
    while True:
        i = d.find(want, start)
        if i < 0:
            break
        start = i + 1
        col_off = i - 0xC
        if col_off < 0:
            continue
        sig, _off, _cd, tdr, _chd, self_rva = struct.unpack_from(
            '<IIIIII', d, col_off)
        if sig != 1 or tdr != td_rva:
            continue
        col_rva = img.off_to_rva(col_off)
        if col_rva is not None and self_rva == col_rva:
            out.append(col_rva)
    return out


def find_vtables(img, col_rva):
    """COL 주소를 담은 8바이트 뒤가 vtable 이다."""
    want = struct.pack('<Q', img.base + col_rva)
    out, start = [], 0
    while True:
        i = img.data.find(want, start)
        if i < 0:
            break
        start = i + 1
        rva = img.off_to_rva(i + 8)
        if rva is not None:
            out.append(rva)
    return out


LEA = re.compile(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]')


def find_lea_refs(img, target_rvas):
    """target 주소를 만들어 내는 lea reg,[rip+disp] 를 찾는다."""
    want = set(target_rvas)
    hits = []
    for lo, hi in img.exec_ranges():
        chunk = img.data[lo:hi]
        for m in LEA.finditer(chunk):
            off = lo + m.start()
            disp = struct.unpack_from('<i', img.data, off + 3)[0]
            here = img.off_to_rva(off)
            if here is None:
                continue
            if (here + 7 + disp) in want:
                hits.append((here, here + 7 + disp))
    return hits


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    img = Image(sys.argv[1])
    print('이미지 베이스 0x%X, 섹션 %d개' % (img.base, len(img.sections)))
    for needle in sys.argv[2:]:
        print('\n=== %s ===' % needle)
        tds = find_type_descriptors(img, needle)
        if not tds:
            print('  TypeDescriptor 를 못 찾았습니다')
            continue
        for name, td_rva, _off in tds:
            print('  %s' % name)
            print('    TypeDescriptor RVA 0x%X' % td_rva)
            cols = find_col(img, td_rva)
            if not cols:
                print('    COL 없음')
                continue
            vts = []
            for c in cols:
                print('    COL RVA 0x%X' % c)
                for v in find_vtables(img, c):
                    print('      vtable RVA 0x%X  (VA 0x%X)' % (v, img.base + v))
                    vts.append(v)
            if vts:
                refs = find_lea_refs(img, vts)
                print('    vtable 을 쓰는 코드 %d곳' % len(refs))
                for here, tgt in refs[:12]:
                    print('      lea @ RVA 0x%X -> vtable 0x%X' % (here, tgt))
    return 0


if __name__ == '__main__':
    sys.exit(main())

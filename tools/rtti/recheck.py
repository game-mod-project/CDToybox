"""게임 갱신 뒤 우리 상수가 아직 맞는지 한 번에 대조한다.

    py -3.14 tools/rtti/recheck.py <새 exe> [--src <레포 루트>]

**소스를 읽어 대조표를 스스로 만든다.** 손으로 적은 목록이 아니므로 상수를
더하거나 고쳐도 이 파일은 안 고쳐도 된다 - 갱신 때마다 목록이 낡아 빠뜨리던
것을 막으려고 그렇게 지었다(2026-09-18, 1.0.0.2944 대응에서).

무엇을 보는가
-------------
1. **파일** - FileVersion · 크기 · SizeOfImage · 실행 섹션(이름은 Denuvo 가
   무작위화하므로 `IMAGE_SCN_MEM_EXECUTE` 로만 고른다).
2. **AOB 패턴** - `constexpr const char* k*Pattern` 을 소스에서 뽑아 실행
   섹션에서 센다. 1곳이면 자가 치유, 0곳/여러 곳이면 손봐야 한다.
3. **메시지 ID** - 소스의 `k*Id` 위에 달린 `// @class <RTTI 이름>` 을 읽어
   새 exe 에서 ID 를 다시 뽑아 견준다. **이 대조가 이 도구의 핵심이다** -
   RVA 는 틀리면 프롤로그·opcode 검사가 걸러 주지만 메시지 ID 에는 그 그물이
   없어서, 1.0.0.2944 의 전면 재번호 때 조용히 다른 처리기가 돌 뻔했다.
4. **고정 RVA** - `k*Rva` 를 뽑아 그 자리에 지금 무엇이 있는지 디스어셈블해
   보여 준다. 판정은 사람이 한다(자동 판정은 거짓 안심을 만든다).

무엇을 안 보는가
----------------
데이터 전역(매니저 포인터 은행)은 정적으로 확정되지 않는다. 2850·2944 두 번
다 그랬다 - 런타임(`cdtb_probe`)으로만 갈린다. 4번에 "데이터" 로 표시만 한다.

관련: `docs/superpowers/specs/2026-09-01-patch-recheck.md`(절차),
`2026-09-11-game-update-2850.md`, `2026-09-18-game-update-2944.md`.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from find_class import Image, find_col, find_vtables   # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))

# 실행 섹션의 경계. 그 위쪽 RVA 는 데이터로 본다.
PAT_DECL = re.compile(
    r'(?:^[ \t]*//\s*@aob\s+(\S+)[^\n]*\n)?'
    r'[ \t]*constexpr\s+const\s+char\*\s+(\w*Pattern)\s*=\s*'
    r'((?:"[^"]*"\s*)+);',
    re.S | re.M)
PAT_ID = re.compile(
    r'//\s*@class\s+(\S+)[^\n]*\n\s*inline\s+constexpr\s+std::uint16_t\s+'
    r'(\w+)\s*=\s*(\d+)\s*;')
# 이름 있는 상수와 같은 값이 숫자로 박힌 자리를 찾는 데 쓴다.
PAT_LIT = re.compile(r'0[xX]([0-9A-Fa-f]{6,8})\b')
PAT_RVA = re.compile(
    r'constexpr\s+std::uint(?:64_t|ptr_t)\s+(\w+)\s*=\s*(0[xX][0-9A-Fa-f]+)\s*;')


def src_files(root):
    out = []
    for base, _, names in os.walk(os.path.join(root, 'src')):
        for n in names:
            if n.endswith(('.h', '.cpp')):
                out.append(os.path.join(base, n))
    return sorted(out)


def rel(root, path):
    return os.path.relpath(path, root).replace('\\', '/')


def aob_to_regex(pat):
    out = b''
    for tok in pat.split():
        out += b'.' if tok == '??' else re.escape(bytes([int(tok, 16)]))
    return re.compile(out, re.DOTALL)


def scan_aob(img, rx):
    hits = []
    for lo, hi in img.exec_ranges():
        for m in rx.finditer(img.data, lo, hi):
            r = img.off_to_rva(m.start())
            if r is not None:
                hits.append(r)
    return hits


def file_version(path):
    """VS_FIXEDFILEINFO 를 리소스에서 안 꺼내고 파일에서 직접 찾는다."""
    data = open(path, 'rb').read(0x8000000)
    i = data.find(b'\xBD\x04\xEF\xFE')          # dwSignature
    if i < 0:
        return '?'
    ms, ls = struct.unpack_from('<II', data, i + 8)      # FileVersionMS/LS
    return '%d.%d.%d.%d' % (ms >> 16, ms & 0xFFFF, ls >> 16, ls & 0xFFFF)


def sections(img):
    pe = struct.unpack_from('<I', img.data, 0x3C)[0]
    nsec = struct.unpack_from('<H', img.data, pe + 6)[0]
    optsz = struct.unpack_from('<H', img.data, pe + 20)[0]
    soi = struct.unpack_from('<I', img.data, pe + 24 + 56)[0]
    base = pe + 24 + optsz
    out = []
    for i in range(nsec):
        b = base + i * 40
        name = img.data[b:b + 8].rstrip(b'\x00').decode('latin1')
        vsz, va = struct.unpack_from('<II', img.data, b + 8)
        chars = struct.unpack_from('<I', img.data, b + 36)[0]
        out.append((name, va, vsz, bool(chars & 0x20000000)))
    return soi, out


def message_ids(img, names):
    """RTTI 이름 -> 메시지 ID. cheat_report 와 같은 사슬을 짧게 쓴다."""
    want = set(names)
    found = {}
    pat = re.compile(rb'\.\?AV([A-Za-z0-9_]+)@[A-Za-z0-9_:@]*@@\x00')
    vt_of = {}
    for m in pat.finditer(img.data):
        short = m.group(1).decode()
        if short not in want:
            continue
        td = img.off_to_rva(m.start() - 0x10)
        if td is None:
            continue
        for col in find_col(img, td):
            for vt in find_vtables(img, col):
                vt_of.setdefault(short, []).append(vt)

    rev = {v: k for k, vs in vt_of.items() for v in vs}
    if not rev:
        return found
    # 정적 초기화: lea rax,[vtable] ; mov [rip+서술자],rax
    init = re.compile(rb'\x48\x8D\x05....\x48\x89\x05....', re.DOTALL)
    for lo, hi in img.exec_ranges():
        for m in init.finditer(img.data, lo, hi):
            off = m.start()
            here = img.off_to_rva(off)
            if here is None:
                continue
            vt = here + 7 + struct.unpack_from('<i', img.data, off + 3)[0]
            if vt not in rev:
                continue
            desc = here + 14 + struct.unpack_from('<i', img.data, off + 10)[0]
            doff = img.rva_to_off(desc)
            if doff is None:
                continue
            # 서술자 +0x0C 의 u32 가 ID 다(+0x08 은 방향).
            # cheat_report.py 와 같은 자리에서 읽는다.
            found.setdefault(rev[vt],
                             struct.unpack_from('<I', img.data, doff + 0xC)[0])
    return found


def here_is(img, rva, n=3):
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    off = img.rva_to_off(rva)
    if off is None:
        return '(파일 범위 밖)'
    out = []
    for i in md.disasm(img.data[off:off + 24], rva):
        out.append('%s %s' % (i.mnemonic, i.op_str))
        if len(out) >= n:
            break
    return ' / '.join(out) if out is not None else '(디스어셈블 실패)'


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    exe = sys.argv[1]
    root = REPO
    if '--src' in sys.argv:
        root = sys.argv[sys.argv.index('--src') + 1]

    img = Image(exe)
    files = src_files(root)
    text = {p: open(p, encoding='utf-8', errors='replace').read() for p in files}

    print('=' * 72)
    print('1. 파일')
    print('   %s' % exe)
    print('   FileVersion %s   크기 %d 바이트' % (file_version(exe), len(img.data)))
    soi, secs = sections(img)
    print('   SizeOfImage 0x%X, 섹션 %d개 (실행 가능만 *)' % (soi, len(secs)))
    exec_spans = []
    for name, va, vsz, x in secs:
        if x:
            print('     * %-10s RVA 0x%08X  vsz 0x%08X' % (name, va, vsz))
            exec_spans.append((va, va + vsz))

    def is_code(rva):
        return any(lo <= rva < hi for lo, hi in exec_spans)

    print()
    print('=' * 72)
    print('2. AOB 패턴 (실행 섹션에서만) - 1곳이면 자가 치유한다')
    npat = 0
    for p, s in text.items():
        for m in PAT_DECL.finditer(s):
            expect, name = m.group(1), m.group(2)
            lit = ''.join(re.findall(r'"([^"]*)"', m.group(3)))
            hits = scan_aob(img, aob_to_regex(lit))
            npat += 1
            # 기대값(`// @aob <n|any>`)이 적혀 있으면 그것과 견준다. 안 적혀
            # 있으면 "유일해야 한다" 가 기본이다.
            if expect == 'any':
                ok = True
            elif expect is not None:
                ok = len(hits) == int(expect, 0)
            else:
                ok = len(hits) == 1
            mark = 'OK  ' if ok else '**  '
            where = ('0x%08X' % hits[0]) if len(hits) == 1 else (
                '%d곳 %s' % (len(hits), ' '.join('0x%X' % h for h in hits[:4])))
            if expect is not None:
                where += '  (기대 %s)' % expect
            print('   %s%-26s %-34s %s' % (mark, name, where, rel(root, p)))
    print('   (%d개. ** 는 손봐야 한다 - 0곳이면 코드가 사라졌거나 바뀐 것,'
          ' 여러 곳이면 패턴을 늘려야 한다)' % npat)

    print()
    print('=' * 72)
    print('3. 메시지 ID  ** 여기가 제일 위험하다 - 틀려도 게임이 안 막는다 **')
    want = {}
    for p, s in text.items():
        for m in PAT_ID.finditer(s):
            want[m.group(2)] = (m.group(1), int(m.group(3)), p)
    if not want:
        print('   `// @class <RTTI 이름>` 이 달린 ID 상수가 없다.')
    else:
        live = message_ids(img, [c for c, _, _ in want.values()])
        bad = 0
        for const, (cls, cur, p) in sorted(want.items()):
            got = live.get(cls)
            if got is None:
                print('   ??  %-24s %-44s 소스 %-5d  exe 에서 못 찾음'
                      % (const, cls, cur))
                bad += 1
            elif got == cur:
                print('   OK  %-24s %-44s %d' % (const, cls, cur))
            else:
                print('   **  %-24s %-44s 소스 %-5d -> exe **%d**  (%s)'
                      % (const, cls, cur, got, rel(root, p)))
                bad += 1
        print('   (%d개 중 %d개가 어긋난다)' % (len(want), bad))

    print()
    print('=' * 72)
    print('4. 고정 RVA - 그 자리에 지금 무엇이 있나 (판정은 사람이 한다)')
    seen = set()
    for p, s in sorted(text.items()):
        rows = []
        for m in PAT_RVA.finditer(s):
            name, val = m.group(1), int(m.group(2), 16)
            if val < 0x1000:
                continue      # 오프셋 상수(+0x18 따위)는 RVA 가 아니다
            if (name, val) in seen:
                continue
            seen.add((name, val))
            rows.append((name, val))
        if not rows:
            continue
        print('   -- %s' % rel(root, p))
        for name, val in rows:
            kind = '코드  ' if is_code(val) else '데이터'
            what = '(런타임으로만 갈린다)' if kind == '데이터' else here_is(img, val)
            print('      %-26s 0x%08X  %s  %s' % (name, val, kind, what))

    print()
    print('=' * 72)
    print('5. 같은 값이 숫자로 박힌 자리 - 상수를 고쳐도 안 따라온다')
    named = {}
    declared = set()
    for p, body in text.items():
        for m in PAT_RVA.finditer(body):
            v = int(m.group(2), 16)
            if v < 0x1000:
                continue
            named.setdefault(v, set()).add(m.group(1))
            declared.add((p, body[:m.start()].count('\n') + 1))
    dup = 0
    for p, body in sorted(text.items()):
        for i, line in enumerate(body.split('\n'), 1):
            if (p, i) in declared or line.lstrip().startswith('//'):
                continue      # 선언 자신과, 주석에 남긴 옛 값은 건너뛴다
            for m in PAT_LIT.finditer(line):
                v = int(m.group(1), 16)
                if v not in named:
                    continue
                inlog = 'log::' in line
                print('   %-4s %s:%d  0x%X = %s%s'
                      % ('' if inlog else '**', rel(root, p), i, v,
                         '/'.join(sorted(named[v])),
                         '  (로그 문구 속 라벨)' if inlog else ''))
                dup += 1
    print('   (%d곳. ** 는 진짜 사용처라 상수로 바꿀 것. 로그 라벨은 문구만 낡는다)'
          % dup)

    print()
    print('=' * 72)
    print('다음 단계')
    print('  - 3번에 ** 가 있으면 **먼저 고친다.** 안전망이 없다.')
    print('  - 2번 0곳 · 4번 코드 자리가 엉뚱하면 해당 스펙의 재도출 경로를 탄다.')
    print('  - 데이터 전역과 지급 실행 지점은 게임을 켜고 로그·프로브로 갈린다.')
    print('    `구동 건너뜀: 확인되지 않은 자리 +<RVA>` -> ini `drive_sites`.')
    return 0


if __name__ == '__main__':
    sys.exit(main())

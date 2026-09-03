"""데이터 클래스의 필드 이름과 오프셋을 뽑는다.

    python tools/rtti/fields.py <exe> <클래스이름>

게임을 켜지 않고 파일만 보고 한다.

원리
----
데이터 표를 읽는 역직렬화 함수가 필드마다 이런 모양이다.

    lea  rdx, [rsi + 0x400]      <- 대상 필드 주소
    mov  r8d, 2                  <- 크기 (있을 때도 없을 때도 있다)
    call 읽기
    test al, al
    jne  계속
    lea  rax, [rip + disp]       <- "ItemInfo의 _maxEndurance를 ... 실패했다"
    jmp  끝

실패 메시지가 **필드 이름을 그대로 들고 있다.** 그래서 실패 메시지를
집는 `lea` 직전의 `lea rdx,[base+off]` 를 짝지으면 이름 -> 오프셋 표가
나온다.

함수 경계는 찾지 않는다. 처음에 `CC CC CC` 패딩으로 함수 시작을
잡고 `ret` 까지 훑으려 했는데, 실제 함수는 중간에 이른 `ret` 이
있어 28개 명령만 보고 멈췄다. 참조 자리마다 **그 앞 몇십 바이트만**
정렬을 맞춰 읽으면 된다 - 짝짓기는 국소적이다.

이 게임의 디버그 문자열은 UTF-8 이 아니라 **CP949** 다. 현지화 표와
다르다 - 그쪽은 UTF-8 이다.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from find_class import Image   # noqa: E402
import xref_data               # noqa: E402

import capstone                # noqa: E402

# 참조 자리 앞으로 이만큼만 읽는다. 필드 하나를 읽는 코드 조각이
# 이보다 길지 않다.
WINDOW = 0x60


def find_message_refs(img, cls):
    """`<클래스>의 _필드를 ...` 문자열의 (RVA, 필드이름)."""
    prefix = cls.encode('cp949')
    out = []
    start = 0
    while True:
        i = img.data.find(prefix, start)
        if i < 0:
            break
        start = i + 1
        if i > 0 and img.data[i - 1] != 0:
            continue          # 문자열 시작이 아니다
        end = img.data.find(b'\0', i, i + 300)
        if end < 0:
            continue
        text = img.data[i:end].decode('cp949', 'replace')
        m = re.search(r'(_[A-Za-z0-9_]+)', text)
        if m is None:
            continue
        rva = img.off_to_rva(i)
        if rva is not None:
            out.append((rva, m.group(1)))
    return out


def _decode_window(img, md, target, back):
    """target 에서 끝나도록 정렬을 맞춰 앞쪽 창을 디스어셈블한다.

    x86 은 가변 길이라 아무 데서나 시작하면 어긋난다. 시작점을 하나씩
    옮겨 보고 target 에 정확히 걸리는 것 중 가장 긴 것을 고른다.
    """
    best = None
    for start in range(target - back, target):
        off = img.rva_to_off(start)
        if off is None:
            continue
        code = img.data[off:off + (target - start) + 8]
        n = 0
        for ins in md.disasm(code, img.base + start):
            n += 1
            if ins.address - img.base == target:
                if best is None or n > best[1]:
                    best = (start, n)
                break
    if best is None:
        return []
    start = best[0]
    off = img.rva_to_off(start)
    code = img.data[off:off + (target - start) + 8]
    return list(md.disasm(code, img.base + start))


# 스택 지역변수는 대상 객체가 아니다. 이 베이스로 나온 짝은 버린다.
STACK_BASES = ('rsp', 'rbp', 'esp', 'ebp')


def field_at(img, md, at):
    """참조 자리 바로 앞의 `lea reg,[base+disp]` 를 돌려준다.

    없거나 스택 지역변수면 None 이다. 색인이 붙은 형태
    (`lea rdx,[rsi+rax*2]`)는 오프셋이 상수가 아니라 못 짝짓는다 -
    그 자리는 앞의 lea 를 물려받지 않도록 버려야 한다. 안 그러면
    엉뚱한 필드에 같은 오프셋이 붙는다.
    """
    last = None
    for ins in _decode_window(img, md, at, WINDOW):
        if ins.mnemonic != 'lea' or len(ins.operands) != 2:
            continue
        src = ins.operands[1]
        if src.type != capstone.x86.X86_OP_MEM:
            continue
        if src.mem.base == capstone.x86.X86_REG_RIP:
            continue          # 문자열을 집는 lea 다
        if src.mem.index != 0:
            last = None       # 상수 오프셋이 아니다
            continue
        if src.mem.disp == 0:
            continue
        base = ins.reg_name(src.mem.base)
        last = None if base in STACK_BASES else (src.mem.disp, base)
    return last


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    img = Image(sys.argv[1])
    cls = sys.argv[2]

    refs = find_message_refs(img, cls)
    if not refs:
        print('"%s..." 로 시작하는 실패 메시지가 없습니다' % cls)
        return 2
    wanted = dict(refs)
    print('%s: 실패 메시지 %d개' % (cls, len(wanted)))

    # 문자열이 한 덩어리에 모여 있으므로 한 번만 훑는다. 하나씩
    # 훑으면 실행 섹션 전체를 124번 다시 읽는다.
    lo = min(wanted)
    hi = max(wanted) + 2
    hits = [(here, tgt) for here, _tag, tgt in xref_data.scan(img, lo, hi)
            if tgt in wanted]
    print('참조 %d곳' % len(hits))

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True

    rows = {}
    for here, tgt in hits:
        got = field_at(img, md, here)
        if got is None:
            continue
        disp, base = got
        rows.setdefault(wanted[tgt], (disp, base, here))

    # 같은 오프셋에 두 이름이 붙으면 둘 중 하나는 짝짓기가 어긋난
    # 것이다. 조용히 넘기지 않고 표시한다.
    seen = {}
    for name, (disp, _b, _a) in rows.items():
        seen.setdefault(disp, []).append(name)

    print()
    print('%-44s %-11s %s' % ('필드', '오프셋', '읽는 자리'))
    for name, (disp, base, at) in sorted(rows.items(), key=lambda kv: kv[1][0]):
        warn = '  <- 겹침' if len(seen[disp]) > 1 else ''
        print('%-44s +0x%-8X RVA 0x%08X  (%s)%s'
              % (name, disp, at, base, warn))
    dup = sum(1 for v in seen.values() if len(v) > 1)
    print()
    print('%d개 짝지었습니다 (메시지 %d개 중), 오프셋이 겹치는 것 %d자리'
          % (len(rows), len(wanted), dup))
    return 0


if __name__ == '__main__':
    sys.exit(main())

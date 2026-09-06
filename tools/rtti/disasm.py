"""RVA 하나를 함수로 보고 디스어셈블한다. RIP 상대 주소를 풀어 준다.

    python tools/rtti/disasm.py <exe> <RVA> [최대명령수=120]

게임을 켜지 않고 파일만 본다. `ret` 이나 무조건 `jmp` 를 만나면 멈춘다.
"""
import sys
import os
import struct

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from find_class import Image   # noqa: E402


def annotate(img, rva, n=16):
    """대상 주소가 무엇처럼 보이는지 짧게 말한다."""
    off = img.rva_to_off(rva)
    if off is None:
        return '(파일 밖)'
    b = img.data[off:off + n]
    txt = b.split(b'\0')[0]
    if len(txt) >= 4 and all(32 <= c < 127 for c in txt):
        return '"%s"' % txt.decode()
    return ' '.join('%02X' % c for c in b[:8])


def disasm(img, rva, limit=120, indent=''):
    off = img.rva_to_off(rva)
    if off is None:
        print('%s(RVA 0x%X 는 파일에 없습니다)' % (indent, rva))
        return []
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    code = img.data[off:off + limit * 15]
    calls = []
    for ins in md.disasm(code, img.base + rva):
        here = ins.address - img.base
        note = ''
        if 'rip' in ins.op_str:
            tgt = here + ins.size + _rip_disp(ins)
            note = '   ; 0x%08X  %s' % (tgt, annotate(img, tgt))
        elif ins.mnemonic in ('call', 'jmp') and ins.op_str.startswith('0x'):
            tgt = int(ins.op_str, 16) - img.base
            note = '   ; RVA 0x%08X' % tgt
            if ins.mnemonic == 'call':
                calls.append(tgt)
        print('%s0x%08X  %-9s %-40s%s'
              % (indent, here, ins.mnemonic, ins.op_str, note))
        if ins.mnemonic == 'ret':
            break
        if ins.mnemonic == 'jmp' and not ins.op_str.startswith('0x'):
            break
        limit -= 1
        if limit <= 0:
            break
    return calls


def _rip_disp(ins):
    for op in ins.operands:
        if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
            return op.mem.disp
    # lea 가 아닌 형태를 놓치면 0 을 돌려준다
    return 0


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    img = Image(sys.argv[1])
    rva = int(sys.argv[2], 0)
    limit = int(sys.argv[3], 0) if len(sys.argv) > 3 else 120
    disasm(img, rva, limit)
    return 0


if __name__ == '__main__':
    sys.exit(main())

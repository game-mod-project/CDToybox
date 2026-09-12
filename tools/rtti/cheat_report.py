"""치트 메시지 클래스를 전부 훑어 한 장으로 낸다.

    python tools/rtti/cheat_report.py <exe> [이름조각=Cheat]

게임을 켜지 않고 파일만 본다. 클래스마다 다음을 뽑는다.

    메시지 ID · 방향 · 서술자 · vtable · 역직렬화 · 처리기
    페이로드 필드 순서(바이트) · 처리기가 부르는 실제 작업 함수

`find_class.py` / `map_cheats.py` 로 하나씩 보다가, 어느 치트가 가장
쓰기 쉬운지 비교하려면 전부를 같은 잣대로 봐야 해서 만들었다.
"""
import os
import re
import struct
import sys

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from find_class import Image, find_col, find_vtables   # noqa: E402

MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
MD.detail = True

# 여러 번 읽는 조합 리더들. 크기를 재귀로 더한다.
COMPOSITE = (0x120CB20, 0x120CC00, 0x10C46C0, 0x120CBA0)


def func_end(img, rva, cap=0x1000):
    """int3 가 넷 이상 이어지면 함수 끝으로 본다. MSVC 가 그렇게 채운다."""
    off = img.rva_to_off(rva)
    if off is None:
        return 0
    buf = img.data[off:off + cap]
    for i in range(len(buf) - 3):
        if buf[i:i + 4] == b'\xcc\xcc\xcc\xcc':
            return i
    return len(buf)


def read_sizes(img, rva, depth=0, cache=None):
    """이 함수가 전선에서 읽는 바이트를 순서대로 낸다."""
    if cache is None:
        cache = {}
    if rva in cache:
        return cache[rva]
    if depth > 4:
        return []
    off = img.rva_to_off(rva)
    if off is None:
        return []
    end = func_end(img, rva)
    out, pend = [], None
    for ins in MD.disasm(img.data[off:off + end], img.base + rva):
        if ins.mnemonic == 'mov' and ins.op_str.startswith('r8d,'):
            try:
                pend = int(ins.op_str.split(',')[1].strip(), 0)
            except ValueError:
                pend = None
        elif ins.mnemonic == 'call':
            if ins.op_str == 'qword ptr [rax + 8]':
                if pend is not None:
                    out.append(('직접', pend))
                    pend = None
            elif ins.op_str.startswith('0x'):
                t = int(ins.op_str, 16) - img.base
                if t in COMPOSITE:
                    inner = read_sizes(img, t, depth + 1, cache)
                    out.append(('0x%X' % t, sum(n for _k, n in inner)))
        elif ins.mnemonic == 'ret':
            break
    cache[rva] = out
    return out


def find_handler(img, deser_rva):
    """역직렬화 본문에서 성공 직전의 처리기 호출을 찾는다.

    파싱을 마치고 성공했을 때만 부르므로 "call rel32" 뒤에
    "mov dword ptr [reg], 0" 이 온다. 어느 레지스터를 쓰는지는
    클래스마다 다르므로 흔한 것들을 모두 본다."""
    off = img.rva_to_off(deser_rva)
    if off is None:
        return None, ''   # 호출부가 (handler, how) 로 푼다 - None 하나면 TypeError
    end = func_end(img, deser_rva)
    body = img.data[off:off + end]
    hits = []
    # mod=00 인 rax rcx rdx rbx rsi rdi. 04 는 SIB, 05 는 RIP 상대다.
    for modrm in (0x00, 0x01, 0x02, 0x03, 0x06, 0x07):
        mark = bytes([0xC7, modrm, 0, 0, 0, 0])
        s = 0
        while True:
            i = body.find(mark, s)
            if i < 0:
                break
            s = i + 1
            if i >= 5 and body[i - 5] == 0xE8:
                rel = struct.unpack_from('<i', body, i - 4)[0]
                hits.append(deser_rva + i + rel)
    uniq = sorted(set(hits))
    if len(uniq) == 1:
        return uniq[0], 'ok'
    if uniq:
        # 여러 개면 성공 경로가 여럿이라는 뜻이다. 마지막 것을 낸다.
        return uniq[-1], '후보 %d개' % len(uniq)
    # 표시가 아예 없는 클래스도 있다. 마지막 호출로 대신한다.
    cs = calls_in(img, deser_rva)
    if cs:
        return cs[-1], '마지막 호출'
    return None, ''


def calls_in(img, rva):
    """함수가 부르는 rel32 대상들, 나온 순서대로."""
    off = img.rva_to_off(rva)
    if off is None:
        return []
    end = func_end(img, rva)
    out = []
    for ins in MD.disasm(img.data[off:off + end], img.base + rva):
        if ins.mnemonic == 'call' and ins.op_str.startswith('0x'):
            out.append(int(ins.op_str, 16) - img.base)
    return out


def has_permission_gate(img, handler_rva):
    """처리기 앞머리에 가상 호출로 된 검사가 있는가.

    바닥 스폰에서 `call [rax+0x140]` 이 거짓이면 조용히 반환하는 것을
    실측했다. 그 모양이 있는지 본다."""
    off = img.rva_to_off(handler_rva)
    if off is None:
        return ''
    end = min(func_end(img, handler_rva), 0x80)
    for ins in MD.disasm(img.data[off:off + end], img.base + handler_rva):
        if ins.mnemonic == 'call' and ins.op_str.startswith('qword ptr [rax +'):
            return ins.op_str.replace('qword ptr ', '')
    return ''


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    img = Image(sys.argv[1])
    needle = sys.argv[2] if len(sys.argv) > 2 else 'Cheat'

    pat = re.compile(rb'\.\?AV[A-Za-z0-9_:@]*' + re.escape(needle.encode()) +
                     rb'[A-Za-z0-9_:@]*@@\x00')
    vt_of = {}
    for m in pat.finditer(img.data):
        td = img.off_to_rva(m.start() - 0x10)
        if td is None:
            continue
        short = m.group(0).rstrip(b'\0').decode()[4:].split('@')[0]
        for col in find_col(img, td):
            for vt in find_vtables(img, col):
                # 다중 상속이면 vtable 이 여럿이다. 정적 초기화가
                # 넣는 것이 어느 것인지 모르므로 전부 들고 있는다.
                vt_of.setdefault(short, []).append(vt)

    # 정적 초기화에서 vtable -> 서술자
    desc_of = {}
    primary = {}
    rev = {}
    for k, vs in vt_of.items():
        for v in vs:
            rev[v] = k
    # 실행 섹션만 훑는다. 전체 파일을 바이트로 도는 것보다 훨씬 빠르고,
    # 정적 초기화 코드는 어차피 실행 섹션에 있다.
    init_re = re.compile(bytes([0x48, 0x8D, 0x05]) + b'....' +
                         bytes([0x48, 0x89, 0x05]) + b'....', re.S)
    for lo, hi in img.exec_ranges():
        chunk = img.data[lo:hi]
        for m in init_re.finditer(chunk):
            off = lo + m.start()
            here = img.off_to_rva(off)
            if here is None:
                continue
            d1 = struct.unpack_from('<i', img.data, off + 3)[0]
            vt = here + 7 + d1
            if vt not in rev:
                continue
            d2 = struct.unpack_from('<i', img.data, off + 10)[0]
            who = rev[vt]
            desc_of[who] = here + 14 + d2
            primary[who] = vt

    rows = []
    for name, vts in sorted(vt_of.items()):
        vt = primary.get(name, vts[0])
        desc = desc_of.get(name)
        row = {'name': name, 'vtable': vt, 'desc': desc}
        if desc is not None:
            o = img.rva_to_off(desc)
            if o is not None:
                row['dir'] = struct.unpack_from('<I', img.data, o + 8)[0]
                row['id'] = struct.unpack_from('<I', img.data, o + 0xC)[0]
                row['hdr'] = struct.unpack_from('<H', img.data, o + 0x18)[0]
                row['max'] = struct.unpack_from('<H', img.data, o + 0x1A)[0]
        vo = img.rva_to_off(vt)
        if vo is not None:
            va = struct.unpack_from('<Q', img.data, vo + 0x10)[0]
            row['deser'] = va - img.base
            row['fields'] = read_sizes(img, row['deser'])
            row['handler'], row['how'] = find_handler(img, row['deser'])
            if row['handler']:
                row['gate'] = has_permission_gate(img, row['handler'])
                row['calls'] = calls_in(img, row['handler'])
        rows.append(row)

    # 처리기들이 공통으로 부르는 것은 로깅·정리 같은 헬퍼다. 그
    # 클래스만 부르는 호출이 진짜 작업 함수다 - 실측에서 바닥 스폰의
    # 작업 함수(0x26A2C50)는 그 처리기에서만 불렸고, 모두가 부르던
    # 0x2520670 은 공용 헬퍼였다.
    freq = {}
    for r in rows:
        for t in set(r.get('calls') or []):
            freq[t] = freq.get(t, 0) + 1
    for r in rows:
        cands = [t for t in (r.get('calls') or []) if freq.get(t, 0) == 1]
        r['worker'] = cands[-1] if cands else None

    rows.sort(key=lambda r: r.get('id', 0))
    for r in rows:
        print('=' * 72)
        print('%s' % r['name'])
        print('  ID %-6s 방향 %-3s  머리 %s바이트  최대 %s'
              % (r.get('id', '?'), r.get('dir', '?'), r.get('hdr', '?'),
                 r.get('max', '?')))
        print('  서술자 0x%-9X vtable 0x%-9X 역직렬화 0x%X'
              % (r.get('desc') or 0, r['vtable'], r.get('deser') or 0))
        h = r.get('handler')
        how = r.get('how') or ''
        print('  처리기 %s%s   실제 작업 %s'
              % ('0x%X' % h if h else '(못 찾음)',
                 (' [%s]' % how) if how and how != 'ok' else '',
                 '0x%X' % r['worker'] if r.get('worker') else '(없음)'))
        if r.get('gate'):
            print('  앞단 검사 %s' % r['gate'])
        f = r.get('fields') or []
        total = sum(n for _k, n in f)
        if f:
            print('  페이로드 %d바이트: %s'
                  % (total, ', '.join('%s%d' % ('' if k == '직접' else '*', n)
                                      for k, n in f)))
    print('=' * 72)
    print('클래스 %d개' % len(rows))
    return 0


if __name__ == '__main__':
    sys.exit(main())

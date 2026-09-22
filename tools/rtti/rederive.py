"""게임 갱신 뒤 고정 자리를 **옛 문서의 경로 그대로** 새 exe 에서 다시 짚는다.

    py -3.14 tools/rtti/rederive.py <exe> [--src <레포 루트>]

`recheck.py` 가 "무엇이 낡았나" 를 알려 준다면 이것은 "새 값이 무엇인가" 를 낸다.
게임을 켜지 않고 파일만 본다. 2944·2949 갱신 대응에서 손으로 탔던 경로를 그대로
옮겼다(`docs/superpowers/specs/2026-09-22-game-update-2949.md` §2~§9).

**입력은 exe 하나다. 어느 빌드의 RVA 도 박아 두지 않았다.** 역직렬화 함수는 메시지
클래스 이름에서, 오류 슬롯은 오류 이름·화면 문구 문자열에서, vtable 은 RTTI 에서,
액터 조회는 소스의 AOB 에서 스스로 구한다. 박아 둔 것은 갱신을 안 탄 것뿐이다:

  - 이름 - 클래스 · 오류 이름 · 표 이름 · 화면 문구
  - 명령 바이트 지문 - specguard `div + 꼬리` · 소환 가드 꼬리 · 함수 프롤로그
  - 함수 **안** 오프셋 - 역직렬화 +0x180 같은 것. 2850·2944·2949 세 번 다 그대로였다

무엇을 내는가
-------------
절마다 경로와 근거를 찍고, 끝에 **소스 상수와 대조한 표**를 낸다.

  같다     소스 값이 이 exe 에서 다시 짚은 값과 같다
  다르다   소스를 고쳐야 한다 - 도출 값이 새 값이다(그래도 근거 줄을 읽고 넣을 것)
  못 찾음  경로가 끊겼다 - 지문이 바뀐 것이다. 해당 스펙의 경로를 손으로 탄다
  런타임   정적으로 안 갈린다 - 게임을 켜 `cdtb_probe` 로 본다

끝 표에 `다르다` · `못 찾음` 이 하나라도 있으면 종료 코드 1.

**판정은 사람이 한다.** 이 도구는 "옛 경로가 이 exe 에서 어디에 닿는가" 까지만
말한다. 특히 지급 구동 자리는 게임에서 성공 경로 줄(`구동 자리 …`)로 확인한 뒤에야
코드에 넣는다(`grant.cpp` 의 kGoodDriveSites 주석).

관련: recheck.py(무엇이 낡았나) · cheat_report.py(메시지 표) · funcstart.py(.pdata).
"""
import collections
import os
import re
import struct
import sys

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from find_class import (Image, find_col, find_lea_refs,  # noqa: E402
                        find_type_descriptors, find_vtables)
from funcstart import function_table, owner  # noqa: E402
from recheck import PAT_DECL, PAT_RVA, aob_to_regex, file_version, scan_aob  # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
MD.detail = True

# ------------------------------------------------------ 갱신을 안 타는 것

# specguard - `div + 꼬리` 바이트열(`specguard_sites.h` 주석 그대로). (표, 칸, 바이트, 뜻)
SPEC_LIVE = [
    ('kSpecguardDivMem', 3, '48 F7 77 08 8B 17', 'div [rdi+8] + mov edx,[rdi]  (착용)'),
    ('kSpecguardDivReg', 0, '49 F7 F0 3B 47 04', 'div r8 + cmp eax,[rdi+4]    (쌍둥이 앞)'),
    ('kSpecguardDivReg', 1, '49 F7 F0 48 85 D2', 'div r8 + test rdx,rdx       (쌍둥이 뒤)'),
    ('kSpecguardDivReg', 2, '49 F7 F6 8B CF', 'div r14 + mov ecx,edi'),
    ('kSpecguardDivReg', 3, '49 F7 F1 41 8B C8', 'div r9 + mov ecx,r8d'),
]
# 2944 부터 없는 가방 렌더 셋. 표에는 2850 값으로 남아 있다 - 다시 나타나면 알린다.
SPEC_STALE = [
    ('kSpecguardDivMem', 0, '48 F7 B5 F0 00 00 00', 'div [rbp+0xF0]'),
    ('kSpecguardDivMem', 1, '48 F7 74 24 38', 'div [rsp+0x38]'),
    ('kSpecguardDivMem', 2, '48 F7 75 C8', 'div [rbp-0x38]'),
]

# 작업 함수 경로(2944 스펙 §3). 함수 안 오프셋은 2850·2944·2949 내내 같았다.
MSG_HIRE = 'TrocTrHireMercenaryToTargetReq'
MSG_SPAWN = 'TrocTrSelectMercenarySpawnReq'
MSG_HIRE_INV = 'TrocTrHireMercenaryFromInventoryReq'
OFF_HIRE_WORK = 0x180    # 고용 역직렬화 -> 고용 작업
OFF_SPAWN_WRAP = 0x138   # 소환 역직렬화 -> 래퍼
OFF_SPAWN_WORK = 0x155   # 래퍼 -> 소환 작업
OFF_HIRE_CHECK = 0x3F3   # 고용 작업 -> 자격 검사 썽크
OFF_INV_WORK = 0x16B     # 소지품고용 역직렬화 -> 그 작업
OFF_INV_CHECK = 0x36A    # 그 작업 -> 같은 썽크여야 한다(교차 확인)
PRO_HIRE_WORK = '48 89 5C 24 10 4C 89 44 24 18'
PRO_SPAWN_WORK = '48 8B C4 4C 89 48 20 4C 89 40 18'
PRO_HIRE_BODY = '48 89 E0 48 89 58 08 44 89 48 20'   # grant.h kHireCheckBodyPrologue

# 소환 가드(`spawnguard_site.h`)
PRO_LOOKUP = '48 89 5C 24 08 83 79 14 00'
SPAWN_TAIL = '48 83 78 28 FF'   # kSpawnDerefTail - cmp qword [rax+0x28], -1

# 지식 등록(`knowledge.h` kKnowRegisterPrologue · 호출부 명령 배열은 TROUBLESHOOTING §1.13)
PRO_KNOW_REG = '44 89 44 24 18 66 89 54 24 10 48 89 4C 24 08'

# RTTI 이름
CLS_KNOW = '.?AVServerKnowledgeActorComponent@pa@@'
CLS_CTL = '.?AVClientCharacterControlActorComponent@pa@@'
CLS_WHEEL = '.?AVUIGamePlayControlRoot_ContentSlotMenu@uiCommonScript@pa@@'
CLS_EQUIP = '.?AVServerEquipSlotActorComponent@pa@@'
WHEEL_SLOT = 18          # ContentSlotMenu vtable[18] = 휠 UI
EQUIP_DRIVE_SLOT = 13    # ServerEquipSlotActorComponent vtable +0x68 = 구동 자리 메서드
# 그 메서드 안 액터 조회 호출 중 성공 경로였던 것의 반환 자리 오프셋. 2949 실측(메서드
# 0x2AD0F60 + 0x10D = 0x2AD106D). 2944 자리 0x2AD105D 도 같은 +0x10 이동이다. 그 메서드에는
# 액터 조회 호출이 둘 있다(2949: +0x10D · +0x187) - 둘째는 게임에서 확인한 적이 없다.
EQUIP_DRIVE_RET_OFF = 0x10D

# callcheck.h 의 함수 안 오프셋과 설치 때 보는 바이트
OFF_WHEEL_PRE_CALL = 0x486
OFF_PRE_VERIFIER_CALL = 0x14C
OFF_VERIFIER_LOOKUP_CALL = 0x5E
FOCUS_QUERY_BYTES = [(0x15, '48 8B 0D'), (0x1C, '48 8B 49 30')]
FOCUS_QUERY_CALL = 0x20
MAIN_PLAYER_BYTES = [(0x1A, '48 8B 79 50')]
FOCUS_ACTOR_BYTES = [(0x23, '48 8B 79 58'), (0xEF, '4C 8B B0 D8 00 00 00')]
VERIFIER_REASONS = 5     # kCallCheckReasons 앞 다섯은 검증기가, 뒤 둘은 앞단이 낸다

# skillgate 경로(skillgate.cpp 주석)
ERR_SKILL = 'eErrNoCannotLearnKnowledgeByFromType'

# 매니저 전역 표 이름 앵커(2944 스펙 §6)
TABLE_REGION = 'regioninfo'
TABLE_COND = 'conditioninfo'


# ------------------------------------------------------------ 도우미

def hx(s):
    return bytes.fromhex(s.replace(' ', ''))


def hexb(b):
    return ' '.join('%02X' % v for v in (b or b''))


def h(v):
    return '-' if v is None else '0x%X' % v


class Exe:
    """exe 한 벌과 .pdata 함수 경계."""

    def __init__(self, path):
        self.path = path
        self.img = Image(path)
        self.base = self.img.base
        self.table = function_table(self.img)
        self.keys = [b for b, _ in self.table]

    # -- 바이트
    def raw(self, rva, n):
        off = self.img.rva_to_off(rva)
        return None if off is None else self.img.data[off:off + n]

    def u64(self, rva):
        b = self.raw(rva, 8)
        return struct.unpack('<Q', b)[0] if b and len(b) == 8 else None

    def u32(self, rva):
        b = self.raw(rva, 4)
        return struct.unpack('<I', b)[0] if b and len(b) == 4 else None

    def has(self, rva, want_hex):
        w = hx(want_hex)
        return self.raw(rva, len(w)) == w

    # -- 함수
    def func(self, rva):
        return owner(self.table, self.keys, rva) if rva is not None else None

    def is_start(self, rva):
        f = self.func(rva)
        return f is not None and f[0] == rva

    def insns(self, begin, end):
        return list(MD.disasm(self.raw(begin, end - begin) or b'', self.base + begin))

    def func_insns(self, rva):
        f = self.func(rva)
        return f, (self.insns(*f) if f else [])

    def at(self, ins):
        return ins.address - self.base

    def rip(self, ins):
        for op in ins.operands:
            if (op.type == capstone.x86.X86_OP_MEM and
                    op.mem.base == capstone.x86.X86_REG_RIP):
                return self.at(ins) + ins.size + op.mem.disp
        return None

    def target(self, ins):
        if ins.op_str.startswith('0x') and (ins.mnemonic == 'call' or
                                            ins.mnemonic.startswith('j')):
            return int(ins.op_str, 16) - self.base
        return None

    def call_at(self, rva):
        b = self.raw(rva, 5) if rva is not None else None
        if not b or b[0] != 0xE8:
            return None
        return rva + 5 + struct.unpack_from('<i', b, 1)[0]

    def jmp_at(self, rva):
        b = self.raw(rva, 5) if rva is not None else None
        if not b or b[0] != 0xE9:
            return None
        return rva + 5 + struct.unpack_from('<i', b, 1)[0]

    def calls_in(self, rva):
        _f, ins = self.func_insns(rva)
        return [t for t in (self.target(i) for i in ins if i.mnemonic == 'call')
                if t is not None]

    # -- 전량 검색
    def exec_spans(self):
        for _n, va, _vs, rp, rs, fl in self.img.sections:
            if fl & 0x20000000 and rs:
                yield va, rp, rs

    def find_code(self, pat, limit=100000):
        """실행 섹션에서 바이트열이 있는 RVA."""
        out = []
        d = self.img.data
        for va, rp, rs in self.exec_spans():
            pos, end = rp, rp + rs
            while len(out) < limit:
                i = d.find(pat, pos, end)
                if i < 0:
                    break
                out.append(va + (i - rp))
                pos = i + 1
        return out

    def count_file(self, pat):
        d = self.img.data
        n = pos = 0
        while True:
            i = d.find(pat, pos)
            if i < 0:
                return n
            n += 1
            pos = i + 1

    def callers_of(self, targets):
        """E8 rel32 로 targets 를 부르는 명령의 RVA. 실행 섹션을 한 번만 훑는다."""
        want = set(targets)
        out = {t: [] for t in want}
        d = self.img.data
        for va, rp, rs in self.exec_spans():
            for m in re.finditer(rb'\xE8', d[rp:rp + rs]):
                i = m.start()
                t = va + i + 5 + struct.unpack_from('<i', d, rp + i + 1)[0]
                if t in want:
                    out[t].append(va + i)
        return out

    def rip_loads(self, targets):
        """`mov r32/r64, [rip+disp]` 로 targets 를 읽는 명령의 RVA. 한 번만 훑는다."""
        want = set(targets)
        out = {t: [] for t in want}
        d = self.img.data
        pat = re.compile(rb'[\x40-\x4F]?\x8B[\x05\x0D\x15\x1D\x25\x2D\x35\x3D]')
        for va, rp, rs in self.exec_spans():
            for m in pat.finditer(d, rp, rp + rs):
                s, e = m.start(), m.end()
                t = va + (e - rp) + 4 + struct.unpack_from('<i', d, e)[0]
                if t in want:
                    out[t].append(va + (s - rp))
        return out

    def strings(self, text):
        """정확히 그 문자열(앞뒤가 NUL)의 RVA. 없으면 끝에 마침표를 붙여 한 번 더 본다 -
        게임 화면 문구는 "…호출할 수 없습니다." 처럼 마침표로 끝나는데 소스의 인용에는
        마침표가 없다(2949 실측)."""
        d = self.img.data
        for t in (text, text + '.'):
            pat = t.encode('utf-8') + b'\0'
            out, pos = [], 0
            while True:
                i = d.find(pat, pos)
                if i < 0:
                    break
                pos = i + 1
                if i > 0 and d[i - 1] != 0:
                    continue
                r = self.img.off_to_rva(i)
                if r is not None:
                    out.append(r)
            if out:
                return out
        return []

    def vtables(self, exact):
        short = exact[4:].split('@')[0]
        out = []
        for name, td, _off in find_type_descriptors(self.img, short):
            if name != exact:
                continue
            for col in find_col(self.img, td):
                out.extend(find_vtables(self.img, col))
        return out

    def vt_slot(self, vt, k):
        q = self.u64(vt + 8 * k)
        return None if q is None else q - self.base


def message_table(x, shorts):
    """메시지 클래스 이름 -> {id, desc, vtable, deser}. cheat_report.py 와 같은 사슬이다:
    RTTI vtable -> 정적 초기화(`lea rax,[vtable] ; mov [rip+서술자],rax`) -> 서술자 +0x0C 가
    ID, 그 vtable 의 +0x10 칸이 역직렬화."""
    img = x.img
    want = set(shorts)
    vt_of = {}
    pat = re.compile(rb'\.\?AV([A-Za-z0-9_]+)@[A-Za-z0-9_:@]*@@\x00')
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
    out = {}
    init = re.compile(rb'\x48\x8D\x05....\x48\x89\x05....', re.S)
    for va, rp, rs in x.exec_spans():
        for m in init.finditer(img.data, rp, rp + rs):
            off = m.start()
            here = va + (off - rp)
            vt = here + 7 + struct.unpack_from('<i', img.data, off + 3)[0]
            if vt not in rev or rev[vt] in out:
                continue
            desc = here + 14 + struct.unpack_from('<i', img.data, off + 10)[0]
            deser = x.u64(vt + 0x10)
            out[rev[vt]] = {'id': x.u32(desc + 0xC), 'desc': desc, 'vtable': vt,
                            'deser': None if deser is None else deser - x.base}
    return out


def manager_global(x, fn):
    """함수 안에서 `mov R,[rip+전역]` 뒤 `[R+8]`(개수) · `[R+0x58]`(배열)을 쓰는 전역."""
    _f, ins = x.func_insns(fn)
    for k, i in enumerate(ins):
        g = x.rip(i)
        if i.mnemonic != 'mov' or g is None or ',' not in i.op_str:
            continue
        reg = i.op_str.split(',')[0].strip()
        nxt = ' | '.join(j.op_str for j in ins[k + 1:k + 14])
        if '[%s + 8]' % reg in nxt and '[%s + 0x58]' % reg in nxt:
            return g, x.at(i)
    return None, None


def slots_of(x, str_rvas, refs):
    """오류 등록에서 값 슬롯. 2949 에서 실측한 두 모양(2026-09-22):

        lea rcx,[슬롯] / mov r9d,2 / mov [rsp+0x20],rcx / lea r8,[설명] / mov rcx,rax /
        lea rdx,[이름] / call          <- 설명·이름을 함께 넘기는 등록
        lea r8,[슬롯] / mov rcx,rax / lea rdx,[이름] / call   <- 이름만 넘기는 등록

    그래서 문자열을 만드는 lea 와 **그 앞 call 사이**만 본다(앞뒤 몇 명령으로 보면 이웃
    등록의 슬롯이 끼어든다 - 실제로 4바이트 뒤 슬롯을 잡았다). 그 구간에 `lea rcx,[rip+..]`
    가 있으면 그것이, 없으면 `lea r8,[rip+..]` 가 슬롯이다(문자열 자신은 뺀다)."""
    want = set(str_rvas)
    out = set()
    for here, tgt in refs:
        if tgt not in want:
            continue
        _f, ins = x.func_insns(here)
        idx = next((k for k, i in enumerate(ins) if x.at(i) == here), None)
        if idx is None:
            continue
        k = idx - 1
        while k >= 0 and ins[k].mnemonic != 'call' and idx - k <= 12:
            k -= 1
        seg = ins[k + 1:idx]
        pick = {}
        for j in seg:
            g = x.rip(j)
            if j.mnemonic == 'lea' and g is not None and g not in want:
                pick.setdefault(j.op_str.split(',')[0], g)
        if 'rcx' in pick:
            out.add(pick['rcx'])
        elif 'r8' in pick:
            out.add(pick['r8'])
    return out


# ------------------------------------------------------------ 소스

def load_source(root):
    text = {}
    for base, _, names in os.walk(os.path.join(root, 'src')):
        for n in names:
            if n.endswith(('.h', '.cpp')):
                p = os.path.join(base, n)
                text[p] = open(p, encoding='utf-8', errors='replace').read()

    def file(rel):
        return text.get(os.path.join(root, *rel.split('/')), '')

    consts = {}
    for s in text.values():
        for m in PAT_RVA.finditer(s):
            consts[m.group(1)] = int(m.group(2), 16)

    spec = {}
    s = file('src/game/specguard_sites.h')
    for name in ('kSpecguardDivMem', 'kSpecguardDivReg'):
        m = re.search(name + r'\[\]\s*=\s*\{(.*?)\};', s, re.S)
        spec[name] = [(int(a, 16), int(b)) for a, b in
                      re.findall(r'\{\s*(0x[0-9A-Fa-f]+)\s*,\s*(\d+)\s*\}',
                                 m.group(1))] if m else []

    gate_re = re.compile(r'\{\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)",\s*'
                         r'(0x[0-9A-Fa-f]+),\s*(?://[^\n]*\n\s*)*\{([^}]*)\}')

    def gates(rel):
        out = []
        for m in gate_re.finditer(file(rel)):
            want = bytes(int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{2})\b', m.group(4)))
            quoted = re.search(r'\\"(.*?)\\"', m.group(2))
            out.append({'name': m.group(1), 'text': quoted.group(1) if quoted else None,
                        'rva': int(m.group(3), 16), 'want': want})
        return out

    m = re.search(r'kGoodDriveSites\[\]\s*=\s*\{([^}]*)\}', file('src/game/grant.cpp'))
    drive = [int(v, 16) for v in re.findall(r'0x[0-9A-Fa-f]+', m.group(1))] if m else []
    reasons = [(int(a, 16), b) for a, b in
               re.findall(r'\{\s*(0x[0-9A-Fa-f]+),\s*"(eErr\w+)"\s*\}',
                          file('src/game/callcheck.h'))]
    aob = {}
    for s in text.values():
        for m in PAT_DECL.finditer(s):
            aob[m.group(2)] = ''.join(re.findall(r'"([^"]*)"', m.group(3)))
    return {'consts': consts, 'spec': spec, 'skillgate': gates('src/game/skillgate.cpp'),
            'callgate': gates('src/game/callgate.cpp'), 'drive': drive,
            'reasons': reasons, 'aob': aob}


# ------------------------------------------------------------ 결과 표

class Rows:
    def __init__(self):
        self.rows = []

    def add(self, name, src, got, note='', runtime=False):
        if runtime:
            verdict = '런타임'
        elif got is None:
            verdict = '못 찾음'
        elif src is None:
            verdict = '소스 없음'
        else:
            verdict = '같다' if src == got else '다르다'
        self.rows.append((name, src, got, verdict, note))

    def bad(self):
        return [r for r in self.rows if r[3] in ('다르다', '못 찾음', '소스 없음')]


def title(n, text):
    print()
    print('=' * 76)
    print('%s. %s' % (n, text))


# ------------------------------------------------------------ 절

def sec_specguard(x, src, rows):
    title(1, 'specguard - div+꼬리 바이트열(각 1곳이어야) + 가족 표식')
    fam_calls, fam_globs = collections.Counter(), collections.Counter()
    fam_n = 0
    got_of = {}
    for table, idx, pat, what in SPEC_LIVE:
        hits = x.find_code(hx(pat))
        got = hits[0] if len(hits) == 1 else None
        entries = src['spec'].get(table, [])
        s_rva, s_len = entries[idx] if idx < len(entries) else (None, None)
        plen = len(hx(pat))
        print('   %s[%d] %-34s %d곳 %s' % (table, idx, what, len(hits),
                                           ' '.join(h(v) for v in hits[:4])))
        got_of[(table, idx)] = got
        note = 'patch_len %d%s' % (plen, '' if s_len == plen else ' (소스 %s)' % s_len)
        rows.add('%s[%d]' % (table, idx), s_rva, got, note)
        if got is None:
            continue
        f, ins = x.func_insns(got)
        calls = {x.target(i) for i in ins if i.mnemonic == 'call'} - {None}
        globs = {x.rip(i) for i in ins if i.mnemonic in ('mov', 'cmp')} - {None}
        gs = sum('gs:[0x58]' in i.op_str for i in ins)
        x1ec = sum('0x1ec' in i.op_str for i in ins)
        print('      함수 %s~%s  gs:[0x58] %d · 0x1EC %d' % (h(f[0]), h(f[1]), gs, x1ec))
        if gs:
            fam_n += 1
            fam_calls.update(calls)
            fam_globs.update(globs)
    common_c = [t for t, n in fam_calls.items() if n == fam_n and fam_n >= 2]
    common_g = [t for t, n in fam_globs.items() if n == fam_n and fam_n >= 2]
    print('   가족 표식(gs:[0x58] 가 있는 함수 %d개가 공통으로): 호출 %s · 전역 %s'
          % (fam_n, ' '.join(h(v) for v in common_c) or '없음',
             ' '.join(h(v) for v in common_g) or '없음'))
    a, b = got_of.get(('kSpecguardDivReg', 0)), got_of.get(('kSpecguardDivReg', 1))
    if a and b:
        same = x.func(a) == x.func(b)
        print('   쌍둥이 %s · %s: 같은 함수 %s, 간격 0x%X'
              % (h(a), h(b), '예' if same else '**아니다**', b - a))
    for table, idx, pat, what in SPEC_STALE:
        n = x.count_file(hx(pat))
        print('   %s[%d] %-18s 파일 전량 %d곳 %s' % (table, idx, what, n,
                                                  '(0 이 정상)' if n == 0 else '** 다시 나타났다'))


def sec_work(x, src, rows, msgs):
    title(2, '작업 함수 둘 · 자격 검사 (역직렬화 +0x180 / +0x138->+0x155 / +0x3F3 · +0x36A)')
    c = src['consts']
    d = {k: (msgs.get(k) or {}).get('deser') for k in (MSG_HIRE, MSG_SPAWN, MSG_HIRE_INV)}
    for k in (MSG_HIRE, MSG_SPAWN, MSG_HIRE_INV):
        m = msgs.get(k) or {}
        print('   %-38s ID %-5s 역직렬화 %s' % (k, m.get('id', '?'), h(m.get('deser'))))

    hire = x.call_at(d[MSG_HIRE] + OFF_HIRE_WORK) if d[MSG_HIRE] else None
    ok = hire is not None and x.has(hire, PRO_HIRE_WORK) and x.is_start(hire)
    f = x.func(hire)
    print('   고용 작업 %s  프롤로그 %s · %s' % (
        h(hire), '맞다' if ok else '**다르다**',
        '%d바이트' % (f[1] - f[0]) if f else '경계 없음'))
    rows.add('kHireWorkRva', c.get('kHireWorkRva'), hire if ok else None,
             '2944·2949 본문 2763바이트')

    wrap = x.call_at(d[MSG_SPAWN] + OFF_SPAWN_WRAP) if d[MSG_SPAWN] else None
    spawn = x.call_at(wrap + OFF_SPAWN_WORK) if wrap else None
    ok = spawn is not None and x.has(spawn, PRO_SPAWN_WORK) and x.is_start(spawn)
    fw = x.func(wrap)
    print('   소환 래퍼 %s (%s) -> 소환 작업 %s  프롤로그 %s' % (
        h(wrap), '%d바이트' % (fw[1] - fw[0]) if fw else '경계 없음', h(spawn),
        '맞다' if ok else '**다르다**'))
    rows.add('kSpawnWorkRva', c.get('kSpawnWorkRva'), spawn if ok else None,
             '래퍼 2944·2949 549바이트')

    thunk = x.call_at(hire + OFF_HIRE_CHECK) if hire else None
    body = x.jmp_at(thunk)
    body_ok = body is not None and x.has(body, PRO_HIRE_BODY)
    inv = x.call_at(d[MSG_HIRE_INV] + OFF_INV_WORK) if d[MSG_HIRE_INV] else None
    thunk2 = x.call_at(inv + OFF_INV_CHECK) if inv else None
    cross = thunk is not None and thunk == thunk2
    print('   자격 검사 썽크 %s (%s) -> 본체 %s 프롤로그 %s · 소지품고용 작업 %s +0x36A -> %s %s' % (
        h(thunk), hexb(x.raw(thunk, 5)) if thunk else '-', h(body),
        '맞다' if body_ok else '**다르다**', h(inv), h(thunk2),
        '(같은 썽크)' if cross else '**다르다**'))
    rows.add('kHireCheckRva', c.get('kHireCheckRva'),
             thunk if (body_ok and cross) else None, '썽크 -> 본체 %s' % h(body))


def sec_spawn_know(x, src, rows):
    title(3, '소환 가드 · 지식 등록 (호출부 전수 - 실행 섹션 한 번 훑기)')
    c = src['consts']
    lookup_c = x.find_code(hx(PRO_LOOKUP), 64)
    know_c = [v for v in x.find_code(hx(PRO_KNOW_REG), 64) if x.is_start(v)]
    callers = x.callers_of(lookup_c + know_c)

    # -- 소환 가드: 후보 중 `call ; lea reg,[rip+..]` 짝이 많은 쪽, 그 lea 대상은 만장일치여야
    best = None
    for cand in lookup_c:
        leas = collections.Counter()
        for site in callers[cand]:
            nb = x.raw(site + 5, 7)
            if nb and len(nb) == 7 and nb[0] in (0x48, 0x4C) and nb[1] == 0x8D and \
                    (nb[2] & 0xC7) == 0x05:
                leas[site + 12 + struct.unpack_from('<i', nb, 3)[0]] += 1
        pairs = sum(leas.values())
        print('   조회 후보 %s: 호출 %d곳, call+lea 짝 %d곳, lea 대상 %s' % (
            h(cand), len(callers[cand]), pairs,
            ' '.join('%s(%d)' % (h(t), n) for t, n in leas.most_common(3)) or '-'))
        if pairs and (best is None or pairs > best[1]):
            best = (cand, pairs, leas)
    lookup = best[0] if best else None
    empty = None
    if best:
        top, n = best[2].most_common(1)[0]
        empty = top if n == best[1] else None
        print('   -> 조회 %s · 빈 레코드 %s (%d/%d %s)' % (
            h(lookup), h(top), n, best[1], '만장일치' if empty else '**갈린다**'))
    rows.add('kSpawnLookupRva', c.get('kSpawnLookupRva'), lookup)
    rows.add('kSpawnEmptyRecordRva', c.get('kSpawnEmptyRecordRva'), empty)
    sites = [t - 5 for t in x.find_code(hx(SPAWN_TAIL)) if lookup and x.call_at(t - 5) == lookup]
    print('   E8 <조회> + %s: %d곳 %s' % (SPAWN_TAIL, len(sites), ' '.join(h(v) for v in sites[:4])))
    if len(sites) == 1:
        print('      그 자리 10바이트: %s' % hexb(x.raw(sites[0], 10)))
    rows.add('kSpawnCallSiteRva', c.get('kSpawnCallSiteRva'), sites[0] if len(sites) == 1 else None)

    # -- 지식 등록: 프롤로그가 함수 시작인 후보 중, 게임 호출부 모양(붙을 스킬 검사 ->
    #    r9b=1 -> rcx=r13 -> call)을 한 함수 안에 둘 이상 가진 것
    shaped = {}
    for cand in know_c:
        for site in callers[cand]:
            f, ins = x.func_insns(site)
            idx = next((k for k, i in enumerate(ins) if x.at(i) == site), None)
            if idx is None:
                continue
            win = ins[max(0, idx - 10):idx]
            text = ' / '.join('%s %s' % (i.mnemonic, i.op_str) for i in win)
            if ('r9b, 1' in text and 'mov rcx, r13' in text and
                    re.search(r'cmp word ptr \[\w+ \+ 0x104\]', text)):
                look = next((x.target(i) for i in reversed(win)
                             if i.mnemonic == 'call' and x.target(i) is not None), None)
                shaped.setdefault(cand, []).append((site, f, look))
    print('   지식 등록 프롤로그 후보(함수 시작) %d곳: %s' % (len(know_c), ' '.join(h(v) for v in know_c)))
    reg = look = None
    for cand, lst in shaped.items():
        by_func = collections.defaultdict(list)
        for site, f, lk in lst:
            by_func[f].append((site, lk))
        for f, items in by_func.items():
            items.sort()
            gaps = ['0x%X' % (b[0] - a[0]) for a, b in zip(items, items[1:])]
            print('   후보 %s: 모양 맞는 호출부 %s (함수 %s, 간격 %s; 2944 는 0xA8)' % (
                h(cand), ' '.join(h(s) for s, _ in items), h(f[0]), ' '.join(gaps) or '-'))
            if len(items) >= 2:
                reg, look = cand, items[0][1]
    rows.add('kKnowRegisterRva', c.get('kKnowRegisterRva'), reg)
    mgr, where = manager_global(x, look) if look is not None else (None, None)
    print('   붙을 스킬 검사 앞의 조회 %s -> 전역 %s (적재 %s)' % (h(look), h(mgr), h(where)))
    rows.add('kKnowMgrGlobalRva', c.get('kKnowMgrGlobalRva'), mgr, '지식 정보 조회가 읽는 전역')
    vts = x.vtables(CLS_KNOW)
    print('   %s vtable %s' % (CLS_KNOW, ' '.join(h(v) for v in vts)))
    rows.add('kServerCompVtableRva', c.get('kServerCompVtableRva'), vts[0] if len(vts) == 1 else None)
    return lookup


def sec_strings(x, src):
    """오류 이름·화면 문구·표 이름 문자열과 그 lea 참조를 한 번에 모은다."""
    texts = {ERR_SKILL, TABLE_REGION, TABLE_COND}
    texts.update(name for _s, name in src['reasons'])
    texts.update(g['text'] for g in src['callgate'] if g['text'])
    rvas = {t: x.strings(t) for t in texts}
    refs = find_lea_refs(x.img, [r for v in rvas.values() for r in v])
    return rvas, refs


def sec_callcheck(x, src, rows, lookup, rvas, refs, loads, slot_of):
    title(5, '호출 검증기 · 앞단 · 휠 UI · 관리자 함수 (오류 슬롯 -> 읽는 곳 -> 함수)')
    c = src['consts']
    reasons = src['reasons']
    for s_rva, name in reasons:
        got = sorted(slot_of.get(name) or [])
        print('   슬롯 %-44s 소스 %s · 등록에서 %s' % (name, h(s_rva), ' '.join(h(v) for v in got) or '못 찾음'))
        rows.add('슬롯 ' + name, s_rva, got[0] if len(got) == 1 else None)

    def funcs_reading(names):
        sets = []
        for n in names:
            fs = set()
            for sl in slot_of.get(n) or []:
                fs.update(x.func(r) for r in loads.get(sl, []))
            sets.append(fs - {None})
        return set.intersection(*sets) if sets else set()

    ver_names = [n for _s, n in reasons[:VERIFIER_REASONS]]
    pre_names = [n for _s, n in reasons[VERIFIER_REASONS:]]
    vf = funcs_reading(ver_names)
    verifier = min(vf)[0] if len(vf) == 1 else None
    vcall = x.call_at(verifier + OFF_VERIFIER_LOOKUP_CALL) if verifier else None
    print('   검증기 %s (앞 다섯 슬롯을 모두 읽는 함수 %d개) · +0x%X -> %s %s' % (
        h(verifier), len(vf), OFF_VERIFIER_LOOKUP_CALL, h(vcall),
        '(= 조회)' if vcall is not None and vcall == lookup else '**조회가 아니다**'))
    rows.add('kCallCheckFnRva', c.get('kCallCheckFnRva'), verifier)

    pf = {f for f in funcs_reading(pre_names)
          if x.call_at(f[0] + OFF_PRE_VERIFIER_CALL) == verifier}
    pre = min(pf)[0] if len(pf) == 1 else None
    print('   앞단 %s (뒤 둘 슬롯을 읽고 +0x%X 가 검증기를 부르는 함수 %d개)' % (
        h(pre), OFF_PRE_VERIFIER_CALL, len(pf)))
    rows.add('kWheelPreFnRva', c.get('kWheelPreFnRva'), pre)

    wheel = None
    for vt in x.vtables(CLS_WHEEL):
        s = x.vt_slot(vt, WHEEL_SLOT)
        cl = x.call_at(s + OFF_WHEEL_PRE_CALL) if s is not None else None
        print('   %s vtable %s [%d] = %s · +0x%X -> %s %s' % (
            CLS_WHEEL, h(vt), WHEEL_SLOT, h(s), OFF_WHEEL_PRE_CALL, h(cl),
            '(= 앞단)' if cl is not None and cl == pre else ''))
        if cl is not None and cl == pre:
            wheel = s
    rows.add('kWheelUiFnRva', c.get('kWheelUiFnRva'), wheel)

    fq = mp = fmgr = None
    if wheel:
        for t in sorted(set(x.calls_in(wheel))):
            if all(x.has(t + o, b) for o, b in FOCUS_QUERY_BYTES):
                m = x.call_at(t + FOCUS_QUERY_CALL)
                if m is not None and all(x.has(m + o, b) for o, b in MAIN_PLAYER_BYTES):
                    fq, mp = t, m
                    _f, ins = x.func_insns(t)
                    fmgr = next((x.rip(i) for i in ins if x.at(i) == t + 0x15), None)
    print('   관리자 조회 %s -> 관문① %s · 관리자 전역 %s' % (h(fq), h(mp), h(fmgr)))
    rows.add('kFocusQueryFnRva', c.get('kFocusQueryFnRva'), fq)
    rows.add('kMainPlayerCheckFnRva', c.get('kMainPlayerCheckFnRva'), mp)
    rows.add('kFocusMgrGlobalRva', c.get('kFocusMgrGlobalRva'), fmgr)
    fa = None
    if pre:
        for t in sorted(set(x.calls_in(pre))):
            if all(x.has(t + o, b) for o, b in FOCUS_ACTOR_BYTES):
                fa = t
    print('   포커스 관문 %s' % h(fa))
    rows.add('kFocusActorCheckFnRva', c.get('kFocusActorCheckFnRva'), fa)
    # 문구 없음 슬롯: 앞단 호출(+0x486) **바로 뒤**의 `cmp [rip+..], reg`(callcheck.h - 오류가
    # 그 값과 같으면 문구를 안 낸다). 휠 UI 전체로 보면 견주는 전역이 둘 이상이다.
    silent = set()
    if wheel:
        _f, ins = x.func_insns(wheel)
        idx = next((k for k, i in enumerate(ins) if x.at(i) == wheel + OFF_WHEEL_PRE_CALL), None)
        if idx is not None:
            silent = {x.rip(i) for i in ins[idx + 1:idx + 7] if i.mnemonic == 'cmp'} - {None}
    print('   앞단 호출 뒤에 견주는 전역(문구 없음 슬롯): %s' % (' '.join(h(v) for v in sorted(silent)) or '-'))
    rows.add('kSilentErrorRva', c.get('kSilentErrorRva'), min(silent) if len(silent) == 1 else None)


def branch_before(x, reader):
    """읽는 명령 바로 앞 명령이 조건 분기면 그 RVA."""
    _f, ins = x.func_insns(reader)
    idx = next((k for k, i in enumerate(ins) if x.at(i) == reader), None)
    if not idx:
        return None
    p = ins[idx - 1]
    return x.at(p) if p.mnemonic in ('je', 'jne') else None


def sec_gates(x, src, rows, rvas, loads, slot_of):
    title(6, 'skillgate · callgate (오류 이름·화면 문구 -> 값 슬롯 -> 읽는 곳 -> 창 8바이트)')
    sg = src['skillgate']
    slots = sorted(slot_of.get(ERR_SKILL) or [])
    print('   skillgate 슬롯(%s): %s' % (ERR_SKILL, ' '.join(h(v) for v in slots) or '못 찾음'))
    g0 = g1 = None
    readers = [r for sl in slots for r in loads.get(sl, [])]
    print('   그 슬롯을 읽는 곳 %d곳: %s' % (len(readers), ' '.join(h(v) for v in readers)))
    if len(readers) == 1:
        f, ins = x.func_insns(readers[0])
        prev = [i for i in ins if x.at(i) < readers[0] and i.mnemonic == 'call']
        thunk = x.target(prev[-1]) if prev else None
        cand0 = x.jmp_at(thunk)
        if cand0 is not None and sg and x.raw(cand0 & ~7, 8) == sg[0]['want'] and x.is_start(cand0):
            g0 = cand0
        setges = [x.at(i) for i in ins if i.mnemonic == 'setge' and i.op_str == 'byte ptr [rsp + 0x40]']
        ok1 = [a for a in setges if len(sg) > 1 and x.raw(a & ~7, 8) == sg[1]['want']]
        g1 = ok1[0] if len(ok1) == 1 else None
        print('   함수 %s~%s (%d바이트; 2944·2949 4437) · 직전 썽크 %s -> 관문0 %s · setge %s -> 관문1 %s' % (
            h(f[0]), h(f[1]), f[1] - f[0], h(thunk), h(cand0), ' '.join(h(a) for a in setges), h(g1)))
    if sg:
        rows.add('skillgate "%s"' % sg[0]['name'], sg[0]['rva'], g0, '창 %s' % hexb(sg[0]['want']))
    if len(sg) > 1:
        rows.add('skillgate "%s"' % sg[1]['name'], sg[1]['rva'], g1, '창 %s' % hexb(sg[1]['want']))

    for g in src['callgate']:
        slots = sorted(slot_of.get(g['text']) or []) if g['text'] else []
        hits = []
        for sl in slots:
            for r in loads.get(sl, []):
                b = branch_before(x, r)
                if b is not None and x.raw(b & ~7, 8) == g['want']:
                    hits.append(b)
        print('   callgate "%s" 문구 "%s": 슬롯 %s -> 창 일치 분기 %s' % (
            g['name'], g['text'], ' '.join(h(v) for v in slots) or '못 찾음',
            ' '.join(h(v) for v in hits) or '없음'))
        rows.add('callgate "%s"' % g['name'], g['rva'], hits[0] if len(hits) == 1 else None)


def sec_data(x, src, rows, rvas, refs):
    title(7, '데이터 전역 · vtable (그 전역을 읽는 코드로)')
    c = src['consts']
    for table, const in ((TABLE_REGION, 'kRegionMgrGlobalRva'), (TABLE_COND, 'kCondMgrGlobalRva')):
        strs = set(rvas.get(table) or [])
        funcs = {x.func(here) for here, tgt in refs if tgt in strs} - {None}
        got = set()
        for f in sorted(funcs):
            g, where = manager_global(x, f[0])
            if g is not None:
                got.add(g)
                print('   "%s" 를 만드는 함수 %s: 전역 %s (적재 %s)' % (table, h(f[0]), h(g), h(where)))
        rows.add(const, c.get(const), min(got) if len(got) == 1 else None, '표 이름 앵커')
    vts = x.vtables(CLS_CTL)
    print('   %s vtable %s' % (CLS_CTL, ' '.join(h(v) for v in vts)))
    rows.add('kCtlVtRva', c.get('kCtlVtRva'), vts[0] if len(vts) == 1 else None)
    for const, cls in (('kSlotMgrGlobalRva', 'ReserveSlotInfoManager'),
                       ('kGpvMgrGlobalRva', 'GamePlayVariableInfoManager')):
        rows.add(const, c.get(const), None,
                 'cdtb_probe instances .?AV%s@pa@@ -> 그 포인터를 담은 칸' % cls, runtime=True)


def sec_drive(x, src, rows):
    title(8, '지급 구동 자리 (ServerEquipSlotActorComponent vtable[13] 안의 액터 조회 호출)')
    pat = src['aob'].get('kActorGetterPattern')
    hits = scan_aob(x.img, aob_to_regex(pat)) if pat else []
    getter = hits[0] if len(hits) == 1 else None
    print('   액터 조회(kActorGetterPattern) %s' % ' '.join(h(v) for v in hits))
    found = []
    for vt in x.vtables(CLS_EQUIP):
        m = x.vt_slot(vt, EQUIP_DRIVE_SLOT)
        _f, ins = x.func_insns(m) if m is not None else (None, [])
        rets = [x.at(i) + i.size for i in ins if i.mnemonic == 'call' and getter is not None and
                x.target(i) == getter]
        print('   vtable %s [%d] = %s · 액터 조회 반환 자리 %s' % (
            h(vt), EQUIP_DRIVE_SLOT, h(m),
            ' '.join('%s(+0x%X)' % (h(v), v - m) for v in rets) or '없음'))
        found.extend(v for v in rets if m is not None and v - m == EQUIP_DRIVE_RET_OFF)
    newest = src['drive'][-1] if src['drive'] else None
    rows.add('kGoodDriveSites[-1]', newest, found[0] if len(found) == 1 else None,
             '메서드 +0x%X 의 반환 자리 · **게임에서 성공 경로 줄로 확인한 뒤에** 넣는다'
             % EQUIP_DRIVE_RET_OFF)
    for s in src['drive']:
        live = getter is not None and x.call_at(s - 5) == getter
        print('   소스 자리 %s: 이 exe 에서 액터 조회 반환 자리 %s' % (h(s), '예' if live else '아니다'))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    exe = sys.argv[1]
    root = sys.argv[sys.argv.index('--src') + 1] if '--src' in sys.argv else REPO
    src = load_source(root)
    x = Exe(exe)
    rows = Rows()
    print('=' * 76)
    print('%s' % exe)
    print('FileVersion %s · 크기 %d · 함수(.pdata) %d개' % (file_version(exe), len(x.img.data), len(x.table)))

    sec_specguard(x, src, rows)
    msgs = message_table(x, [MSG_HIRE, MSG_SPAWN, MSG_HIRE_INV])
    sec_work(x, src, rows, msgs)
    lookup = sec_spawn_know(x, src, rows)

    title(4, '문자열 앵커 (오류 이름 · 화면 문구 · 표 이름) -> 등록 -> 값 슬롯')
    rvas, refs = sec_strings(x, src)
    names = [n for _s, n in src['reasons']] + [ERR_SKILL] + \
        [g['text'] for g in src['callgate'] if g['text']]
    slot_of = {n: slots_of(x, rvas.get(n) or [], refs) for n in names}
    for n in names:
        print('   %-46s 문자열 %d곳 · 슬롯 %s' % (n, len(rvas.get(n) or []),
                                             ' '.join(h(v) for v in sorted(slot_of[n])) or '-'))
    loads = x.rip_loads({s for v in slot_of.values() for s in v})

    sec_callcheck(x, src, rows, lookup, rvas, refs, loads, slot_of)
    sec_gates(x, src, rows, rvas, loads, slot_of)
    sec_data(x, src, rows, rvas, refs)
    sec_drive(x, src, rows)

    title(9, '소스 대조')
    for name, s, g, verdict, note in rows.rows:
        mark = {'같다': 'OK  ', '런타임': '..  '}.get(verdict, '**  ')
        print('   %s%-34s 소스 %-11s 도출 %-11s %s%s' % (
            mark, name, h(s), h(g), verdict, ('  · ' + note) if note else ''))
    bad = rows.bad()
    print()
    print('   %d개 중 같다 %d · 다르다 %d · 못 찾음 %d · 런타임 %d' % (
        len(rows.rows), sum(r[3] == '같다' for r in rows.rows),
        sum(r[3] == '다르다' for r in rows.rows), sum(r[3] == '못 찾음' for r in rows.rows),
        sum(r[3] == '런타임' for r in rows.rows)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())

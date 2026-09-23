"""버프 파라미터 **배율 표**를 게임 이미지에서 뽑는다 (`src/game/buff_param_table.inc`).

    python tools/rtti/buff_params.py <덤프이미지> <vtable목록> [-o <나갈파일>]
    python tools/rtti/buff_params.py %TEMP%\\cdtb_image.exe vt_counts.json \\
           -o src/game/buff_param_table.inc

덤프 이미지는 `cdtb_probe dumpimage <경로>` 로 뜬다(**게임 실행 중에만** 된다 -
디스크 exe 는 Denuvo 로 코드가 안 보인다). 이미지가 없으면 이 도구는 조용히 빈
표를 내지 않고 **실패한다**.

vtable 목록은 둘 다 받는다.
  * JSON 객체 - `{"0x1458FBD20": 31, ...}` (값은 효과 줄 수, 참고용)
  * 줄 목록   - `0x1458FBD20 VaryStatMaxValue` (쉼표·공백 구분, `#` 는 주석)
이름을 안 적으면 이미지의 RTTI 에서 직접 읽는다.

왜 생성기인가
-------------
효과 줄의 `{Param1}` 자리에 들어갈 값은 BuffData 의 어느 칸을 **어떤 배율로**
나눈 것인데, 그 규칙이 **파생 클래스마다 다르다**(명세 §4.7-H''). 28종을 손으로
옮기면 틀리고, 게임이 갱신되면 전부 다시 해야 한다.

무엇을 읽는가
-------------
문구 조립 함수가 파라미터 종류마다 BuffData 의 **가상 함수 슬롯 11**
(`call qword ptr [rax+0x58]`)을 부른다. 인자는
`rcx`=BuffData · `rdx`=결과 자리 · `r8b`=파라미터 종류 · `r9b`=`_isDisplayAbsoluteNumber`.
그 함수 안에 "어느 칸을 어떤 배율로" 가 그대로 있다.

함정 셋 - 이 도구가 다루는 것
-----------------------------
1. **thunk** - 슬롯 11 이 `jmp` 하나인 클래스가 있다(0x1458FC230 · 0x1458FC1C0).
   대상까지 따라간다.
2. **분기** - 함수는 파라미터 종류로 갈라진다(`dec r8b` · `sub r8b, N` ·
   `test r8b, 0xFB`). **분기마다 결과가 다르다.** 한 함수에서 상수를 전부 긁어
   곱하면 틀린다. 그래서 정규식이 아니라 종류 0~8 마다 **따로 모의 실행**한다.
3. **나눗셈의 두 얼굴** - 정수는 컴파일러 매직 상수(`imul` + `sar`), 실수는
   `vdivsd`/`vmulsd` 의 rip 상대 상수다. 둘을 합쳐 최종 배율을 내되, **어느
   쪽이었는지를 `integer_div` 로 같이 낸다** - 정수 경로는 나머지가 버려지므로
   부르는 쪽이 잘라야 하고(25000/10⁴ -> 2), 실수 경로는 소수가 살아남아야 한다.

표의 키는 셋이다 - (vtable, 파라미터 종류, `+0x3A`)
---------------------------------------------------
배율은 클래스만으로 안 정해진다. **같은 클래스라도 두 가지로 더 갈린다.**

1. **파라미터 종류**(r8b). `DamageBuffData` 는 종류 1 이 `+0x98 ÷1000`,
   종류 3 이 `+0xF8 ÷10⁴` 다. `SetStatMinRate` 는 종류 1 이 `+0x98`,
   종류 2 가 `+0xA0` 이다.
2. **BuffData `+0x3A`**(r9b). 호출부(RVA 0x1F29931~)가
   `movzx r9d, byte ptr [rdi + 0x3a]` + `mov rcx, rdi` 로 넘기는 값이다 -
   **BuffData 자신의 바이트**지 파라미터 항목의 둘째 바이트가 아니다
   (그 둘째 바이트 `_isDisplayAbsoluteNumber` 는 호출부가 따로 본다:
   `cmp byte ptr [rax + r15*2 + 1], 0`). 비율형 클래스는 `cmp r9b, 1` 로
   갈라져 1 이면 ÷10⁷, 아니면 ÷1000 이 된다. 실측 분포는 0 이 2881개,
   1 이 232개다.

그래서 이 도구는 종류 0~7 × `+0x3A` 0·1 을 **전부 펼쳐** 한 조합에 한 줄씩 낸다.
조합마다 값이 같아도 줄을 합치지 않는다 - 부르는 쪽이 고르지 않고 찾게 한다.

종류 8(`{RepeatTick}`)은 여기 없다. 호출부가 슬롯 11 을 부르지 않고 BuffData
`+0x28`(주기 ms)을 1000.0 으로 직접 나눈다(RVA 0x1F29A51).

숫자가 아닌 파라미터
--------------------
`TribeAdditionalDamageRate` 의 종류 1 처럼 값이 숫자가 아니라 **표 조회(종족
이름)** 인 것이 있다. 배율이 없으므로 줄을 내지 않고 못 뽑은 목록에 적는다.

자기 검증
---------
아래 셋이 안 맞으면 **0이 아닌 종료 코드로 죽는다**(조용히 틀린 표를 내지 않는다).
셋 다 게임 툴팁과 글자까지 맞춘 줄에서 나온 실측이다.

    0x1458FBD20 VaryStatMaxValue        종류1 +0x3A=0 -> +0x98 ÷1000
    0x1458FA908 (어비스 소켓 치명타)     종류1 +0x3A=0 -> +0x98 ÷10000
    0x1458FB5A0 VaryDataDefinedStatRate 종류2 +0x3A=1 -> +0x98 ÷10000000
"""
import argparse
import json
import os
import re
import struct
import sys
from fractions import Fraction

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from find_class import Image                                      # noqa: E402

try:
    import capstone
    from capstone import x86 as cs86
except ImportError:                                               # pragma: no cover
    print('capstone 이 필요합니다: python -m pip install capstone', file=sys.stderr)
    raise

# 파라미터 게터가 사는 가상 함수 슬롯. `call qword ptr [rax + 0x58]` = 11번째 칸.
PARAM_SLOT = 11

# 자기 검증 셋 (실측, 명세 §4.7-H'·H''). 키는 (vtable, 종류, +0x3A).
SELF_CHECK = {
    (0x1458FBD20, 1, 0): (0x98, 1000.0),
    (0x1458FA908, 1, 0): (0x98, 10000.0),
    (0x1458FB5A0, 2, 1): (0x98, 10000000.0),
}

# 파라미터 종류. {Param0..3} = 0..3, {|Param0..3|} = 4..7.
# 8({RepeatTick})은 호출부가 BuffData +0x28 ÷1000.0 으로 직접 만든다 - 슬롯 11 에
# 오지 않으므로 훑지 않는다.
PARAM_KINDS = range(0, 8)

# 슬롯 11 의 r9b = BuffData +0x3A. 게임은 `cmp r9b, 1` 만 하므로 1 이 아닌 값은
# 전부 0 과 같다(실측 분포도 0·1 뿐이다).
FLAG3A_VALUES = (0, 1)

_REG64 = {}
for _base, _alias in [
    ('rax', ('rax', 'eax', 'ax', 'al', 'ah')),
    ('rbx', ('rbx', 'ebx', 'bx', 'bl', 'bh')),
    ('rcx', ('rcx', 'ecx', 'cx', 'cl', 'ch')),
    ('rdx', ('rdx', 'edx', 'dx', 'dl', 'dh')),
    ('rsi', ('rsi', 'esi', 'si', 'sil')),
    ('rdi', ('rdi', 'edi', 'di', 'dil')),
    ('rbp', ('rbp', 'ebp', 'bp', 'bpl')),
    ('rsp', ('rsp', 'esp', 'sp', 'spl')),
] + [('r%d' % i, ('r%d' % i, 'r%dd' % i, 'r%dw' % i, 'r%db' % i))
     for i in range(8, 16)]:
    for _a in _alias:
        _REG64[_a] = _base

VOLATILE = ('rax', 'rcx', 'rdx', 'r8', 'r9', 'r10', 'r11')

# 값을 읽어 주는 게터(`rcx`=칸 주소, `rdx`=결과 자리, `r8d`=겹침 수).
# 돌려주는 것은 `[rcx] + [rcx+0x10] * r8d` 다 - r8d=1 이면 사실상 그 칸 값이다.
GETTER_HINT = '값 게터(겹침 증분 +0x10 포함)'


def reg64(name):
    return _REG64.get(name, name)


# --------------------------------------------------------------- 이미지 도우미
class Code:
    def __init__(self, img):
        self.img = img
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        self.md.detail = True
        self._cache = {}

    def off(self, va):
        return self.img.rva_to_off(va - self.img.base)

    def qword(self, va):
        o = self.off(va)
        return struct.unpack_from('<Q', self.img.data, o)[0] if o is not None else None

    def dword(self, va):
        o = self.off(va)
        return struct.unpack_from('<I', self.img.data, o)[0] if o is not None else None

    def f32(self, va):
        o = self.off(va)
        return struct.unpack_from('<f', self.img.data, o)[0] if o is not None else None

    def f64(self, va):
        o = self.off(va)
        return struct.unpack_from('<d', self.img.data, o)[0] if o is not None else None

    def at(self, va):
        """va 한 명령. 같은 자리를 여러 번 보므로 캐시한다."""
        if va in self._cache:
            return self._cache[va]
        o = self.off(va)
        ins = None
        if o is not None:
            for i in self.md.disasm(self.img.data[o:o + 16], va):
                ins = i
                break
        self._cache[va] = ins
        return ins

    def cstr(self, va, n=64):
        o = self.off(va)
        if o is None:
            return ''
        return self.img.data[o:o + n].split(b'\0')[0].decode('utf-8', 'replace')


def class_name(code, vtable_va):
    """vtable 바로 앞 8바이트의 COL -> TypeDescriptor 이름."""
    img = code.img
    col_va = code.qword(vtable_va - 8)
    if not col_va or col_va < img.base:
        return ''
    col_rva = col_va - img.base
    o = img.rva_to_off(col_rva)
    if o is None:
        return ''
    sig, _off, _cd, td_rva, _chd, self_rva = struct.unpack_from('<IIIIII', img.data, o)
    if sig != 1 or self_rva != col_rva:
        return ''
    raw = code.cstr(img.base + td_rva + 0x10, 128)
    m = re.match(r'\.\?AV([A-Za-z0-9_]+)@', raw)
    return m.group(1) if m else raw


def slot_entry(code, vtable_va, index=PARAM_SLOT):
    return code.qword(vtable_va + index * 8)


def follow_thunk(code, va, limit=8):
    """슬롯이 `jmp <대상>` 하나뿐이면 대상까지 따라간다."""
    hops = 0
    for _ in range(limit):
        ins = code.at(va)
        if ins is None or ins.mnemonic != 'jmp' or not ins.op_str.startswith('0x'):
            break
        va = int(ins.op_str, 16)
        hops += 1
    return va, hops


def magic_divisor(magic, shift):
    """부호 있는 정수 나눗셈 매직 상수 -> 나누는 수. 안 맞으면 None."""
    if magic <= 0 or shift < 0 or shift > 63:
        return None
    exact = (1 << (64 + shift)) / magic
    d = round(exact)
    if d <= 1 or abs(exact - d) / d > 1e-9:
        return None
    return d


# ------------------------------------------------------------- 모의 실행기
class Walk:
    """파라미터 종류 하나를 정해 슬롯 11 함수를 따라간다.

    분기는 `r8b`(종류) · `r9b`(절대표시)에서 값이 정해지는 것만 따라가고,
    값이 안 정해지는 분기를 만나면 **거기서 멈춘다** - 그 뒤는 자릿수 맞추기
    (나머지 계산 · 소수 표기)라 배율과 상관이 없기 때문이다.
    """

    MAX_STEPS = 400

    def __init__(self, code, entry, kind, abs_flag):
        self.code = code
        self.entry = entry
        self.ptr = {'rcx': 0}       # 이 포인터에서 떨어진 거리를 아는 레지스터
        self.bval = {'r8': kind & 0xFF, 'r9': abs_flag & 0xFF}
        self.imm = {}               # 직전에 즉치가 들어간 레지스터
        self.xmm = {}               # rip 상수를 담은 xmm
        self.magic = {}             # movabs 로 들어온 나눗셈 매직
        self.awaiting_sar = None
        self.zf = None
        self.num = Fraction(1)
        self.den = Fraction(1)
        self.read = None
        self.outcome = 'none'       # none | number | object
        self.getter = False         # 겹침 증분을 더해 주는 값 게터를 거쳤는가
        self.trunc = False
        self.int_div = False        # 매직 상수(imul+sar) 로 나눴다 - **잘린다**
        self.float_div = False      # vdivsd/vmulsd 로 나눴다 - 소수가 남는다
        self.stop = ''
        self.steps = 0

    # -- 상태 손질 ---------------------------------------------------------
    def drop(self, r):
        self.ptr.pop(r, None)
        self.bval.pop(r, None)
        self.imm.pop(r, None)
        self.magic.pop(r, None)

    def note_read(self, off):
        if self.read is None:
            self.read = off

    def const_at(self, ins, op, wide):
        if op.type != cs86.X86_OP_MEM or op.mem.base != cs86.X86_REG_RIP:
            return None
        tgt = ins.address + ins.size + op.mem.disp
        return self.code.f64(tgt) if wide else self.code.f32(tgt)

    def mem_field(self, ins, op):
        """`[추적중인 this + disp]` 면 그 오프셋."""
        if op.type != cs86.X86_OP_MEM or op.mem.index != 0:
            return None
        if op.mem.base == 0:
            return None
        base = reg64(ins.reg_name(op.mem.base))
        if base not in self.ptr:
            return None
        return self.ptr[base] + op.mem.disp

    def is_stack(self, ins, op):
        return (op.type == cs86.X86_OP_MEM and op.mem.base != 0
                and reg64(ins.reg_name(op.mem.base)) == 'rsp')

    # -- 본체 -------------------------------------------------------------
    def run(self):
        va = self.entry
        while True:
            self.steps += 1
            if self.steps > self.MAX_STEPS:
                self.stop = '명령 상한'
                return self
            ins = self.code.at(va)
            if ins is None:
                self.stop = '코드 밖'
                return self
            nxt = self.step(ins)
            if nxt is None:
                return self
            va = nxt

    def step(self, ins):
        m, ops = ins.mnemonic, ins.operands
        nxt = ins.address + ins.size

        if m in ('ret', 'int3', 'ud2'):
            self.finish('ret')
            return None

        if m == 'jmp':
            if ins.op_str.startswith('0x'):
                return int(ins.op_str, 16)
            self.stop = '간접 jmp'
            return None

        if m in ('je', 'jz', 'jne', 'jnz'):
            if self.zf is None or not ins.op_str.startswith('0x'):
                self.finish('값 모르는 분기')      # 여기서 값은 이미 완성이다
                return None
            taken = self.zf if m in ('je', 'jz') else not self.zf
            return int(ins.op_str, 16) if taken else nxt

        if m[0] == 'j':
            self.finish('값 모르는 분기')
            return None

        if m == 'call':
            return self.do_call(ins, ops, nxt)

        self.do_data(ins, m, ops)
        return nxt

    def do_call(self, ins, ops, nxt):
        here = self.ptr.get('rcx')
        if ops and ops[0].type == cs86.X86_OP_MEM:
            self.finish('import 호출')            # roundf 따위 - 값은 완성이다
            return None
        if here:
            if 'r8' in self.imm:
                # 값 게터. 결과는 rdx 가 가리키는 스택 칸에 들어간다.
                self.note_read(here)
                self.outcome = 'number'
                self.getter = True
                for r in VOLATILE:
                    self.drop(r)
                self.imm.clear()
                self.zf = None
                return nxt
            # rcx 를 그대로 넘기고 rax 로 객체를 받는다 = 표 조회(문자열).
            self.note_read(here)
            self.outcome = 'object'
            self.stop = '표 조회'
            return None
        self.finish('형식화 호출')
        return None

    def finish(self, why):
        self.stop = why
        if self.read is None:
            # 값을 가리키는 포인터가 r9 로 넘어가는 꼴(`add r9, 0x94; call fmt`).
            off = self.ptr.get('r9')
            if off:
                self.note_read(off)
        if self.outcome == 'none' and self.read is not None:
            self.outcome = 'number'

    # -- 자료 처리 명령 ----------------------------------------------------
    def do_data(self, ins, m, ops):
        for op in ops:
            if op.type == cs86.X86_OP_MEM and (op.access & capstone.CS_AC_READ):
                off = self.mem_field(ins, op)
                if off is not None and m in (
                        'mov', 'movzx', 'movsx', 'movsxd', 'imul', 'add', 'cmp',
                        'vcvtsi2sd', 'vcvtsi2ss', 'cvtsi2sd', 'cvtsi2ss', 'cmovs'):
                    self.note_read(off)

        handled = self.apply(ins, m, ops)

        _rd, wr = ins.regs_access()
        touches_flags = False
        for r in wr:
            name = ins.reg_name(r)
            if name in ('eflags', 'rflags', 'flags'):
                touches_flags = True
                continue
            r64 = reg64(name)
            if r64 in handled.get('keep', ()):
                continue
            self.drop(r64)
            self.xmm.pop(name, None)
        if touches_flags and not handled.get('flags'):
            self.zf = None
        for r, v in handled.get('ptr', {}).items():
            self.ptr[r] = v
        for r, v in handled.get('bval', {}).items():
            self.bval[r] = v
        for r, v in handled.get('imm', {}).items():
            self.imm[r] = v
        for r, v in handled.get('xmm', {}).items():
            self.xmm[r] = v
        for r, v in handled.get('magic', {}).items():
            self.magic[r] = v

    def apply(self, ins, m, ops):
        out = {}

        def reg_of(op):
            return reg64(ins.reg_name(op.reg)) if op.type == cs86.X86_OP_REG else None

        # --- 포인터 산술
        if m == 'lea' and len(ops) == 2 and ops[1].type == cs86.X86_OP_MEM:
            dst = reg_of(ops[0])
            mem = ops[1].mem
            if mem.index == 0 and mem.base != 0:
                base = reg64(ins.reg_name(mem.base))
                if base in self.ptr:
                    out['ptr'] = {dst: self.ptr[base] + mem.disp}
                    out['keep'] = (dst,)
                    return out
                if base in ('r8', 'r9') and base in self.bval:
                    # `lea eax, [r8 - 1]` - 종류를 옮겨 담는 꼴
                    out['bval'] = {dst: (self.bval[base] + mem.disp) & 0xFF}
                    out['keep'] = (dst,)
                    return out
            return out

        if m == 'add' and len(ops) == 2 and ops[0].type == cs86.X86_OP_REG:
            dst = reg_of(ops[0])
            if ops[1].type == cs86.X86_OP_IMM and dst in self.ptr:
                out['ptr'] = {dst: self.ptr[dst] + ops[1].imm}
                out['keep'] = (dst,)
                return out
            if ops[1].type == cs86.X86_OP_IMM and dst in self.bval and ops[0].size == 1:
                v = (self.bval[dst] + ops[1].imm) & 0xFF
                out['bval'] = {dst: v}
                out['keep'] = (dst,)
                out['flags'] = True
                self.zf = (v == 0)
                return out
            return out

        if m in ('sub', 'dec', 'inc') and ops and ops[0].type == cs86.X86_OP_REG:
            dst = reg_of(ops[0])
            step = None
            if m == 'dec':
                step = -1
            elif m == 'inc':
                step = 1
            elif len(ops) == 2 and ops[1].type == cs86.X86_OP_IMM:
                step = -ops[1].imm
            if step is not None and dst in self.bval and ops[0].size == 1:
                v = (self.bval[dst] + step) & 0xFF
                out['bval'] = {dst: v}
                out['keep'] = (dst,)
                out['flags'] = True
                self.zf = (v == 0)
            return out

        # --- 값 옮기기
        if m in ('mov', 'movzx', 'movsxd', 'movsx') and len(ops) == 2:
            dst = reg_of(ops[0])
            if dst is None:
                return out
            src = ops[1]
            if src.type == cs86.X86_OP_REG:
                s = reg64(ins.reg_name(src.reg))
                keep = []
                if s in self.ptr and ops[0].size == 8:
                    out.setdefault('ptr', {})[dst] = self.ptr[s]
                    keep.append(dst)
                if s in self.bval and (src.size == 1 or m == 'movzx'):
                    out.setdefault('bval', {})[dst] = self.bval[s]
                    keep.append(dst)
                out['keep'] = tuple(keep)
                return out
            if src.type == cs86.X86_OP_IMM:
                out['imm'] = {dst: src.imm}
                out['bval'] = {dst: src.imm & 0xFF}
                out['keep'] = (dst,)
                return out
            return out

        if m == 'movabs' and len(ops) == 2 and ops[1].type == cs86.X86_OP_IMM:
            dst = reg_of(ops[0])
            out['magic'] = {dst: ops[1].imm & 0xFFFFFFFFFFFFFFFF}
            out['keep'] = (dst,)
            return out

        # --- 정수 나눗셈 (매직 + imul + sar)
        if m == 'imul':
            if len(ops) == 1 and 'rax' in self.magic:
                self.awaiting_sar = self.magic['rax']
            elif len(ops) == 3 and ops[2].type == cs86.X86_OP_IMM:
                if self.mem_field(ins, ops[1]) is not None:
                    self.num *= ops[2].imm
            return out

        if m == 'sar' and len(ops) == 2 and ops[1].type == cs86.X86_OP_IMM:
            if self.awaiting_sar is not None and reg_of(ops[0]) == 'rdx':
                d = magic_divisor(self.awaiting_sar, ops[1].imm)
                if d:
                    self.den *= d
                    self.int_div = True   # 정수 나눗셈이다 - 나머지가 버려진다
                self.awaiting_sar = None
            return out

        # --- 실수 나눗셈·곱셈
        if m in ('vdivsd', 'vdivss', 'vmulsd', 'vmulss', 'divsd', 'divss',
                 'mulsd', 'mulss'):
            wide = m.endswith('sd')
            src = ops[-1]
            c = self.const_at(ins, src, wide)
            if c is None and src.type == cs86.X86_OP_REG:
                c = self.xmm.get(ins.reg_name(src.reg))
            if c:
                f = Fraction(c).limit_denominator(1 << 40)
                if 'div' in m:
                    self.den *= f
                else:
                    self.num *= f
                self.float_div = True   # 실수 나눗셈이다 - 소수가 살아남는다
            return out

        if m in ('vmovss', 'vmovsd', 'movss', 'movsd') and len(ops) == 2:
            if ops[0].type == cs86.X86_OP_REG:
                c = self.const_at(ins, ops[1], m.endswith('sd'))
                if c is not None:
                    out['xmm'] = {ins.reg_name(ops[0].reg): c}
                    out['keep'] = (reg64(ins.reg_name(ops[0].reg)),)
            return out

        if m.startswith('vcvtt') or m.startswith('cvtt'):
            self.trunc = True
            return out

        # --- 비교
        if m in ('test', 'cmp') and len(ops) == 2:
            a = ops[0]
            if a.type == cs86.X86_OP_REG and a.size == 1:
                r = reg64(ins.reg_name(a.reg))
                if r in self.bval and ops[1].type == cs86.X86_OP_IMM:
                    v = self.bval[r]
                    self.zf = ((v & ops[1].imm) == 0) if m == 'test' else (v == ops[1].imm)
                    out['flags'] = True
            return out

        return out

    # -- 결과 -------------------------------------------------------------
    def result(self):
        if self.outcome != 'number' or self.read is None:
            return None
        div = self.den / self.num
        # **정수 나눗셈인가.** 매직 상수(`imul`+`sar`)로 나눴으면 나머지가
        # 버려지므로 부르는 쪽도 잘라야 한다(실측 25000/10⁴ -> 2). 실수
        # 경로(`vdivsd`/`vmulsd`)를 한 번이라도 지났으면 소수가 살아남는다 -
        # 둘이 섞이면 **안 자르는 쪽**으로 둔다(없는 자릿수를 만들지 않는다).
        return {'offset': self.read, 'divisor': div, 'trunc': self.trunc,
                'integer_div': self.int_div and not self.float_div,
                'getter': self.getter, 'stop': self.stop}


def analyze(code, vtable_va):
    """클래스 하나 -> (종류, +0x3A) 조합마다 규칙.

    조합을 **전부 펼친다**. 함수 하나에서 상수를 긁어모으면 분기가 섞여 틀리므로
    조합마다 따로 모의 실행한다.
    """
    raw = slot_entry(code, vtable_va)
    if not raw:
        return {'error': 'vtable 을 읽을 수 없음'}
    fn, hops = follow_thunk(code, raw)
    if code.off(fn) is None:
        return {'error': '슬롯 11 이 코드가 아님 (0x%X)' % fn}

    rules = {}
    objects = []
    for k in PARAM_KINDS:
        for flag in FLAG3A_VALUES:
            w = Walk(code, fn, k, flag).run()
            if w.outcome == 'object':
                objects.append((k, flag, w.read))
                continue
            r = w.result()
            if r:
                rules[(k, flag)] = r
    return {'fn': fn, 'raw': raw, 'thunk': hops > 0,
            'rules': rules, 'objects': objects}


def flag_sensitive(info):
    """`+0x3A` 값에 따라 칸이나 배율이 달라지는 종류들."""
    rules = info.get('rules') or {}
    out = []
    for k in PARAM_KINDS:
        a, b = rules.get((k, 0)), rules.get((k, 1))
        if a and b and (a['offset'], a['divisor']) != (b['offset'], b['divisor']):
            out.append(k)
    return out


# ------------------------------------------------------------------ 출력
def fmt_div(d):
    f = float(d)
    if f == int(f) and abs(f) < 1e18:
        return '%d.0' % int(f)
    return repr(f)


def fmt_div_short(d):
    f = float(d)
    return '%g' % f


def esc(s):
    return s.replace('\\', '\\\\').replace('"', '\\"')


def build_note(name, kind, rule, info):
    """줄에 붙일 말. 키(vtable·종류·+0x3A)에 이미 있는 것은 적지 않는다."""
    bits = [name or '(이름 없음)']
    if 4 <= kind <= 7:
        bits.append('절대값 표시')
    if rule.get('getter'):
        bits.append('겹침 증분 +0x%X' % (rule['offset'] + 0x10))
    if info.get('thunk'):
        bits.append('thunk')
    return ' · '.join(bits)


HEADER = """\
// 생성물이다 - 손으로 고치지 말 것.
//   python tools/rtti/buff_params.py <덤프이미지> <vtable목록> \\
//          --out src/game/buff_param_table.inc
//
// BuffData 파생 클래스의 vtable -> 효과 문구 값이 사는 칸과 배율.
// 뽑는 방법과 함정은 `tools/rtti/buff_params.py` 머리 주석, 근거는
// `docs/superpowers/specs/2026-09-22-item-description-effects-design.md` §4.7-H''.
//
// 키가 **셋**이다: vtable · 파라미터 종류(슬롯 11 의 r8b) · BuffData `+0x3A`
// (슬롯 11 의 r9b). 같은 클래스라도 종류마다 칸이 다르고, `+0x3A` 값마다 배율이
// 다르다. 조합이 표에 없으면 **모름**이다(`buff_param_divisor` 가 0 을 돌려준다) -
// 그런 효과 줄은 버리고 세기만 한다. 틀린 숫자를 보이는 것보다 낫다.
//
// 종류 8({RepeatTick})은 여기 없다. 호출부가 슬롯 11 을 부르지 않고 BuffData
// `+0x28`(주기 ms)을 1000.0 으로 나눠 직접 만든다(RVA 0x1F29A51).
//
// `integer_div` — 나눗셈이 **정수**인가(컴파일러 매직 상수 `imul`+`sar`). true 면
// 게임이 나머지를 버리므로 부르는 쪽도 잘라야 한다(실측 25000/10⁴ -> 2 · 75000/10⁴
// -> 7). false 는 `vdivsd`/`vmulsd` 경로라 **소수가 살아남는다** - 자르면 화면값이
// 2.5 여야 할 자리에 2 가 나간다. 둘이 섞인 조합은 false 다(없는 자릿수를 만들지
// 않는다).
//
// `note` 에 붙는 말
//   절대값 표시 - 종류 4~7 = `{|ParamN|}` 자리. 게임이 부호를 떼고 보인다.
//   겹침 증분   - 게임은 그 칸이 아니라 `[칸] + [증분칸] * 겹침수` 를 쓴다.
//                 툴팁은 겹침수 1 로 부른다.
//   thunk       - 슬롯 11 이 점프 하나이고 본체는 딴 데 있다.
//
// 이미지 %(image)s
// 클래스 %(total)d종 중 %(known)d종에서 규칙 %(rowcount)d줄을 얻었다.
// `+0x3A` 값에 따라 배율이 갈리는 클래스 %(fsens)d종:
%(fsens_lines)s
// 값이 숫자가 아니라 **표 조회(이름)** 인 조합 - 배율이 없어 줄을 내지 않았다:
%(object_lines)s
// 규칙을 못 얻은 %(unknown)d종:
%(unknown_lines)s
// vtable 주소는 모듈 고정 VA(기준 0x140000000)다. 게임이 갱신되면 다시 돌린다.

constexpr BuffParamRule kBuffParamRules[] = {
%(rows)s};
"""


def main(argv=None):
    ap = argparse.ArgumentParser(add_help=True, description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('image', help='cdtb_probe dumpimage 로 뜬 실행 이미지')
    ap.add_argument('vtables', help='vtable 목록(JSON 또는 줄 목록)')
    ap.add_argument('-o', '--out', help='나갈 파일(없으면 표준 출력)')
    args = ap.parse_args(argv)

    image = os.path.expandvars(args.image)
    if not os.path.isfile(image):
        print('이미지가 필요합니다: %s\n'
              '  게임을 켠 채 `cdtb_probe dumpimage <경로>` 로 뜨십시오.' % image,
              file=sys.stderr)
        return 2
    img = Image(image)
    code = Code(img)

    entries = load_vtables(args.vtables)
    if not entries:
        print('vtable 목록이 비었습니다: %s' % args.vtables, file=sys.stderr)
        return 2

    rows, unknown, classes, fsens, objs = [], [], [], [], []
    for vt, given_name in entries:
        name = given_name or class_name(code, vt)
        info = analyze(code, vt)
        if 'error' in info:
            unknown.append((vt, name, info['error']))
            continue
        if not info['rules']:
            why = '표 조회(이름)만 있음' if info['objects'] else '파라미터 없음'
            unknown.append((vt, name, why))
            continue
        classes.append(vt)
        sens = flag_sensitive(info)
        if sens:
            fsens.append((vt, name, sens, info))
        if info['objects']:
            objs.append((vt, name, info['objects']))
        for (kind, flag), rule in info['rules'].items():
            rows.append({'vt': vt, 'name': name, 'kind': kind, 'flag': flag,
                         'rule': rule, 'info': info})

    rows.sort(key=lambda r: (r['vt'], r['kind'], r['flag']))
    unknown.sort(key=lambda u: u[0])
    fsens.sort(key=lambda f: f[0])

    ok = self_check(rows)

    body = ''.join(
        '    {0x%XULL, %d, %d, 0x%X, %s, %s,\n     "%s"},\n'
        % (r['vt'], r['kind'], r['flag'], r['rule']['offset'],
           fmt_div(r['rule']['divisor']),
           'true' if r['rule']['integer_div'] else 'false',
           esc(build_note(r['name'], r['kind'], r['rule'], r['info'])))
        for r in rows)
    unknown_lines = ''.join(
        '//   0x%X  %-34s %s\n' % (vt, name or '(이름 없음)', why)
        for vt, name, why in unknown) or '//   (없음)\n'
    fsens_lines = ''.join(
        '//   0x%X  %-34s %s\n'
        % (vt, name or '(이름 없음)',
           ' · '.join('종류 %d: 0->÷%s 1->÷%s'
                      % (k, fmt_div_short(info['rules'][(k, 0)]['divisor']),
                         fmt_div_short(info['rules'][(k, 1)]['divisor']))
                      for k in ks))
        for vt, name, ks, info in fsens) or '//   (없음)\n'
    object_lines = ''.join(
        '//   0x%X  %-34s 종류 %s\n'
        % (vt, name or '(이름 없음)',
           ','.join(str(k) for k in sorted({k for k, _f, _o in items})))
        for vt, name, items in objs) or '//   (없음)\n'

    text = HEADER % {
        'image': os.path.basename(image),
        'total': len(entries), 'known': len(classes), 'unknown': len(unknown),
        'rowcount': len(rows), 'fsens': len(fsens),
        'fsens_lines': fsens_lines.rstrip('\n'),
        'object_lines': object_lines.rstrip('\n'),
        'unknown_lines': unknown_lines.rstrip('\n'),
        'rows': body,
    }

    report(rows, unknown, classes, fsens)
    if not ok:
        print('자기 검증 실패 - 표를 내지 않습니다.', file=sys.stderr)
        return 1

    if args.out:
        with open(args.out, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        print('썼습니다: %s (규칙 %d줄 · 클래스 %d종)'
              % (args.out, len(rows), len(classes)), file=sys.stderr)
    else:
        sys.stdout.write(text)
    return 0


def load_vtables(path):
    raw = open(path, encoding='utf-8').read()
    try:
        obj = json.loads(raw)
    except ValueError:
        obj = None
    out = []
    if isinstance(obj, dict):
        for k, v in obj.items():
            out.append((int(k, 16), v if isinstance(v, str) else ''))
        return out
    if isinstance(obj, list):
        for e in obj:
            if isinstance(e, str):
                out.append((int(e, 16), ''))
            elif isinstance(e, (list, tuple)) and e:
                out.append((int(e[0], 16), e[1] if len(e) > 1 else ''))
        return out
    for line in raw.splitlines():
        line = line.split('#')[0].strip().rstrip(',')
        if not line:
            continue
        parts = [p for p in re.split(r'[\s,]+', line) if p]
        try:
            vt = int(parts[0], 16)
        except ValueError:
            continue
        name = parts[1] if len(parts) > 1 and not parts[1].isdigit() else ''
        out.append((vt, name))
    return out


def self_check(rows):
    by_key = {(r['vt'], r['kind'], r['flag']): r['rule'] for r in rows}
    ok = True
    print('== 자기 검증 ==', file=sys.stderr)
    for (vt, kind, flag), (off, div) in sorted(SELF_CHECK.items()):
        got = by_key.get((vt, kind, flag))
        if got is None:
            print('  0x%X 종류%d +0x3A=%d  없음 (기대 +0x%X ÷%s)  FAIL'
                  % (vt, kind, flag, off, fmt_div_short(div)), file=sys.stderr)
            ok = False
            continue
        good = got['offset'] == off and float(got['divisor']) == div
        print('  0x%X 종류%d +0x3A=%d  +0x%X ÷%-10s (기대 +0x%X ÷%s)  %s'
              % (vt, kind, flag, got['offset'], fmt_div_short(got['divisor']),
                 off, fmt_div_short(div), 'OK' if good else 'FAIL'), file=sys.stderr)
        ok = ok and good
    return ok


def report(rows, unknown, classes, fsens):
    print('\n== 뽑은 규칙 %d줄 (클래스 %d종) =='
          % (len(rows), len(classes)), file=sys.stderr)
    for r in rows:
        print('  0x%X  종류%d  +0x3A=%d  +0x%-4X ÷%-12s %-4s %s'
              % (r['vt'], r['kind'], r['flag'], r['rule']['offset'],
                 fmt_div_short(r['rule']['divisor']),
                 '정수' if r['rule']['integer_div'] else '실수',
                 build_note(r['name'], r['kind'], r['rule'], r['info'])),
              file=sys.stderr)
    ints = sum(1 for r in rows if r['rule']['integer_div'])
    print('  (정수 나눗셈 %d줄 · 실수 %d줄)' % (ints, len(rows) - ints),
          file=sys.stderr)
    print('\n== +0x3A 로 배율이 갈리는 %d종 ==' % len(fsens), file=sys.stderr)
    for vt, name, ks, info in fsens:
        for k in ks:
            a, b = info['rules'][(k, 0)], info['rules'][(k, 1)]
            print('  0x%X  %-34s 종류%d  0 -> +0x%X ÷%s   1 -> +0x%X ÷%s'
                  % (vt, name or '(이름 없음)', k, a['offset'],
                     fmt_div_short(a['divisor']), b['offset'],
                     fmt_div_short(b['divisor'])), file=sys.stderr)
    print('\n== 못 뽑은 %d종 ==' % len(unknown), file=sys.stderr)
    for vt, name, why in unknown:
        print('  0x%X  %-34s %s' % (vt, name or '(이름 없음)', why), file=sys.stderr)


if __name__ == '__main__':
    sys.exit(main())

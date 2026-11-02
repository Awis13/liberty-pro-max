#!/usr/bin/env python3
"""Translate one x86 function of GTAIV.exe (integer + scalar SSE subset) into
plain C++ for src/native_*.cpp: integer registers as locals, the SSE
registers' low lanes as float bits, guest memory through Rd/Wr, no flags but
the ones a branch reads. A function that calls nothing and never takes its
frame's address keeps its frame in a local array; otherwise the frame stays
on the guest stack and calls go through the runtime (registers and the xmm
registers' low lanes handed over around them). Fails on anything it does not
model.
usage: x86n.py START END NAME > out.inc"""
import os, re, subprocess, sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OBJDUMP = os.environ.get('OBJDUMP', '/opt/homebrew/opt/llvm@22/bin/llvm-objdump')
EXE = os.environ['GTAIV_EXE']  # the player's own GTAIV.exe 1.0.8.0
SYMBOLS = os.path.join(HERE, 'symbols', 'gtaiv')
start, end, name = int(sys.argv[1], 16), int(sys.argv[2], 16), sys.argv[3]


def disasm(lo, hi):
    raw = subprocess.run([OBJDUMP, '-d', '--x86-asm-syntax=intel', '--no-show-raw-insn',
                          f'--start-address={lo}', f'--stop-address={hi}', EXE],
                         capture_output=True, text=True).stdout
    out = []
    for l in raw.split('\n'):
        m = re.match(r'^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$', l)
        if m:
            ops = re.sub(r'\s*#.*$', '', re.sub(r'\s*<[^>]*>', '', m.group(3))).strip()
            out.append((int(m.group(1), 16), m.group(2), ops))
    return out


lin = disasm(start, end)

R32 = ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi']
R8 = {'al': 'eax', 'cl': 'ecx', 'dl': 'edx', 'bl': 'ebx'}
R8H = {'ah': 'eax', 'ch': 'ecx', 'dh': 'edx', 'bh': 'ebx'}
R16 = {'ax': 'eax', 'cx': 'ecx', 'dx': 'edx', 'bx': 'ebx', 'si': 'esi', 'di': 'edi', 'bp': 'ebp'}
SIZE = {'byte': 1, 'word': 2, 'dword': 4, 'qword': 8, 'xmmword': 16}
TY = {1: 'uint8_t', 2: 'uint16_t', 4: 'uint32_t'}
IMM = re.compile(r'^-?(0x[0-9a-f]+|\d+)$')


def fail(a, msg):
    sys.exit(f'{a:08X}: {msg}')


def split_ops(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        depth += (ch == '[') - (ch == ']')
        if ch == ',' and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def imm(t, sz=4):
    return f'{int(t, 0) & ((1 << 8 * sz) - 1):#x}u'


def ops_raw(o):
    return ' '.join(o)


def xmm(op):
    m = re.match(r'^xmm(\d)$', op)
    return int(m.group(1)) if m else None


# The instructions reachable from the entry (a function's chunks may have
# other code between them), each with its fall-through address.
JT = {}
for l in open(os.path.join(SYMBOLS, 'jumptables.tsv')):
    p_ = l.rstrip('\n').split('\t')
    if len(p_) == 2 and re.match(r'^[0-9A-Fa-f]{8}$', p_[0]) and p_[1]:
        JT[int(p_[0], 16)] = [int(t, 16) for t in p_[1].split(',')]
by = {x[0]: x for x in lin}
nxt = {lin[k][0]: lin[k + 1][0] for k in range(len(lin) - 1)}
reach, work = set(), [start]
tails = set()
while work:
    a = work.pop()
    if a in reach:
        continue
    if a not in by:
        fail(a, 'jump into the middle of an instruction or out of the range')
    reach.add(a)
    _, mn, ops = by[a]
    if mn.startswith('j') and mn != 'jmp' or mn == 'jmp':
        if IMM.match(ops):
            t = int(ops, 16)
            if start <= t < end:
                work.append(t)
            elif mn == 'jmp':
                tails.add(a)
            else:
                fail(a, 'conditional jump out of the function')
        elif mn == 'jmp':
            if a not in JT:
                fail(a, 'indirect jump without a table')
            work.extend(JT[a])
    if mn in ('ret', 'jmp', 'int3', 'ud2'):
        continue
    if a not in nxt:
        fail(a, 'falls off the range')
    work.append(nxt[a])
ins = [x for x in lin if x[0] in reach]
fall = {}  # instruction -> the next one when it falls through
for x in ins:
    if x[1] not in ('ret', 'jmp', 'int3', 'ud2'):
        fall[x[0]] = nxt[x[0]]

# The frame stays local only if nothing but this code can see it.
calls = [x for x in ins if x[1] == 'call' or x[0] in tails]
escapes = [x for x in ins if x[1] == 'lea' and re.search(r'\[(esp|ebp)\b', x[2])]
local = not calls and not escapes
targets = set()
for x in ins:
    if x[1].startswith('j') and IMM.match(x[2]) and x[0] not in tails:
        targets.add(int(x[2], 16))
    if x[0] in JT:
        targets.update(JT[x[0]])
for k in range(len(ins) - 1):  # fall-through to code that is not next in line
    if ins[k][0] in fall and fall[ins[k][0]] != ins[k + 1][0]:
        targets.add(fall[ins[k][0]])

ret_of = {}
float_of = {}


def callee_float(t):
    """Whether the callee returns a float on the x87 stack (one value left at its ret)."""
    if t not in float_of:
        float_of[t] = False
        mx = None
        for l in open(os.path.join(SYMBOLS, 'functions.tsv')):
            if l.startswith(f'{t:08X}\t'):
                mx = int(l.split('\t')[2], 16)
                break
        if mx is not None and '--ret-fd' not in sys.argv:
            r = subprocess.run([sys.executable, __file__, f'{t:X}', f'{mx + 1:X}', 'N', '--ret-fd'], capture_output=True, text=True)
            float_of[t] = r.stdout.strip() == '1'
    return float_of[t]


def callee_ret(t):
    """The callee's ret N (the oracle's stubs pop as much)."""
    if t not in ret_of:
        n = 0
        for a, mn, ops in disasm(t, t + 0x4000):
            if mn == 'ret':
                n = int(ops, 0) if ops else 0
                break
        ret_of[t] = n
    return ret_of[t]


class Walk:
    def __init__(self, layout=None, hint=None, tolerant=False):
        self.hint = hint or {}
        self.hint_fk = {}
        self.kept = {}
        self.tolerant = tolerant
        self.lines = []
        self.sd = 0              # esp's displacement from self.base
        self.base = 'entry'
        self.at = {}             # label -> (base, sd) on the way there
        self.fk = {}             # label -> the flags' kinds arriving there
        self.flags = None
        self.fr = set()          # frame slots used (local mode): (base tag, offset)
        self.fw = set()          # ... written
        self.fw_all = None       # ... written by the whole function (an earlier pass)
        self.layout = layout or {}  # base tag -> (index in Fr, lowest offset)
        self.ftag = 'E'          # the frame's base: E entry esp, P the aligned prologue's, A<va> an and esp
        self.decl = set()
        self.skip = 0
        self.entry_frame = False
        self.ebp_sd = None       # local mode: ebp as a frame displacement
        self.fd = 0              # x87 stack depth (this function's own)
        self.after_call = False  # st0 may be a callee's result
        self.last_call = None    # the last direct callee
        self.x87ret = set()      # direct callees that return a float (x87)
        self.ret_fd = set()      # x87 depth at this function's returns
        self.cw_changed = False  # a fldcw other than the restore of a saved word in force
        self.saved_cw = None
        self.x87 = False         # uses x87 arithmetic (needs PC=24, RC=nearest)

    def E(self, s):
        self.lines.append(s)

    # --- operands
    def mem(self, a, op):
        m = re.match(r'^(byte|word|dword|qword|xmmword) ptr (\w+:)?\[(.*)\]$', op)
        if not m:
            fail(a, f'operand {op}')
        if m.group(2) and m.group(2) != 'fs:':
            fail(a, 'segment')
        seg = m.group(2) == 'fs:'
        sz = SIZE[m.group(1)]
        parts, disp = [], 0
        for sign, t in re.findall(r'([+-]?)\s*([^+-]+)', m.group(3).replace(' ', '')):
            if IMM.match(t):
                disp += (-1 if sign == '-' else 1) * int(t, 0)
            elif t in R32:
                if sign == '-':
                    fail(a, 'negative register')
                parts.append(t)
            elif re.match(r'^\d\*\w+$', t):
                k, r = t.split('*')
                parts.append(f'{k}u * {r}')
            else:
                fail(a, f'term {t}')
        if local and 'esp' in parts:
            if len(parts) != 1 or self.base != 'frame':
                fail(a, 'not a frame slot')
            off = self.sd + disp
            if self.ftag == 'E' and off >= 0:  # the return address and the arguments
                return ('guest', f'uint32_t(c.esp + {off:#x}u)', sz)
            self.fr.add((self.ftag, off))
            return ('frame', (self.ftag, off), sz)
        if local and 'ebp' in parts and self.ebp_sd is not None:
            if len(parts) != 1:
                fail(a, 'indexed ebp slot')
            off = self.ebp_sd + disp
            if off >= 0:
                return ('guest', f'uint32_t(c.esp + {off:#x}u)', sz)
            if self.ftag != 'E':
                fail(a, 'ebp-relative local after esp was aligned')
            self.fr.add(('E', off))
            return ('frame', ('E', off), sz)
        if local and 'ebp' in parts and not self.entry_frame and (len(parts) != 1 or disp < 8):
            fail(a, 'ebp: only arguments')
        e = ' + '.join(parts + ([f'{disp & 0xFFFFFFFF:#x}u'] if disp else []))
        if seg:  # fs: the thread's TIB (only its TLS pointer is read in the game's code)
            if parts or disp != 0x2C:
                fail(a, 'fs: other than [0x2c]')
            return ('guest', 'uint32_t(c.fs_base + 0x2cu)', sz)
        return ('guest', f'uint32_t({e or "0u"})', sz)

    def fidx(self, key):
        tag, off = key
        start_, lo = self.layout.get(tag, (0, off))
        return start_ + off - lo

    def fguest(self, key):
        tag, off = key
        return f'uint32_t(fb{tag} + {off & 0xFFFFFFFF:#x}u)'

    def written(self, key, n=4):
        for d in range(0, n):
            self.fw.add((key[0], key[1] + d))

    def never_written(self, key):
        return self.fw_all is not None and key not in self.fw_all

    def load(self, a, op, sz=4):
        if op in R32:
            return op
        if op in R8:
            return f'({R8[op]} & 0xFFu)'
        if op in R8H:
            return f'(({R8H[op]} >> 8) & 0xFFu)'
        if op in R16:
            return f'({R16[op]} & 0xFFFFu)'
        if IMM.match(op):
            return imm(op, sz)
        k, e, msz = self.mem(a, op)
        if msz not in TY:
            fail(a, f'{msz}-byte integer access')
        if k == 'frame':
            if self.never_written(e):
                return f'uint32_t(Rd<{TY[msz]}>({self.fguest(e)}))'  # never written here: what the stack holds
            return f'uint32_t(FrRd<{TY[msz]}>(Fr, {self.fidx(e)}))'
        return f'uint32_t(Rd<{TY[msz]}>({e}))'

    def store(self, a, op, val):
        if op in R32:
            return f'{op} = {val};'
        if op in R8:
            r = R8[op]
            return f'{r} = ({r} & ~0xFFu) | (({val}) & 0xFFu);'
        if op in R16:
            r = R16[op]
            return f'{r} = ({r} & ~0xFFFFu) | (({val}) & 0xFFFFu);'
        if op in R8H:
            r = R8H[op]
            return f'{r} = ({r} & ~0xFF00u) | ((({val}) & 0xFFu) << 8);'
        k, e, sz = self.mem(a, op)
        if sz not in TY:
            fail(a, f'{sz}-byte integer store')
        if k == 'frame':
            self.written(e, sz)
            return f'FrWr<{TY[sz]}>(Fr, {self.fidx(e)}, {TY[sz]}({val}));'
        return f'Wr<{TY[sz]}>({e}, {TY[sz]}({val}));'

    def opsize(self, op):
        if op in R32:
            return 4
        if op in R8 or op in R8H:
            return 1
        if op in R16:
            return 2
        m = re.match(r'^(byte|word|dword) ptr', op)
        return SIZE[m.group(1)] if m else 4

    def xload(self, a, op, wide=False):
        """An SSE source: its low lane (bits), or all four lanes (V)."""
        x = xmm(op)
        if x is not None:
            return f'X{x}' if wide else f'X{x}.l[0]'
        k, e, sz = self.mem(a, op.replace('xmmword ptr', 'dword ptr') if not wide else op)
        if wide:
            if sz == 4 and op.startswith('dword'):
                fail(a, 'packed operation on a dword')
            return self.frd(k, e, 'V')
        return self.frd(k, e, 'uint32_t')

    def frd(self, k, e, t):
        if k != 'frame':
            return f'Rd<{t}>({e})'
        if self.never_written(e):
            return f'Rd<{t}>({self.fguest(e)})'  # never written here: what the stack holds
        return f'FrRd<{t}>(Fr, {self.fidx(e)})'

    def dload(self, a, op):
        x = xmm(op)
        if x is not None:
            return f'GetD(X{x}, 0)'
        k, e, sz = self.mem(a, op.replace('xmmword ptr', 'qword ptr'))
        return self.frd(k, e, 'double')

    def xstore(self, a, op, val, wide):
        k, e, sz = self.mem(a, op)
        t = 'V' if wide else 'uint32_t'
        if k == 'frame':
            self.written(e, 16 if wide else 4)
            return f'FrWr<{t}>(Fr, {self.fidx(e)}, {val});'
        return f'Wr<{t}>({e}, {val});'

    def jump(self, a, t):
        self.fk.setdefault(t, set()).add(self.flags)
        if t in self.kept and self.kept[t] != self.flags and not self.tolerant:
            fail(a, f'flags differ on the way to {t:08X}, used there')
        st = (self.base, self.sd, self.fd) if local else ('guest', 0, self.fd)
        if t in self.at and self.at[t] != st and not self.tolerant:
            fail(a, f'esp differs on the way to {t:08X}')
        self.at[t] = st

    def run(self):
        seq = ins
        i = 0
        fallthrough = True
        while i < len(seq):
            a, mn, ops = seq[i]
            o = split_ops(ops)
            if a in targets:
                if not fallthrough:
                    st = self.at.get(a) or self.hint.get(a)
                    if st is None and not self.tolerant:
                        fail(a, 'label never jumped to')
                    if st is not None:
                        self.base, self.sd, self.fd = st
                else:
                    self.jump(a, a)
                self.E(f'L_{a:08X}:')
                kinds = self.fk.get(a, set()) | self.hint_fk.get(a, set())
                if len(kinds) == 1 and None not in kinds:
                    self.flags = next(iter(kinds))  # the same kind from every path (set on each)
                    self.kept[a] = self.flags
                else:
                    self.flags = None
            elif not fallthrough:
                fail(a, 'not reached (cfg)')
            fallthrough = True
            self.E(f'  // {a:08X} {mn} {ops}')
            if local and a == start:
                want = [('push', 'ebp'), ('mov', 'ebp, esp'), ('and', 'esp, -0x10')]
                if [x[1:] for x in seq[:3]] == want and seq[3][1] == 'sub' and seq[3][2].startswith('esp,'):
                    nsub = int(seq[3][2].split(',')[1], 0)
                    self.E('  ebp = c.esp - 4u;')
                    self.E(f'  [[maybe_unused]] const uint32_t fbP = (ebp & ~0xFu) - {nsub:#x}u;  // where the original\'s frame is')
                    self.base, self.sd, self.ftag = 'frame', 0, 'P'
                    i += 4
                    continue
                self.base, self.sd, self.ftag = 'frame', 0, 'E'  # esp-relative from the entry
                self.E('  ebp = c.ebp;')
                self.E('  [[maybe_unused]] const uint32_t fbE = c.esp;')
                self.entry_frame = True
            self.skip = 0
            self.one(a, mn, o, seq, i)
            if mn in ('jmp', 'ret') or self.lines[-1].endswith('return;'):
                fallthrough = False
            i += self.skip + 1
            last = seq[i - 1][0]
            if fallthrough and last in fall and (i >= len(seq) or seq[i][0] != fall[last]):
                self.jump(last, fall[last])
                self.E(f'  goto L_{fall[last]:08X};')
                fallthrough = False

    def one(self, a, mn, o, seq, i):
        E = self.E
        if mn == 'push':
            v = self.load(a, o[0])
            self.sd -= 4
            if local:
                E('  ' + self.store(a, 'dword ptr [esp]', v))
            else:
                E(f'  {{ const uint32_t v = {v}; esp -= 4; Wr<uint32_t>(esp, v); }}')
        elif mn == 'pop':
            if local:
                if o[0] == 'ebp' and self.ebp_sd is not None:
                    self.ebp_sd = None
                    E('  ebp = c.ebp;')
                elif o[0] == 'ebp' and not self.entry_frame:
                    fail(a, 'pop ebp outside the epilogue')
                else:  # ebp an ordinary register in a function without a frame pointer
                    E('  ' + self.store(a, o[0], self.load(a, 'dword ptr [esp]')))
            else:
                E('  { const uint32_t v = Rd<uint32_t>(esp); esp += 4; ' + self.store(a, o[0], 'v') + ' }')
            self.sd += 4
        elif local and mn == 'mov' and o == ['esp', 'ebp']:
            if seq[i + 1][1:] != ('pop', 'ebp') or seq[i + 2][1] != 'ret':
                fail(a, 'unexpected epilogue')
            self.ret_fd.add(self.fd)
            self.spill_x87()  # a float result, or values the caller left
            nret = int(seq[i + 2][2], 0) if seq[i + 2][2] else 0
            E(f'  c.eax = eax; c.ecx = ecx; c.edx = edx; c.esp += {4 + nret}u; XmmOut(c, X0, X1, X2, X3, X4, X5, X6, X7); return;')
            self.skip = 2
        elif mn == 'ret':
            self.ret_fd.add(self.fd)
            self.ret_fd.add(self.fd)
            self.spill_x87()  # a float result, or values the caller left
            nret = int(o[0], 0) if o else 0
            if local:
                if self.sd != 0:
                    fail(a, 'ret with a frame')
                E(f'  c.eax = eax; c.ecx = ecx; c.edx = edx; c.esp += {4 + nret}u; XmmOut(c, X0, X1, X2, X3, X4, X5, X6, X7); return;')
            else:
                E('  c.eax = eax; c.ecx = ecx; c.edx = edx; c.ebx = ebx; c.ebp = ebp; c.esi = esi; c.edi = edi;')
                E(f'  c.esp = esp + {4 + nret}u; XmmOut(c, X0, X1, X2, X3, X4, X5, X6, X7); return;')
        elif mn == 'call':
            self.spill_x87()
            ret = seq[i + 1][0] if i + 1 < len(seq) else a + 5
            target = None if IMM.match(o[0]) else self.load(a, o[0])  # before the push (esp-relative operands)
            if target is not None:
                E(f'  {{ const uint32_t t = {target}; esp -= 4; Wr<uint32_t>(esp, {ret:#x}u); calltarget = t; }}')
            else:
                E(f'  esp -= 4; Wr<uint32_t>(esp, {ret:#x}u);')
            E('  c.eax = eax; c.ecx = ecx; c.edx = edx; c.ebx = ebx; c.esp = esp; c.ebp = ebp; c.esi = esi; c.edi = edi;')
            E('  XmmOut(c, X0, X1, X2, X3, X4, X5, X6, X7);')
            if IMM.match(o[0]):
                t = int(o[0], 16)
                self.decl.add(t)
                E(f'  CALL_DIRECT(f_{t:08X}, {t:#x}u, {callee_ret(t)});')
                self.last_call = t
                if callee_float(t):
                    self.x87ret.add(t)
            else:
                E(f'  CALL_INDIRECT(calltarget, {a:#x}u);')
                self.last_call = None
            E('  eax = c.eax; ecx = c.ecx; edx = c.edx; ebx = c.ebx; esp = c.esp; ebp = c.ebp; esi = c.esi; edi = c.edi;')
            E('  XmmIn(c, X0, X1, X2, X3, X4, X5, X6, X7);')
            self.flags = None
            self.after_call = True
        elif local and mn == 'mov' and o == ['ebp', 'esp']:
            self.ebp_sd = self.sd  # ebp: the frame at this displacement
            if self.ftag != 'E':
                fail(a, 'mov ebp, esp after an alignment')
            E(f'  ebp = uint32_t(fbE + {self.sd & 0xFFFFFFFF:#x}u);')
        elif local and mn == 'mov' and o == ['esp', 'ebp'] and self.ebp_sd is not None:
            self.sd = self.ebp_sd
        elif mn in ('mov', 'movzx', 'movsx'):
            if local and 'esp' in o:
                fail(a, 'esp moved in local mode')
            if not local and o[0] == 'esp':
                self.base, self.sd = ('mov', a), 0
            if not local and o == ['ebp', 'esp']:
                self.ebp_at = (self.base, self.sd)
            sz = self.opsize(o[1]) if mn != 'mov' else self.opsize(o[0])
            v = self.load(a, o[1], self.opsize(o[0]))
            if mn == 'movsx':
                v = f'uint32_t(int32_t(int{8 * sz}_t({v})))'
            E('  ' + self.store(a, o[0], v))
        elif mn == 'lea' and o[0] == 'esp' and not local:
            k, e, sz = self.mem(a, o[1].replace('[', 'dword ptr [', 1))
            E(f'  esp = {e};')
            self.base, self.sd = ('lea', a), 0
        elif mn == 'lea':
            if o[0] == 'esp':
                fail(a, 'lea esp')
            k, e, sz = self.mem(a, o[1].replace('[', 'dword ptr [', 1))
            if k != 'guest':
                fail(a, 'lea of the frame')
            E(f'  {o[0]} = {e};')
        elif mn in ('mul', 'imul') and len(o) == 1 and self.opsize(o[0]) == 4:
            v = self.load(a, o[0])
            if mn == 'mul':
                E(f'  {{ const uint64_t p = uint64_t(eax) * {v}; eax = uint32_t(p); edx = uint32_t(p >> 32); }}')
            else:
                E(f'  {{ const int64_t p = int64_t(int32_t(eax)) * int32_t({v}); eax = uint32_t(p); edx = uint32_t(uint64_t(p) >> 32); }}')
            self.flags = None
        elif mn in ('add', 'sub', 'and', 'or', 'xor', 'shl', 'shr', 'sar', 'imul', 'inc', 'dec', 'neg', 'not'):
            self.alu(a, mn, o)
        elif mn == 'test' and o == ['ah', '0x44'] and self.flags and self.flags[0] == 'ucomi':
            if not self.flags or self.flags[0] != 'ucomi' or seq[i - 1][1] != 'lahf':
                fail(a, 'test ah without lahf after ucomiss')
            self.flags = ('lahf44',)
        elif mn == 'lahf':
            if not self.flags or self.flags[0] != 'ucomi':
                fail(a, 'lahf without ucomiss')
            # ah = SF ZF 0 AF 0 PF 1 CF of the compare (unordered: ZF PF CF)
            E('  eax = (eax & ~0xFF00u) | (((fa < fb || fa > fb) ? (fa < fb ? 0x03u : 0x02u) : (fa == fb ? 0x42u : 0x47u)) << 8);')
        elif mn == 'test':
            sz = self.opsize(o[0])
            lhs, rhs = self.load(a, o[0], sz), self.load(a, o[1], sz)
            ext = {1: ' res = uint32_t(int32_t(int8_t(res)));', 2: ' res = uint32_t(int32_t(int16_t(res)));', 4: ''}[sz]
            E(f'  res = uint32_t({lhs} & {rhs});{ext}')
            self.flags = ('res', 'and')
        elif mn == 'cmp':
            sz = self.opsize(o[0])
            lhs, rhs = self.load(a, o[0], sz), self.load(a, o[1], sz)
            if sz < 4:  # the narrow values, sign-extended for the signed relations
                b = 8 * sz
                E(f'  cu = {lhs}; cv = {rhs}; cl = uint32_t(int32_t(int{b}_t(cu))); cr = uint32_t(int32_t(int{b}_t(cv)));')
            else:
                E(f'  cl = {lhs}; cr = {rhs}; cu = cl; cv = cr;')
            self.flags = ('cmp', sz)
        elif mn in ('comiss', 'ucomiss'):
            E(f'  fa = F(X{xmm(o[0])}.l[0]); fb = F({self.xload(a, o[1])});')
            self.flags = ('ucomi',)
        elif mn.startswith('j'):
            self.branch(a, mn, o)
        elif mn in ('adc', 'sbb'):
            f = self.flags
            if not f or f[0] != 'cmp' and f not in (('res', 'add'), ('res', 'neg')):
                fail(a, f'{mn} after unknown carry')
            carry = {'cmp': '(cu < cv)', 'add': '(cl + cr < cl)', 'neg': '(res != 0)'}[f[0] if f[0] == 'cmp' else f[1]]
            cur, src = self.load(a, o[0]), self.load(a, o[1])
            op = '+' if mn == 'adc' else '-'
            E(f'  {{ const uint32_t r = uint32_t({cur} {op} {src} {op} uint32_t{carry}); ' + self.store(a, o[0], 'r') + ' }')
            self.flags = None
        elif mn == 'cdq':
            E('  edx = uint32_t(int32_t(eax) >> 31);')
        elif mn in ('div', 'idiv') and len(o) == 1 and self.opsize(o[0]) == 4:
            v = self.load(a, o[0])
            if mn == 'div':
                E(f'  {{ const uint64_t n = (uint64_t(edx) << 32) | eax; const uint32_t d = {v}; eax = uint32_t(n / d); edx = uint32_t(n % d); }}')
            else:
                E(f'  {{ const int64_t n = int64_t((uint64_t(edx) << 32) | eax); const int32_t d = int32_t({v}); eax = uint32_t(n / d); edx = uint32_t(n % d); }}')
            self.flags = None
        elif mn in ('mul', 'imul') and len(o) == 1 and self.opsize(o[0]) == 4:
            v = self.load(a, o[0])
            if mn == 'mul':
                E(f'  {{ const uint64_t p = uint64_t(eax) * {v}; eax = uint32_t(p); edx = uint32_t(p >> 32); }}')
            else:
                E(f'  {{ const int64_t p = int64_t(int32_t(eax)) * int32_t({v}); eax = uint32_t(p); edx = uint32_t(uint64_t(p) >> 32); }}')
            self.flags = None
        elif mn.startswith('set'):
            E('  ' + self.store(a, o[0], f'(({self.cond(a, mn)}) ? 1u : 0u)'))
        elif mn.startswith('cmov'):
            E(f'  if ({self.cond(a, mn)}) {o[0]} = {self.load(a, o[1])};')
        elif mn == 'rep' and o and re.match(r'^(movs|stos)d\b', ops_raw(o)):
            if ops_raw(o).startswith('movsd'):
                E('  for (; ecx; ecx--, esi += 4, edi += 4) Wr<uint32_t>(edi, Rd<uint32_t>(esi));  // rep movsd (DF = 0)')
            else:
                E('  for (; ecx; ecx--, edi += 4) Wr<uint32_t>(edi, eax);  // rep stosd (DF = 0)')
        elif mn == 'rep' and o and re.match(r'^(movs|stos)b\b', ops_raw(o)):
            if ops_raw(o).startswith('movsb'):
                E('  for (; ecx; ecx--, esi++, edi++) Wr<uint8_t>(edi, Rd<uint8_t>(esi));  // rep movsb (DF = 0)')
            else:
                E('  for (; ecx; ecx--, edi++) Wr<uint8_t>(edi, uint8_t(eax));  // rep stosb (DF = 0)')
        elif mn in ('prefetcht0', 'prefetchnta', 'nop'):
            pass
        elif self.sse(a, mn, o):
            pass
        elif mn.startswith('f') or mn in ('sahf',):
            self.fpu(a, mn, o, seq, i)
        else:
            fail(a, f'unsupported {mn} {" ".join(o)}')

    def sse(self, a, mn, o):
        E = self.E
        x = xmm(o[0]) if o else None
        PS = {'addps': '+', 'subps': '-', 'mulps': '*', 'divps': '/'}
        SS = {'addss': '+', 'subss': '-', 'mulss': '*', 'divss': '/'}
        if mn == 'movss':
            if x is not None and xmm(o[1]) is not None:
                E(f'  X{x}.l[0] = X{xmm(o[1])}.l[0];')
            elif x is not None:
                E(f'  X{x} = V{{{{{self.xload(a, o[1])}, 0, 0, 0}}}};')
            else:
                E('  ' + self.xstore(a, o[0], f'X{xmm(o[1])}.l[0]', False))
        elif mn in ('movaps', 'movups', 'movdqa', 'movdqu', 'movapd', 'movupd'):
            if x is not None:
                E(f'  X{x} = {self.xload(a, o[1], True)};')
            else:
                E('  ' + self.xstore(a, o[0], f'X{xmm(o[1])}', True))
        elif mn in ('movlps', 'movhps', 'movq'):
            half = 0 if mn in ('movlps', 'movq') else 2
            if x is not None and xmm(o[1]) is None:
                k, e, sz = self.mem(a, o[1].replace('qword ptr', 'dword ptr'))
                ld = (lambda d: f'FrRd<uint32_t>(Fr, {self.fidx(e) + d})') if k == 'frame' else (lambda d: f'Rd<uint32_t>({e} + {d}u)')
                if mn == 'movq':
                    E(f'  X{x} = V{{{{{ld(0)}, {ld(4)}, 0, 0}}}};')
                else:
                    E(f'  X{x}.l[{half}] = {ld(0)}; X{x}.l[{half + 1}] = {ld(4)};')
            elif x is None:
                xs = xmm(o[1])
                k, e, sz = self.mem(a, o[0].replace('qword ptr', 'dword ptr'))
                for d in (0, 1):
                    v = f'X{xs}.l[{half + d}]'
                    E('  ' + (self.written((e[0], e[1] + 4 * d)) or f'FrWr<uint32_t>(Fr, {self.fidx(e) + 4 * d}, {v});' if k == 'frame' else f'Wr<uint32_t>({e} + {4 * d}u, {v});'))
            else:
                fail(a, f'{mn} between registers')
        elif mn in SS:
            E(f'  X{x}.l[0] = U(F(X{x}.l[0]) {SS[mn]} F({self.xload(a, o[1])}));')
        elif mn in ('minss', 'maxss'):  # the second operand unless the first compares less (greater)
            s = self.xload(a, o[1])
            E(f'  {{ const uint32_t s = {s}; X{x}.l[0] = F(X{x}.l[0]) {"<" if mn == "minss" else ">"} F(s) ? X{x}.l[0] : s; }}')
        elif mn == 'sqrtss':
            E(f'  X{x}.l[0] = U(std::sqrt(F({self.xload(a, o[1])})));')
        elif mn in PS:
            E(f'  {{ const V s = {self.xload(a, o[1], True)}; for (int i = 0; i < 4; i++) X{x}.l[i] = U(F(X{x}.l[i]) {PS[mn]} F(s.l[i])); }}')
        elif mn in ('minps', 'maxps'):
            E(f'  {{ const V s = {self.xload(a, o[1], True)}; for (int i = 0; i < 4; i++) X{x}.l[i] = F(X{x}.l[i]) {"<" if mn == "minps" else ">"} F(s.l[i]) ? X{x}.l[i] : s.l[i]; }}')
        elif mn == 'sqrtps':
            E(f'  {{ const V s = {self.xload(a, o[1], True)}; for (int i = 0; i < 4; i++) X{x}.l[i] = U(std::sqrt(F(s.l[i]))); }}')
        elif mn in ('andps', 'xorps', 'orps', 'andnps', 'pand', 'por', 'pxor', 'pandn'):
            op = {'andps': '&', 'pand': '&', 'xorps': '^', 'pxor': '^', 'orps': '|', 'por': '|'}.get(mn)
            if xmm(o[1]) == x and mn in ('xorps', 'pxor'):
                E(f'  X{x} = V{{}};')
            elif op:
                E(f'  {{ const V s = {self.xload(a, o[1], True)}; for (int i = 0; i < 4; i++) X{x}.l[i] {op}= s.l[i]; }}')
            else:
                E(f'  {{ const V s = {self.xload(a, o[1], True)}; for (int i = 0; i < 4; i++) X{x}.l[i] = ~X{x}.l[i] & s.l[i]; }}')
        elif mn in ('shufps', 'pshufd'):
            k = int(o[2], 0)
            s = self.xload(a, o[1], True)
            if mn == 'shufps':
                E(f'  {{ const V d = X{x}, s = {s}; X{x} = V{{{{d.l[{k & 3}], d.l[{k >> 2 & 3}], s.l[{k >> 4 & 3}], s.l[{k >> 6 & 3}]}}}}; }}')
            else:
                E(f'  {{ const V s = {s}; X{x} = V{{{{s.l[{k & 3}], s.l[{k >> 2 & 3}], s.l[{k >> 4 & 3}], s.l[{k >> 6 & 3}]}}}}; }}')
        elif mn in ('unpcklps', 'unpckhps'):
            h = 0 if mn == 'unpcklps' else 2
            E(f'  {{ const V d = X{x}, s = {self.xload(a, o[1], True)}; X{x} = V{{{{d.l[{h}], s.l[{h}], d.l[{h + 1}], s.l[{h + 1}]}}}}; }}')
        elif mn in ('movlhps', 'movhlps'):
            xs = xmm(o[1])
            if mn == 'movlhps':
                E(f'  X{x}.l[2] = X{xs}.l[0]; X{x}.l[3] = X{xs}.l[1];')
            else:
                E(f'  X{x}.l[0] = X{xs}.l[2]; X{x}.l[1] = X{xs}.l[3];')
        elif mn == 'cvtsi2ss':
            E(f'  X{x}.l[0] = U(float(int32_t({self.load(a, o[1])})));')
        elif mn == 'cvttss2si':
            E(f'  {o[0]} = CvttF2I(F({self.xload(a, o[1])}));')
        elif mn == 'cvtdq2ps':
            E(f'  {{ const V s = {self.xload(a, o[1], True)}; for (int i = 0; i < 4; i++) X{x}.l[i] = U(float(int32_t(s.l[i]))); }}')
        elif mn == 'cvttps2dq':
            E(f'  {{ const V s = {self.xload(a, o[1], True)}; for (int i = 0; i < 4; i++) X{x}.l[i] = CvttF2I(F(s.l[i])); }}')
        elif mn == 'movd':
            if x is not None:
                E(f'  X{x} = V{{{{{self.load(a, o[1])}, 0, 0, 0}}}};')
            else:
                E('  ' + self.store(a, o[0], f'X{xmm(o[1])}.l[0]'))
        elif mn == 'movmskps':
            E(f'  {o[0]} = (X{xmm(o[1])}.l[0] >> 31) | (X{xmm(o[1])}.l[1] >> 31 << 1) | (X{xmm(o[1])}.l[2] >> 31 << 2) | (X{xmm(o[1])}.l[3] >> 31 << 3);')
        elif mn in ('movsd',) and len(o) == 2 and 'es:' not in o[0]:
            if x is not None and xmm(o[1]) is not None:
                E(f'  X{x}.l[0] = X{xmm(o[1])}.l[0]; X{x}.l[1] = X{xmm(o[1])}.l[1];')
            elif x is not None:
                E(f'  X{x} = V{{{{0, 0, 0, 0}}}}; SetD(X{x}, 0, {self.dload(a, o[1])});')
            else:
                k, e, sz = self.mem(a, o[0])
                E('  ' + (self.written(e, 8) or f'FrWr<double>(Fr, {self.fidx(e)}, GetD(X{xmm(o[1])}, 0));' if k == 'frame' else f'Wr<double>({e}, GetD(X{xmm(o[1])}, 0));'))
        elif mn in ('addsd', 'subsd', 'mulsd', 'divsd'):
            op = {'addsd': '+', 'subsd': '-', 'mulsd': '*', 'divsd': '/'}[mn]
            E(f'  SetD(X{x}, 0, GetD(X{x}, 0) {op} {self.dload(a, o[1])});')
        elif mn in ('minsd', 'maxsd'):
            E(f'  {{ const double s = {self.dload(a, o[1])}, d = GetD(X{x}, 0); SetD(X{x}, 0, d {"<" if mn == "minsd" else ">"} s ? d : s); }}')
        elif mn == 'sqrtsd':
            E(f'  SetD(X{x}, 0, std::sqrt({self.dload(a, o[1])}));')
        elif mn in ('comisd', 'ucomisd'):
            E(f'  fa = GetD(X{x}, 0); fb = {self.dload(a, o[1])};')
            self.flags = ('ucomi',)
        elif mn == 'cvtss2sd':
            E(f'  SetD(X{x}, 0, double(F({self.xload(a, o[1])})));')
        elif mn == 'cvtsd2ss':
            E(f'  X{x}.l[0] = U(float({self.dload(a, o[1])}));')
        elif mn == 'cvtps2pd':
            if xmm(o[1]) is not None:
                E(f'  {{ const V s = X{xmm(o[1])}; SetD(X{x}, 0, double(F(s.l[0]))); SetD(X{x}, 1, double(F(s.l[1]))); }}')
            else:
                k, e, sz = self.mem(a, o[1].replace('qword ptr', 'dword ptr'))
                ld = (lambda d: f'FrRd<uint32_t>(Fr, {self.fidx(e) + d})') if k == 'frame' else (lambda d: f'Rd<uint32_t>({e} + {d}u)')
                E(f'  {{ const uint32_t a0 = {ld(0)}, a1 = {ld(4)}; SetD(X{x}, 0, double(F(a0))); SetD(X{x}, 1, double(F(a1))); }}')
        elif mn == 'cvtpd2ps':
            E(f'  {{ const V s = {self.xload(a, o[1], True)}; X{x} = V{{{{U(float(GetD(s, 0))), U(float(GetD(s, 1))), 0, 0}}}}; }}')
        elif mn == 'cvtsi2sd':
            E(f'  SetD(X{x}, 0, double(int32_t({self.load(a, o[1])})));')
        elif mn == 'cvttsd2si':
            E(f'  {o[0]} = CvttD2I({self.dload(a, o[1])});')
        elif mn in ('addpd', 'subpd', 'mulpd', 'divpd'):
            op = {'addpd': '+', 'subpd': '-', 'mulpd': '*', 'divpd': '/'}[mn]
            E(f'  {{ const V s = {self.xload(a, o[1], True)}; for (int i = 0; i < 2; i++) SetD(X{x}, i, GetD(X{x}, i) {op} GetD(s, i)); }}')
        elif mn in ('andpd', 'xorpd', 'orpd'):
            op = {'andpd': '&', 'xorpd': '^', 'orpd': '|'}[mn]
            if mn == 'xorpd' and xmm(o[1]) == x:
                E(f'  X{x} = V{{}};')
            else:
                E(f'  {{ const V s = {self.xload(a, o[1], True)}; for (int i = 0; i < 4; i++) X{x}.l[i] {op}= s.l[i]; }}')
        elif mn in ('unpcklpd', 'unpckhpd'):
            h = 0 if mn == 'unpcklpd' else 2
            E(f'  {{ const V d = X{x}, s = {self.xload(a, o[1], True)}; X{x} = V{{{{d.l[{h}], d.l[{h + 1}], s.l[{h}], s.l[{h + 1}]}}}}; }}')
        elif re.match(r'^cmp(eq|lt|le|unord|neq|nlt|nle|ord)(ss|ps)$', mn):
            m = re.match(r'^cmp(eq|lt|le|unord|neq|nlt|nle|ord)(ss|ps)$', mn)
            pred = {'eq': 'x == y', 'lt': 'x < y', 'le': 'x <= y', 'unord': '(x != x || y != y)',
                    'neq': '!(x == y)', 'nlt': '!(x < y)', 'nle': '!(x <= y)', 'ord': '!(x != x || y != y)'}[m.group(1)]
            n = 1 if m.group(2) == 'ss' else 4
            src = f'V{{{{{self.xload(a, o[1])}, 0, 0, 0}}}}' if n == 1 else self.xload(a, o[1], True)
            E(f'  {{ const V s = {src}; for (int i = 0; i < {n}; i++) {{ const float x = F(X{x}.l[i]), y = F(s.l[i]); X{x}.l[i] = ({pred}) ? 0xFFFFFFFFu : 0u; }} }}')
        elif mn in ('cmpps', 'cmpss'):
            fail(a, f'{mn} with an immediate (todo)')
        else:
            return False
        return True

    def st(self, a, i):
        while i >= self.fd:
            # deeper than this code's own values: the x87 model's (the
            # caller's or a callee's, PC=24 floats), taken over at the bottom
            if self.fd >= 8:
                fail(a, 'x87 stack overflow')
            shift = ' '.join(f'T{k} = T{k - 1};' for k in range(self.fd, 0, -1))
            self.E(f'  {shift} T0 = X87PopF32(c);'.replace('   ', ' '))
            self.fd += 1
        return f'T{self.fd - 1 - i}'

    def spill_x87(self):
        # this code's values back to the model, bottom first
        for k in range(self.fd):
            self.E(f'  X87PushF32(c, T{k});')
        self.fd = 0

    def push(self, a, v):
        self.after_call = False
        if self.fd >= 8:
            fail(a, 'x87 stack overflow')
        self.E(f'  T{self.fd} = {v};')
        self.fd += 1

    def fpu(self, a, mn, o, seq, i):
        E = self.E
        # the deepest register the instruction names, taken over first: a
        # take-over renumbers this code's values
        deep = [int(m) for op in o for m in re.findall(r'^st\((\d)\)$', op)]
        if mn in ('fcompp', 'fucompp') or (mn in ('faddp', 'fsubp', 'fsubrp', 'fmulp', 'fdivp', 'fdivrp', 'fxch', 'fcom', 'fcomp', 'fucom', 'fucomp') and not o):
            deep.append(1)
        if deep or o and o[0] == 'st' or mn in ('fst', 'fstp', 'fistp', 'fist', 'fchs', 'fabs', 'fsqrt') or mn.startswith('fcom') or mn.startswith('fucom'):
            if mn not in ('fld',) or deep:
                self.st(a, max(deep + [0]))
        def sti(op):
            m = re.match(r'^st\((\d)\)$', op)
            return 0 if op == 'st' else int(m.group(1)) if m else None
        def m32(op):
            if not op.startswith('dword ptr'):
                fail(a, f'x87 operand {op}')
            return self.load(a, op)
        ARITH = {'fadd': '+', 'fsub': '-', 'fmul': '*', 'fdiv': '/', 'fsubr': '-', 'fdivr': '/'}
        base = mn[:-1] if mn.endswith('p') and mn[:-1] in ARITH else mn
        if mn == 'fld':
            if sti(o[0]) is not None:
                self.push(a, self.st(a, sti(o[0])))
            else:
                self.push(a, m32(o[0]))
        elif mn in ('fldz', 'fld1'):
            self.push(a, '0x0u' if mn == 'fldz' else '0x3f800000u')
        elif mn == 'fild':
            self.x87 = True
            sz = self.opsize(o[0])
            if sz == 4:
                self.push(a, f'U(float(int32_t({self.load(a, o[0])})))')
            elif sz == 2:
                self.push(a, f'U(float(int16_t({self.load(a, o[0])})))')
            else:
                fail(a, 'fild size')
        elif mn in ('fst', 'fstp'):
            if sti(o[0]) is not None:
                E(f'  {self.st(a, sti(o[0]))} = {self.st(a, 0)};')
            else:
                E('  ' + self.store(a, o[0], self.st(a, 0)))
            if mn == 'fstp':
                self.fd -= 1
        elif mn == 'fistp' or mn == 'fist':
            self.x87 = True
            if self.opsize(o[0]) != 4:
                fail(a, 'fistp size')
            E('  ' + self.store(a, o[0], f'FistF(c, F({self.st(a, 0)}))'))
            if mn == 'fistp':
                self.fd -= 1
        elif mn == 'fxch':
            j = sti(o[0]) if o else 1
            E(f'  std::swap({self.st(a, 0)}, {self.st(a, j)});')
        elif base in ARITH:
            self.x87 = True
            if self.cw_changed:  # floats are right only at the entry's PC=24/nearest: logged if not
                E(f'  CheckCw(c, {a:#x}u);')
            op = ARITH[base]
            rev = base in ('fsubr', 'fdivr')
            pop = mn != base
            if len(o) == 1 and sti(o[0]) is None:  # st0 op= m32
                d, s = self.st(a, 0), f'{m32(o[0])}'
            elif len(o) == 0:  # faddp: st(1) op= st0, pop
                d, s = self.st(a, 1), self.st(a, 0)
            elif len(o) == 2 and o[1] == 'st':  # st(i) op= st0
                d, s = self.st(a, sti(o[0])), self.st(a, 0)
            elif len(o) == 2 and o[0] == 'st':  # st0 op= st(i)
                d, s = self.st(a, 0), self.st(a, sti(o[1]))
            else:
                fail(a, f'x87 form {mn} {o}')
            if rev:
                E(f'  {d} = U(F({s}) {op} F({d}));')
            else:
                E(f'  {d} = U(F({d}) {op} F({s}));')
            if pop:
                self.fd -= 1
        elif mn in ('fchs', 'fabs'):
            E(f'  {self.st(a, 0)} {"^= 0x80000000u" if mn == "fchs" else "&= 0x7FFFFFFFu"};')
        elif mn == 'fsqrt':
            self.x87 = True
            E(f'  {self.st(a, 0)} = U(std::sqrt(F({self.st(a, 0)})));')
        elif mn in ('fcom', 'fcomp', 'fcompp', 'fucom', 'fucomp', 'fucompp'):
            if not o:
                s = self.st(a, 1)
            elif sti(o[0]) is not None:
                s = self.st(a, sti(o[0]))
            else:
                s = m32(o[0])
            E(f'  fa = F({self.st(a, 0)}); fb = F({s});')
            self.fd -= mn.count('p')
            self.flags = ('fcom',)
        elif mn in ('fcomi', 'fcomip', 'fucomi', 'fucomip', 'fcompi', 'fucompi'):
            E(f'  fa = F({self.st(a, 0)}); fb = F({self.st(a, sti(o[1]))});')
            self.fd -= mn.endswith('p') or mn.endswith('pi')
            self.flags = ('ucomi',)
        elif mn == 'fnstcw':
            E('  ' + self.store(a, o[0], 'c.fpu.cw'))
            self.saved_cw = o[0]
        elif mn == 'fldcw':
            E(f'  ::recomp::x87::SetControlWord(c.fpu, uint16_t({self.load(a, o[0])}));')
            self.cw_changed = True
        elif mn == 'fnstsw':
            if o != ['ax'] or not self.flags or self.flags[0] != 'fcom':
                fail(a, 'fnstsw other than ax after a compare')
            # C0 0x100, C2 0x400, C3 0x4000 (unordered: all three); TOP
            E(f'  eax = (eax & ~0xFFFFu) | FcomSw(fa, fb) | (((c.fpu.top - {self.fd}u) & 7u) << 11);')
            self.flags = ('fsw',)
        elif mn == 'sahf':
            if not self.flags or self.flags[0] != 'fsw':
                fail(a, 'sahf not after fnstsw')
            self.flags = ('ucomi',)  # ZF = C3, PF = C2, CF = C0
        else:
            fail(a, f'unsupported {mn} {" ".join(o)}')

    def alu(self, a, mn, o):
        E = self.E
        if o[0] == 'esp':
            if not IMM.match(o[1]) or mn not in ('add', 'sub', 'and'):
                fail(a, 'esp arithmetic')
            if mn == 'and':
                if local:
                    if self.ftag != 'E' or self.ebp_sd is None:
                        fail(a, 'and esp in local mode without an ebp frame')
                    tag = f'A{a:X}'
                    E(f'  [[maybe_unused]] const uint32_t fb{tag} = uint32_t(fbE + {self.sd & 0xFFFFFFFF:#x}u) & {imm(o[1])};')
                    self.ftag, self.sd = tag, 0
                    return
                self.base, self.sd = f'aligned@{a:X}', 0
            else:
                self.sd += (1 if mn == 'add' else -1) * int(o[1], 0)
            if not local:
                E(f'  esp = uint32_t(esp {dict(add="+", sub="-", **{"and": "&"})[mn]} {imm(o[1])});')
            return
        sz = self.opsize(o[0])
        if sz == 2 and mn not in ('add', 'sub', 'and', 'or', 'xor', 'inc', 'dec'):
            fail(a, f'16-bit {mn}')
        if mn == 'xor' and len(o) == 2 and o[0] == o[1]:
            E('  ' + self.store(a, o[0], '0u') + ' res = 0;')
            self.flags = ('res', 'xor')
            return
        cur = self.load(a, o[0], sz)
        src = self.load(a, o[1], sz) if len(o) > 1 else None
        if mn == 'imul':
            if len(o) == 3:
                expr = f'uint32_t(int32_t({src}) * int32_t({imm(o[2])}))'
            elif len(o) == 2:
                expr = f'uint32_t(int32_t({cur}) * int32_t({src}))'
            else:
                fail(a, 'one-operand imul')
            E('  ' + self.store(a, o[0], expr))
            self.flags = None
            return
        if mn in ('shl', 'shr', 'sar') and len(o) == 2 and o[1] == 'cl' and sz == 4:
            op = {'shl': f'({cur} << (ecx & 31u))', 'shr': f'({cur} >> (ecx & 31u))', 'sar': f'uint32_t(int32_t({cur}) >> (ecx & 31u))'}[mn]
            E('  ' + self.store(a, o[0], f'uint32_t({op})'))
            self.flags = None  # a zero count leaves them: not modelled
            return
        if mn in ('shl', 'shr', 'sar'):
            if len(o) == 1:
                o = o + ['1']
            if not IMM.match(o[1]) or sz != 4:
                fail(a, 'shift form')
            k = int(o[1], 0) & 31
            expr = {'shl': f'({cur} << {k})', 'shr': f'({cur} >> {k})', 'sar': f'uint32_t(int32_t({cur}) >> {k})'}[mn]
        elif mn in ('inc', 'dec'):
            expr = f'({cur} {"+" if mn == "inc" else "-"} 1u)'
        elif mn == 'neg':
            expr = f'(0u - {cur})'
        elif mn == 'not':
            expr = f'(~{cur})'
        elif mn == 'sub':
            if sz != 4:
                b = 8 * sz
                E(f'  {{ cu = {cur}; cv = {src}; cl = uint32_t(int32_t(int{b}_t(cu))); cr = uint32_t(int32_t(int{b}_t(cv))); const uint32_t r = uint32_t(cu - cv); ' + self.store(a, o[0], 'r') + ' res = r; }')
                self.flags = ('cmp', sz)
                return
            E(f'  {{ cl = {cur}; cr = {src}; cu = cl; cv = cr; const uint32_t r = uint32_t(cl - cr); ' + self.store(a, o[0], 'r') + ' res = r; }')
            self.flags = ('cmp', 4)
            return
        else:
            expr = f'({cur} {dict(add="+", **{"and": "&", "or": "|", "xor": "^"})[mn]} {src})'
            if mn == 'add' and sz == 4:
                E(f'  cl = {cur}; cr = {src};')
                expr = '(cl + cr)'
        ext = {1: ' res = uint32_t(int32_t(int8_t(res)));', 2: ' res = uint32_t(int32_t(int16_t(res)));', 4: ''}[sz]
        if mn == 'not':  # the flags stay
            E(f'  {{ const uint32_t r = uint32_t({expr}); ' + self.store(a, o[0], 'r') + ' }')
        else:
            E(f'  {{ const uint32_t r = uint32_t({expr}); ' + self.store(a, o[0], 'r') + f' res = r;{ext} }}')
        if mn != 'not':  # not leaves the flags
            self.flags = ('res', mn)

    def cond(self, a, mn):
        """The condition of jcc/setcc/cmovcc `mn` from the flags in force."""
        cc = 'j' + re.sub(r'^(j|set|cmov)', '', mn)
        cc = {'jz': 'je', 'jnz': 'jne', 'jc': 'jb', 'jnc': 'jae', 'jnae': 'jb', 'jnb': 'jae', 'jna': 'jbe', 'jnbe': 'ja',
              'jnge': 'jl', 'jnl': 'jge', 'jng': 'jle', 'jnle': 'jg', 'jpe': 'jp', 'jpo': 'jnp'}.get(cc, cc)
        f = self.flags
        if f is None:
            fail(a, f'{mn} without known flags')
        cond = None
        if f[0] == 'ucomi':  # unordered: ZF = PF = CF = 1
            cond = {'ja': 'fa > fb', 'jbe': '!(fa > fb)', 'jae': 'fa >= fb', 'jb': '!(fa >= fb)',
                    'je': '!(fa < fb || fa > fb)', 'jne': '(fa < fb || fa > fb)',
                    'jp': '(fa != fa || fb != fb)', 'jnp': '!(fa != fa || fb != fb)'}.get(cc)
        elif f[0] == 'lahf44':
            # PF of (ZF, PF) of the compare: even with both set (unordered)
            # or both clear (ordered, not equal)
            cond = {'jp': '!(fa == fb)', 'jnp': 'fa == fb'}.get(cc)
        elif f[0] == 'res':
            cond = {'js': 'int32_t(res) < 0', 'jns': 'int32_t(res) >= 0', 'je': 'res == 0', 'jne': 'res != 0',
                    'jp': '__builtin_parity(res & 0xFFu) == 0', 'jnp': '__builtin_parity(res & 0xFFu) != 0'}.get(cc)
            if cc in ('jl', 'jge', 'jg', 'jle', 'jb', 'jae', 'ja', 'jbe'):
                if f[1] not in ('and', 'or', 'xor'):  # OF = CF = 0 only after logic
                    fail(a, f'{mn} after {f[1]}')
                cond = {'jl': 'int32_t(res) < 0', 'jge': 'int32_t(res) >= 0', 'jg': 'int32_t(res) > 0',
                        'jle': 'int32_t(res) <= 0', 'jb': 'false', 'jae': 'true', 'ja': 'res != 0', 'jbe': 'res == 0'}[cc]
        elif f[0] == 'cmp':
            b = 8 * f[1]
            cond = {'je': 'cu == cv', 'jne': 'cu != cv', 'jl': 'int32_t(cl) < int32_t(cr)', 'jge': 'int32_t(cl) >= int32_t(cr)',
                    'jle': 'int32_t(cl) <= int32_t(cr)', 'jg': 'int32_t(cl) > int32_t(cr)',
                    'jb': 'cu < cv', 'jae': 'cu >= cv', 'jbe': 'cu <= cv', 'ja': 'cu > cv',
                    'js': f'int{b}_t(cu - cv) < 0', 'jns': f'int{b}_t(cu - cv) >= 0'}.get(cc)
        if cond is None:
            fail(a, f'{mn} after {f[0]}')
        return cond

    def branch(self, a, mn, o):
        if a in tails:  # a tail call: the callee returns for this function
            self.spill_x87()
            t = int(o[0], 16)
            self.decl.add(t)
            if local:
                fail(a, 'tail call in local mode')
            self.E('  c.eax = eax; c.ecx = ecx; c.edx = edx; c.ebx = ebx; c.esp = esp; c.ebp = ebp; c.esi = esi; c.edi = edi;')
            self.E(f'  XmmOut(c, X0, X1, X2, X3, X4, X5, X6, X7); CALL_TAIL(f_{t:08X}, {t:#x}u); return;')
            return
        if not IMM.match(o[0]):  # a jump table
            if mn != 'jmp':
                fail(a, 'indirect jcc')
            m = re.match(r'^dword ptr', o[0])
            v = self.load(a, o[0]) if m else o[0]
            self.E(f'  switch ({v}) {{')
            for t in sorted(set(JT[a])):
                self.jump(a, t)
                self.E(f'    case {t:#x}u: goto L_{t:08X};')
            self.E('    default: __builtin_trap();  // not in the table Ghidra found')
            self.E('  }')
            return
        t = int(o[0], 16)
        self.jump(a, t)
        if mn == 'jmp':
            self.E(f'  goto L_{t:08X};')
            return
        cond = self.cond(a, mn)
        self.E(f'  if ({cond}) goto L_{t:08X};')


# Labels' esp and flags from every path: tolerant passes until the flags'
# kinds stop growing, then the strict one.
w = Walk(tolerant=True)
w.run()
for _ in range(8):
    w1 = Walk(hint=w.at, tolerant=True)
    w1.hint_fk = {k: v | w.hint_fk.get(k, set()) for k, v in w.fk.items()}
    w1.run()
    done = all(w1.fk.get(k, set()) <= w1.hint_fk.get(k, set()) for k in w1.fk)
    w = w1
    if done:
        break
w2 = Walk(hint=w.at)
w2.hint_fk = w.hint_fk
w2.run()
w = w2
if '--ret-fd' in sys.argv:
    print(max(w.ret_fd) if w.ret_fd else 0)
    sys.exit(0)
nbytes = 0
if local and w.fr:
    layout = {}
    for tag in sorted({t for t, o in w.fr}):
        offs = [o for t, o in w.fr if t == tag]
        layout[tag] = (nbytes, min(offs))
        nbytes += max(offs) - min(offs) + 16
    w3 = Walk(layout, hint=w.at)
    w3.fw_all = w.fw
    w3.hint_fk = w.fk
    w3.run()
    w = w3

print(f'// {name}: GTAIV.exe {start:08X}-{end - 1:08X} by tools/x86n.py, {"frame local" if local else "frame on the guest stack"}'
      + ('; x87 as floats: run only under PC=24, RC=nearest.' if w.x87 else '.'))
print(f'[[maybe_unused]] constexpr bool {name}_x87 = {"true" if w.x87 else "false"};')
print(f'// x87-result callees: {",".join(f"{t:08X}" for t in sorted(w.x87ret))}')
print(f'[[maybe_unused]] static const uint32_t {name}_x87ret[] = {{{"".join(f"{t:#x}u, " for t in sorted(w.x87ret))}0}};')
for t in sorted(w.decl):
    print(f'void f_{t:08X}(Ctx& c);')
print(f'[[maybe_unused]] static void {name}(Ctx& c) {{')
print('  using namespace ::recomp::gen::native;')
if local:
    print('  uint32_t eax = c.eax, ecx = c.ecx, edx = c.edx, ebx = c.ebx, esi = c.esi, edi = c.edi, ebp;')
    if nbytes:
        print(f'  alignas(16) uint8_t Fr[{nbytes}];  // the frame')
else:
    print('  uint32_t eax = c.eax, ecx = c.ecx, edx = c.edx, ebx = c.ebx, esp = c.esp, ebp = c.ebp, esi = c.esi, edi = c.edi;')
print('  V X0, X1, X2, X3, X4, X5, X6, X7;')
print('  [[maybe_unused]] uint32_t T0, T1, T2, T3, T4, T5, T6, T7;  // x87 values (PC=24: floats)')
print('  XmmIn(c, X0, X1, X2, X3, X4, X5, X6, X7);')
print('  [[maybe_unused]] uint32_t res = 0, cl = 0, cr = 0, cu = 0, cv = 0, calltarget = 0;')
print('  [[maybe_unused]] double fa = 0, fb = 0;  // a compare\'s operands')
for l in w.lines:
    print(l)
print('}')

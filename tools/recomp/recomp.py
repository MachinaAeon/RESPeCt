#!/usr/bin/env python3
"""Static recompiler: PS1 (MIPS R3000A) machine code -> C.

Every function of every module becomes a C function that does what the
original instructions did, on a register file in the runtime (port/).
Control flow inside a function becomes goto; calls become C calls; returns
(jr ra) become return. Memory goes through the runtime's load/store helpers,
COP2 through its GTE.

  direct call into the same module or KERNEL   -> C call
  call into an overlay slot from elsewhere     -> rt_call(addr) (picks the
                                                  overlay loaded right now)
  jalr / jr to a function pointer              -> rt_call(addr)
  jr through a jump table                      -> switch + goto
  backward branch                              -> RT_POLL() (lets the
                                                  runtime run interrupts, so
                                                  polling loops can finish)

Output: port/gen/<module>.c and port/gen/funcs.c (address tables).

Usage: recomp.py [module ...]     (default: all)
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import modules as M  # noqa: E402

OUT = os.path.join(M.ROOT, "port", "gen")
R = ["zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
     "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"]


def s16(x):
    return x - 0x10000 if x & 0x8000 else x


class Insn:
    __slots__ = ("pc", "w", "op", "rs", "rt", "rd", "sa", "fn", "imm", "uimm", "target", "kind")

    def __init__(self, pc, w):
        self.pc, self.w = pc, w
        self.op = w >> 26
        self.rs = (w >> 21) & 31
        self.rt = (w >> 16) & 31
        self.rd = (w >> 11) & 31
        self.sa = (w >> 6) & 31
        self.fn = w & 63
        self.uimm = w & 0xFFFF
        self.imm = s16(self.uimm)
        self.target = None
        self.kind = classify(self)


SPECIAL = {0: "sll", 2: "srl", 3: "sra", 4: "sllv", 6: "srlv", 7: "srav", 8: "jr", 9: "jalr",
           12: "syscall", 13: "break", 16: "mfhi", 17: "mthi", 18: "mflo", 19: "mtlo",
           24: "mult", 25: "multu", 26: "div", 27: "divu", 32: "add", 33: "addu", 34: "sub",
           35: "subu", 36: "and", 37: "or", 38: "xor", 39: "nor", 42: "slt", 43: "sltu",
           # MIPS II traps: not on the R3000, but clang emits teq for
           # divide-by-zero checks in our C mods; kept as checks that log.
           48: "tge", 49: "tgeu", 50: "tlt", 51: "tltu", 52: "teq", 54: "tne"}
PRIMARY = {2: "j", 3: "jal", 4: "beq", 5: "bne", 6: "blez", 7: "bgtz", 8: "addi", 9: "addiu",
           10: "slti", 11: "sltiu", 12: "andi", 13: "ori", 14: "xori", 15: "lui",
           32: "lb", 33: "lh", 34: "lwl", 35: "lw", 36: "lbu", 37: "lhu", 38: "lwr",
           40: "sb", 41: "sh", 42: "swl", 43: "sw", 46: "swr", 50: "lwc2", 58: "swc2"}


def classify(i):
    if i.op == 0:
        return SPECIAL.get(i.fn, "?")
    if i.op == 1:
        return {0: "bltz", 1: "bgez", 16: "bltzal", 17: "bgezal"}.get(i.rt, "?")
    if i.op == 16:                                   # COP0
        if i.rs == 0:
            return "mfc0"
        if i.rs == 4:
            return "mtc0"
        if i.rs == 16 and i.fn == 16:
            return "rfe"
        return "?"
    if i.op == 18:                                   # COP2 (GTE)
        if i.rs & 16:
            return "cop2"
        return {0: "mfc2", 2: "cfc2", 4: "mtc2", 6: "ctc2"}.get(i.rs, "?")
    return PRIMARY.get(i.op, "?")


BRANCHES = {"beq", "bne", "blez", "bgtz", "bltz", "bgez", "bltzal", "bgezal"}


def decode(mod, pc):
    if not mod.has(pc):
        return None
    i = Insn(pc, mod.word(pc))
    if i.kind in BRANCHES:
        i.target = (pc + 4 + (i.imm << 2)) & 0xFFFFFFFF
    elif i.kind in ("j", "jal"):
        i.target = ((pc + 4) & 0xF0000000) | ((i.w & 0x03FFFFFF) << 2)
    return i


# ---------------------------------------------------------------- jump tables

WRITES_RT = {"addiu", "addi", "slti", "sltiu", "andi", "ori", "xori", "lui",
             "lb", "lbu", "lh", "lhu", "lw", "lwl", "lwr", "mfc0", "mfc2", "cfc2"}
WRITES_RD = {"sll", "srl", "sra", "sllv", "srlv", "srav", "mfhi", "mflo", "add", "addu",
             "sub", "subu", "and", "or", "xor", "nor", "slt", "sltu", "jalr"}


def writes(i, reg):
    if i.kind in WRITES_RT:
        return i.rt == reg
    if i.kind in WRITES_RD:
        return i.rd == reg
    return False


def find_def(mod, pc, reg, limit=300):
    """Nearest instruction before pc (in address order) that writes reg."""
    pc -= 4
    for _ in range(limit):
        i = decode(mod, pc)
        if i is None:
            return None
        if writes(i, reg):
            return i
        pc -= 4
    return None


def const_value(mod, pc, reg, depth=0):
    """Value of reg at pc if it's built from lui/addiu/ori, else None."""
    if reg == 0:
        return 0
    if depth > 6:
        return None
    d = find_def(mod, pc, reg)
    if d is None:
        return None
    if d.kind == "lui":
        return d.uimm << 16
    if d.kind == "addiu":
        b = const_value(mod, d.pc, d.rs, depth + 1)
        return None if b is None else (b + d.imm) & 0xFFFFFFFF
    if d.kind == "ori":
        b = const_value(mod, d.pc, d.rs, depth + 1)
        return None if b is None else b | d.uimm
    if d.kind == "addu" and (d.rs == 0 or d.rt == 0):
        return const_value(mod, d.pc, d.rs or d.rt, depth + 1)
    return None


def find_jump_table(mod, jr):
    """Targets of `jr reg` through a jump table, or None.

    Pattern: lw reg, off(p) with p = table_base + index*4, where table_base
    is a lui/addiu constant (possibly kept in a saved register), and an
    sltiu bound check before it."""
    lw = find_def(mod, jr.pc, jr.rs, limit=16)
    if lw is None or lw.kind != "lw":
        return None
    add = find_def(mod, lw.pc, lw.rs, limit=16)
    table = None
    if add is not None and add.kind == "addu":
        for base, idx in ((add.rs, add.rt), (add.rt, add.rs)):
            di = find_def(mod, add.pc, idx, limit=16)
            if di is not None and di.kind == "sll" and di.sa == 2:
                b = const_value(mod, add.pc, base)
                if b is not None:
                    table = (b + lw.imm) & 0xFFFFFFFF
                    break
    if table is None:
        return None
    bound = None
    pc = lw.pc - 4
    for _ in range(40):
        i = decode(mod, pc)
        if i is None:
            break
        if i.kind == "sltiu":
            bound = i.uimm
            break
        pc -= 4
    if not (mod.vram <= table < mod.end):
        return None
    out = []
    n = 0
    while bound is None or n < bound:
        if table + 4 * n + 4 > mod.end:
            break
        x = mod.word(table + 4 * n)
        if not mod.has(x) or x & 3:
            if bound is None:
                break
            return None
        out.append(x)
        n += 1
        if n > 1024:
            return None
    return out or None


# ------------------------------------------------------------- discovery

class Func:
    def __init__(self, mod, entry):
        self.mod = mod
        self.entry = entry
        self.insns = {}          # pc -> Insn
        self.labels = set()      # branch targets
        self.tables = {}         # jr pc -> [targets]
        self.calls = set()       # direct call targets
        self.bad = None          # reason this isn't valid code


def walk(mod, entry, entries):
    """Collect the instructions of the function at entry."""
    f = Func(mod, entry)
    work = [entry]
    seen = set()
    while work:
        pc = work.pop()
        while pc not in seen:
            i = decode(mod, pc)
            if i is None:
                f.bad = f"runs out of module at {pc:#x}"
                return f
            if i.kind == "?":
                f.bad = f"invalid instruction {i.w:08x} at {pc:#x}"
                return f
            seen.add(pc)
            f.insns[pc] = i
            k = i.kind
            if k in BRANCHES or k in ("j", "jal", "jr", "jalr"):
                d = decode(mod, pc + 4)
                if d is None or d.kind == "?":
                    f.bad = f"bad delay slot at {pc + 4:#x}"
                    return f
                seen.add(pc + 4)
                f.insns[pc + 4] = d
            if k in BRANCHES:
                f.labels.add(i.target)
                work.append(i.target)
                if k in ("bltzal", "bgezal"):
                    f.calls.add(i.target)
                pc += 8
                continue
            if k == "jal":
                f.calls.add(i.target)
                pc += 8
                continue
            if k == "jalr":
                pc += 8
                continue
            if k == "j":
                if i.target in entries and i.target != entry or not mod.has(i.target):
                    f.calls.add(i.target)       # tail call
                else:
                    f.labels.add(i.target)
                    work.append(i.target)
                break
            if k == "jr":
                if i.rs != 31:
                    t = find_jump_table(mod, i)
                    if t:
                        f.tables[pc] = t
                        for x in t:
                            f.labels.add(x)
                            work.append(x)
                break
            if k == "break" or k == "rfe":
                pc += 4
                continue
            pc += 4
    return f


def is_prologue(mod, addr):
    if not mod.has(addr) or addr & 3:
        return False
    w = mod.word(addr)
    return w >> 16 == 0x27BD and w & 0x8000      # addiu sp, sp, -N


def is_boundary(mod, addr):
    """addr follows a `jr ra` and its delay slot: a function can start here."""
    if not mod.has(addr) or addr & 3 or not mod.has(addr - 8):
        return False
    return mod.word(addr - 8) == 0x03E00008 and mod.word(addr) != 0


def pointer_seeds(mod):
    """Function pointers: words anywhere in the module, and lui/addiu (or
    lui/ori) constants in its code, that point at a plausible function start
    (a stack-frame setup, or right after another function's return)."""
    out = set()
    img = mod.image
    for o in range(0, len(img) - 3, 4):
        v = struct.unpack_from("<I", img, o)[0]
        if is_prologue(mod, v) or is_boundary(mod, v):
            out.add(v)
    # lui rX, hi ... addiu/ori rY, rX, lo within a few instructions
    lo, hi = mod.code
    for pc in range(lo, hi - 4, 4):
        w = mod.word(pc)
        if w >> 26 != 15:
            continue
        reg = (w >> 16) & 31
        upper = (w & 0xFFFF) << 16
        for k in range(1, 8):
            if not mod.has(pc + 4 * k):
                break
            x = mod.word(pc + 4 * k)
            op, rs = x >> 26, (x >> 21) & 31
            if rs == reg and op in (9, 13):
                v = (upper + s16(x & 0xFFFF)) & 0xFFFFFFFF if op == 9 else upper | (x & 0xFFFF)
                if is_prologue(mod, v) or is_boundary(mod, v):
                    out.add(v)
                break
    return out


def discover(mod):
    entries = {e for e in mod.seeds if mod.has(e)} | pointer_seeds(mod)
    funcs = {}
    # Fixpoint: direct call targets inside the module become functions, and
    # a stack-frame setup right after a function's last instruction starts
    # another one (functions only reached through pointers).
    while True:
        new = set()
        for e in sorted(entries):
            if e in funcs:
                continue
            f = walk(mod, e, entries)
            funcs[e] = f
            if f.bad:
                continue
            for c in f.calls:
                if mod.has(c) and c not in entries:
                    new.add(c)
            nxt = max(f.insns) + 4
            for _ in range(4):               # skip alignment nops
                if is_prologue(mod, nxt) and nxt not in entries:
                    new.add(nxt)
                    break
                if not mod.has(nxt) or mod.word(nxt) != 0:
                    break
                nxt += 4
        if not new:
            break
        entries |= new
    # Re-walk everything with the final entry set (j vs tail call depends on it).
    out = {}
    for e in sorted(entries):
        out[e] = walk(mod, e, entries)
    return out


# ------------------------------------------------------------------ emit C

def fname(mod, addr):
    n = NATIVE.get((mod.name, addr))
    if n:
        return f"n_{n.lstrip('~')}"
    return f"f_{mod.name}_{addr:08X}"


def wrapped(mod, addr):
    """A native wrapper ("~name" in native.txt): calls go to n_name, which
    calls the translated original, still emitted as f_<module>_<addr>."""
    return NATIVE.get((mod.name, addr), "").startswith("~")


def tname(mod, addr):
    """The translation of a function. It is emitted for natives too (as
    f_<module>_<addr>): save states continue in it (see RtFunc.resume)."""
    return f"f_{mod.name}_{addr:08X}"


class Emitter:
    def __init__(self, mod, funcs, kernel_funcs, all_mods):
        self.mod = mod
        self.funcs = funcs
        self.kernel_funcs = kernel_funcs
        self.all_mods = all_mods
        self.unresolved = []

    def call(self, target):
        """C statement calling target."""
        if self.mod.has(target) and target in self.funcs and not self.funcs[target].bad:
            return f"{fname(self.mod, target)}();"
        if self.mod.name != "kernel" and target in self.kernel_funcs and not self.kernel_funcs[target].bad:
            return f"{fname(M_KERNEL, target)}();"
        return f"rt_call(0x{target:08X}u);"

    def emit_func(self, f):
        L = []
        w = L.append
        name = tname(self.mod, f.entry)
        w(f"void {name}(void) {{")
        self.resume = []           # (key, label): where a saved game can continue
        w("    RESUME_SWITCH_PLACEHOLDER")
        hook = HOOKS.get((self.mod.name, f.entry))
        if hook:
            w(f"    {{ extern void {hook}(void); {hook}(); }}")
        pcs = sorted(f.insns)
        delay = set()
        for pc in pcs:
            k = f.insns[pc].kind
            if k in BRANCHES or k in ("j", "jal", "jr", "jalr"):
                delay.add(pc + 4)
        if f.entry != pcs[0]:
            w(f"    goto L_{f.entry:08X};")
        prev_end = None
        for pc in pcs:
            if pc in delay:
                continue
            i = f.insns[pc]
            if prev_end is not None and prev_end != pc:
                w(f"    goto L_{prev_end:08X};")   # fall through over a gap
            needs_label = pc in f.labels or pc == f.entry or (prev_end is not None and prev_end != pc)
            if needs_label or True:
                w(f"L_{pc:08X}:;")
            k = i.kind
            if k in BRANCHES or k in ("j", "jal", "jr", "jalr"):
                d = f.insns[pc + 4]
                w(self.control(f, i, d))
                if k in ("j", "jr"):
                    prev_end = None
                else:
                    prev_end = pc + 8
            else:
                w("    " + self.op(i))
                prev_end = pc + 4
        if prev_end is not None:
            # Falls off the end into the next function (no return): continue there.
            w(f"    {self.call(prev_end)} return;")
        # Branches straight into another branch's delay slot: run that one
        # instruction, then carry on after it (the branch itself is skipped).
        for pc in sorted(delay & f.labels):
            w(f"L_{pc:08X}:;")
            w("    " + self.op(f.insns[pc]))
            if pc + 4 in f.insns and pc + 4 not in delay:
                w(f"    goto L_{pc + 4:08X};")
            else:
                w(f"    {self.call(pc + 4)} return;")
        w("}")
        # Saved games continue at a call's return address or just after a
        # poll (see port/src/savestate.c): a switch over those, only taken
        # while resuming.
        body = "\n".join(L)
        self.resume = [(k, lab) for k, lab in set(self.resume) if f"{lab}:;" in body]
        if self.resume:
            cases = " ".join(f"case 0x{k:08X}u: goto {lab};" for k, lab in sorted(self.resume))
            sw = (f"if (__builtin_expect(g_resume_pc != 0, 0)) {{ uint32_t r_ = g_resume_pc; g_resume_pc = 0; "
                  f"switch (r_) {{ {cases} default: g_resume_pc = r_; g_resume_miss = 1; return; }} }}")
        else:
            sw = "if (__builtin_expect(g_resume_pc != 0, 0)) { g_resume_miss = 1; return; }"
        L[1] = "    " + sw
        # Labels for every pc: unused ones are fine; silence warnings in the build.
        return "\n".join(L)

    def control(self, f, i, d):
        k = i.kind
        ds = self.op(d)
        rs, rt = f"R[{i.rs}]", f"R[{i.rt}]"
        back = i.target is not None and i.target <= i.pc
        poll = ""
        if back:
            poll = f" RT_POLL_AT(0x{i.pc:08X}u); P_{i.pc:08X}:;"
            self.resume.append((i.pc | 1, f"P_{i.pc:08X}"))
        if k in BRANCHES:
            cond = {"beq": f"{rs} == {rt}", "bne": f"{rs} != {rt}",
                    "blez": f"(int32_t){rs} <= 0", "bgtz": f"(int32_t){rs} > 0",
                    "bltz": f"(int32_t){rs} < 0", "bgez": f"(int32_t){rs} >= 0",
                    "bltzal": f"(int32_t){rs} < 0", "bgezal": f"(int32_t){rs} >= 0"}[k]
            if k in ("bltzal", "bgezal"):
                if i.pc + 8 in f.insns: self.resume.append((i.pc + 8, f"L_{i.pc + 8:08X}"))
                return (f"    {{ int c_ = {cond}; R[31] = 0x{i.pc + 8:08X}u; {ds} "
                        f"if (c_) {{ {self.call(i.target)} }} }}")
            if i.rs == i.rt and k == "beq":
                return f"    {{ {ds}{poll} goto L_{i.target:08X}; }}"
            return f"    {{ int c_ = {cond}; {ds} if (c_) {{{poll} goto L_{i.target:08X}; }} }}"
        if k == "j":
            if i.target in f.labels and self.mod.has(i.target) and i.target in f.insns:
                return f"    {{ {ds}{poll} goto L_{i.target:08X}; }}"
            return f"    {{ {ds} {self.call(i.target)} return; }}"
        if k == "jal":
            if i.pc + 8 in f.insns: self.resume.append((i.pc + 8, f"L_{i.pc + 8:08X}"))
            return f"    {{ {ds} R[31] = 0x{i.pc + 8:08X}u; {self.call(i.target)} }}"
        if k == "jalr":
            if i.pc + 8 in f.insns: self.resume.append((i.pc + 8, f"L_{i.pc + 8:08X}"))
            return (f"    {{ uint32_t t_ = {rs}; {ds} R[{i.rd}] = 0x{i.pc + 8:08X}u; "
                    f"rt_call(t_); }}").replace("R[0] = ", "(void)")
        if k == "jr":
            if i.rs == 31:
                return f"    {{ {ds} return; }}"
            if i.pc in f.tables:
                cases = " ".join(f"case 0x{t:08X}u: goto L_{t:08X};" for t in sorted(set(f.tables[i.pc])))
                return (f"    {{ uint32_t t_ = {rs}; {ds} RT_POLL_NOSAVE(0x{i.pc:08X}u); switch (t_) {{ {cases} "
                        f"default: rt_badjump(0x{i.pc:08X}u, t_); return; }} }}")
            self.unresolved.append(i.pc)
            return f"    {{ uint32_t t_ = {rs}; {ds} rt_call(t_); return; }}"
        raise ValueError(k)

    def op(self, i):
        """C for a non-control instruction."""
        k = i.kind
        rs, rt, rd = f"R[{i.rs}]", f"R[{i.rt}]", f"R[{i.rd}]"
        imm, uimm = i.imm, i.uimm

        def setd(reg, expr):
            if reg == 0:
                return "/* nop */" if expr is None else f"(void)({expr});"
            return f"R[{reg}] = {expr};"

        if i.w == 0:
            return "/* nop */"
        if k == "sll":
            return setd(i.rd, f"{rt} << {i.sa}")
        if k == "srl":
            return setd(i.rd, f"{rt} >> {i.sa}")
        if k == "sra":
            return setd(i.rd, f"(uint32_t)((int32_t){rt} >> {i.sa})")
        if k == "sllv":
            return setd(i.rd, f"{rt} << ({rs} & 31)")
        if k == "srlv":
            return setd(i.rd, f"{rt} >> ({rs} & 31)")
        if k == "srav":
            return setd(i.rd, f"(uint32_t)((int32_t){rt} >> ({rs} & 31))")
        if k == "syscall":
            return f"rt_syscall(0x{i.pc:08X}u);"
        if k == "break":
            return f"rt_break(0x{i.pc:08X}u, {(i.w >> 6) & 0xFFFFF});"
        if k in ("tge", "tgeu", "tlt", "tltu", "teq", "tne"):
            cond = {"tge": f"(int32_t){rs} >= (int32_t){rt}", "tgeu": f"{rs} >= {rt}",
                    "tlt": f"(int32_t){rs} < (int32_t){rt}", "tltu": f"{rs} < {rt}",
                    "teq": f"{rs} == {rt}", "tne": f"{rs} != {rt}"}[k]
            return f"if ({cond}) rt_break(0x{i.pc:08X}u, 0x7E0 | {(i.w >> 6) & 0x3FF});"
        if k == "mfhi":
            return setd(i.rd, "HI")
        if k == "mflo":
            return setd(i.rd, "LO")
        if k == "mthi":
            return f"HI = {rs};"
        if k == "mtlo":
            return f"LO = {rs};"
        if k == "mult":
            return f"{{ int64_t p_ = (int64_t)(int32_t){rs} * (int32_t){rt}; LO = (uint32_t)p_; HI = (uint32_t)(p_ >> 32); }}"
        if k == "multu":
            return f"{{ uint64_t p_ = (uint64_t){rs} * {rt}; LO = (uint32_t)p_; HI = (uint32_t)(p_ >> 32); }}"
        if k == "div":
            return f"rt_div({rs}, {rt});"
        if k == "divu":
            return f"rt_divu({rs}, {rt});"
        if k in ("add", "addu"):
            return setd(i.rd, f"{rs} + {rt}")
        if k in ("sub", "subu"):
            return setd(i.rd, f"{rs} - {rt}")
        if k == "and":
            return setd(i.rd, f"{rs} & {rt}")
        if k == "or":
            return setd(i.rd, f"{rs} | {rt}")
        if k == "xor":
            return setd(i.rd, f"{rs} ^ {rt}")
        if k == "nor":
            return setd(i.rd, f"~({rs} | {rt})")
        if k == "slt":
            return setd(i.rd, f"(int32_t){rs} < (int32_t){rt}")
        if k == "sltu":
            return setd(i.rd, f"{rs} < {rt}")
        if k in ("addi", "addiu"):
            return setd(i.rt, f"{rs} + 0x{imm & 0xFFFFFFFF:X}u")
        if k == "slti":
            return setd(i.rt, f"(int32_t){rs} < {imm}")
        if k == "sltiu":
            return setd(i.rt, f"{rs} < 0x{imm & 0xFFFFFFFF:X}u")
        if k == "andi":
            return setd(i.rt, f"{rs} & 0x{uimm:X}u")
        if k == "ori":
            return setd(i.rt, f"{rs} | 0x{uimm:X}u")
        if k == "xori":
            return setd(i.rt, f"{rs} ^ 0x{uimm:X}u")
        if k == "lui":
            return setd(i.rt, f"0x{uimm << 16:08X}u")
        addr = f"{rs} + 0x{imm & 0xFFFFFFFF:X}u"
        if k == "lb":
            return setd(i.rt, f"(uint32_t)(int32_t)(int8_t)MEM_R8({addr})")
        if k == "lbu":
            return setd(i.rt, f"MEM_R8({addr})")
        if k == "lh":
            return setd(i.rt, f"(uint32_t)(int32_t)(int16_t)MEM_R16({addr})")
        if k == "lhu":
            return setd(i.rt, f"MEM_R16({addr})")
        if k == "lw":
            return setd(i.rt, f"MEM_R32({addr})")
        if k == "lwl":
            return f"R[{i.rt}] = rt_lwl({rt}, {addr});" if i.rt else "/* lwl r0 */"
        if k == "lwr":
            return f"R[{i.rt}] = rt_lwr({rt}, {addr});" if i.rt else "/* lwr r0 */"
        if k == "sb":
            return f"MEM_W8({addr}, {rt});"
        if k == "sh":
            return f"MEM_W16({addr}, {rt});"
        if k == "sw":
            return f"MEM_W32({addr}, {rt});"
        if k == "swl":
            return f"rt_swl({addr}, {rt});"
        if k == "swr":
            return f"rt_swr({addr}, {rt});"
        if k == "lwc2":
            return f"gte_write_data({i.rt}, MEM_R32({addr}));"
        if k == "swc2":
            return f"gte_swc2({addr}, {i.rt});"
        if k == "mfc0":
            return setd(i.rt, f"rt_mfc0({i.rd})")
        if k == "mtc0":
            return f"rt_mtc0({i.rd}, {rt});"
        if k == "rfe":
            return "rt_rfe();"
        if k == "mfc2":
            return setd(i.rt, f"gte_read_data({i.rd})")
        if k == "cfc2":
            return setd(i.rt, f"gte_read_ctrl({i.rd})")
        if k == "mtc2":
            return f"gte_write_data({i.rd}, {rt});"
        if k == "ctc2":
            return f"gte_write_ctrl({i.rd}, {rt});"
        if k == "cop2":
            return f"gte_command(0x{i.w & 0x1FFFFFF:07X}u);"
        raise ValueError(f"{k} at {i.pc:#x}")


M_KERNEL = None


def load_hooks():
    """{(module, addr): C function} called on entry to a translated function
    (after the resume switch, so not when a saved game continues there)."""
    out = {}
    path = os.path.join(M.ROOT, "tools/recomp/hooks.txt")
    if os.path.exists(path):
        for line in open(path):
            line = line.split("#")[0].split()
            if len(line) == 3:
                out[(line[0], int(line[1], 16))] = line[2]
    return out


HOOKS = {}


def load_native():
    """{(module, addr): name} of functions replaced by native C."""
    out = {}
    for line in open(os.path.join(M.ROOT, "tools/recomp/native.txt")):
        line = line.split("#")[0].split()
        if len(line) == 3:
            out[(line[0], int(line[1], 16))] = line[2]   # "~name": a wrapper (see wrapped())
    return out


NATIVE = {}
HOOKS = {}


def main():
    global M_KERNEL, NATIVE, HOOKS
    NATIVE = load_native()
    HOOKS.update(load_hooks())
    mods, _ = M.all_modules()
    want = set(sys.argv[1:])
    os.makedirs(OUT, exist_ok=True)
    M_KERNEL = mods[0]
    # Calls between modules: KERNEL functions only called from overlays, and
    # TEST functions called from the overlays that run alongside it (stage,
    # enemy, options), become function starts there too.
    test = next(m for m in mods if m.name == "test")
    for _ in range(3):
        before = (len(M_KERNEL.seeds), len(test.seeds))
        for mod in mods:
            if mod is M_KERNEL:
                continue
            for f in discover(mod).values():
                if f.bad:
                    continue
                for c in f.calls:
                    if M_KERNEL.has(c):
                        M_KERNEL.seeds.add(c)
                    elif mod.name.startswith("ovl") or mod.name == "option":
                        if test.has(c) and not mod.has(c):
                            test.seeds.add(c)
        if (len(M_KERNEL.seeds), len(test.seeds)) == before:
            break
    kernel_funcs = discover(M_KERNEL)
    table = []
    total_bad = 0
    for mod in mods:
        funcs = kernel_funcs if mod is M_KERNEL else discover(mod)
        good = {e: f for e, f in funcs.items() if not f.bad}
        bad = {e: f for e, f in funcs.items() if f.bad}
        total_bad += len(bad)
        for e, f in sorted(bad.items()):
            print(f"  {mod.name}: skip {e:#010x}: {f.bad}", file=sys.stderr)
        for e in good:
            table.append((mod, e))
        if want and mod.name not in want:
            continue
        em = Emitter(mod, funcs, kernel_funcs, mods)
        body = [em.emit_func(f) for e, f in sorted(good.items())]
        with open(os.path.join(OUT, f"{mod.name}.c"), "w") as fh:
            fh.write('#include "recomp.h"\n#pragma clang diagnostic ignored "-Wunused-label"\n\n')
            for e in sorted(good):
                fh.write(f"void {fname(mod, e)}(void);\n")
                if (mod.name, e) in NATIVE:
                    fh.write(f"void {tname(mod, e)}(void);\n")
            if mod is not M_KERNEL:
                for e in sorted(kernel_funcs):
                    if not kernel_funcs[e].bad:
                        fh.write(f"void {fname(M_KERNEL, e)}(void);\n")
            fh.write("\n")
            fh.write("\n\n".join(body))
            fh.write("\n")
        print(f"{mod.name}: {len(good)} functions, {len(bad)} skipped, "
              f"{len(em.unresolved)} unresolved jr")
    # Address tables, one per module, for rt_call.
    with open(os.path.join(OUT, "funcs.c"), "w") as fh:
        fh.write('#include "recomp.h"\n\n')
        for mod, e in table:
            fh.write(f"void {fname(mod, e)}(void);\n")
            if (mod.name, e) in NATIVE:
                fh.write(f"void {tname(mod, e)}(void);\n")
        by_mod = {}
        for mod, e in table:
            by_mod.setdefault(mod.name, []).append(e)
        for mod in mods:
            es = sorted(by_mod.get(mod.name, []))
            fh.write(f"\nstatic const RtFunc tab_{mod.name}[] = {{\n")
            for e in es:
                fh.write(f"    {{0x{e:08X}u, {fname(mod, e)}, {tname(mod, e)}}},\n")
            fh.write("};\n")
            fh.write(f"static const uint8_t sig_{mod.name}[] = {{{', '.join(str(b) for b in mod.sig)}}};\n")
        fh.write("\nconst RtModule rt_modules[] = {\n")
        for mod in mods:
            fh.write(f"    {{\"{mod.name}\", 0x{mod.vram:08X}u, 0x{mod.code[0]:08X}u, 0x{mod.code[1]:08X}u, "
                     f"sig_{mod.name}, sizeof sig_{mod.name}, tab_{mod.name}, "
                     f"sizeof tab_{mod.name} / sizeof tab_{mod.name}[0]}},\n")
        fh.write("};\n")
        fh.write(f"const int rt_module_count = {len(mods)};\n")
    print(f"total skipped: {total_bad}")


if __name__ == "__main__":
    main()

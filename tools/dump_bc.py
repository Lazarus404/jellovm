#!/usr/bin/env python3
import struct
from pathlib import Path

OP_NAMES = {
    0: "NOP",
    1: "RET",
    2: "JMP",
    3: "JMP_IF",
    4: "MOV",
    8: "ASSERT",
    9: "CALL",
    10: "CALLR",
    16: "CONST_I32",
    28: "ADD_I32",
    35: "ADD_I32_IMM",
    61: "EQ_I32",
    62: "LT_I32",
    63: "EQ_I32_IMM",
    64: "LT_I32_IMM",
    "JOP_OBJ_SET_ATOM": 123,
}

# invert for printing
OP_BY_VAL = {
    0: "NOP",
    1: "RET",
    2: "JMP",
    3: "JMP_IF",
    4: "MOV",
    8: "ASSERT",
    9: "CALL",
    10: "CALLR",
    13: "CONST_FUN",
    16: "CONST_I32",
    19: "CONST_NULL",
    20: "CONST_ATOM",
    28: "ADD_I32",
    35: "ADD_I32_IMM",
    61: "EQ_I32",
    62: "LT_I32",
    63: "EQ_I32_IMM",
    64: "LT_I32_IMM",
    120: "OBJ_NEW",
    123: "OBJ_SET_ATOM",
}

JELLO_BC_FEAT_CONST64 = 1
JELLO_BC_FEAT_CONSTBYTES = 2
JELLO_BC_FEAT_CAP_START = 4
JELLO_BC_FEAT_LINE_TABLE = 8
JELLO_BC_FEAT_LINE_COL = 16


def main() -> None:
    path = Path(r"c:\Users\me\Documents\projects\jello\jello-compiler\bench\out\repro.jlo")
    data = path.read_bytes()
    off = 0

    def u32() -> int:
        nonlocal off
        v = struct.unpack_from("<I", data, off)[0]
        off += 4
        return v

    def u16() -> int:
        nonlocal off
        v = struct.unpack_from("<H", data, off)[0]
        off += 2
        return v

    def u8() -> int:
        nonlocal off
        v = data[off]
        off += 1
        return v

    magic = u32()
    version = u32()
    features = u32()
    ntypes = u32()
    nsigs = u32()
    natoms = u32()
    nfuncs = u32()
    entry = u32()
    print(f"magic={magic:#x} ver={version} feat={features} nfuncs={nfuncs} entry={entry}")

    if features & JELLO_BC_FEAT_CONST64:
        nconst_i64 = u32()
        nconst_f64 = u32()
    else:
        nconst_i64 = nconst_f64 = 0
    if features & JELLO_BC_FEAT_CONSTBYTES:
        nconst_bytes = u32()
    else:
        nconst_bytes = 0

    for _ in range(ntypes):
        off += 12
    for _ in range(nsigs):
        u32()
        nargs = u16()
        off += 2
        off += 4 * nargs
    for _ in range(natoms):
        ln = u32()
        off += ln

    if features & JELLO_BC_FEAT_CONST64:
        off += 8 * nconst_i64 + 8 * nconst_f64
    if features & JELLO_BC_FEAT_CONSTBYTES:
        for _ in range(nconst_bytes):
            ln = u32()
            off += ln
    if features & JELLO_BC_FEAT_LINE_TABLE:
        nsource = u32()
        for _ in range(nsource):
            ln = u32()
            off += ln

    funcs = []
    for _ in range(nfuncs):
        nregs = u32()
        cap_start = u32() if (features & JELLO_BC_FEAT_CAP_START) else 0
        ninsns = u32()
        off += 4 * nregs
        if features & JELLO_BC_FEAT_LINE_TABLE:
            u16()
            if features & JELLO_BC_FEAT_LINE_COL:
                off += 4 * ninsns
            else:
                off += 2 * ninsns
        insns = []
        for _ in range(ninsns):
            op, a, b, c = u8(), u8(), u8(), u8()
            imm = u32()
            insns.append((op, a, b, c, imm))
        funcs.append((nregs, cap_start, ninsns, insns))

    large = [(i, nregs, ninsns) for i, (nregs, cap, ninsns, insns) in enumerate(funcs) if ninsns >= 100]
    print("--- large funcs ---")
    for i, nregs, ninsns in large:
        print(f"func {i}: nregs={nregs} ninsns={ninsns}")

    # main body called from entry wrapper (CALL imm=207 in entry)
    target = 161
    if target < len(funcs):
        nregs, cap, ninsns, insns = funcs[target]
        print(f"\nfunc {target}: nregs={nregs} ninsns={ninsns}")
        for pc in range(100, ninsns):
            op, a, b, c, imm = insns[pc]
            name = OP_BY_VAL.get(op, str(op))
            extra = f" -> {pc + 1 + (imm if imm < 0x80000000 else imm - 2**32)}" if op in (2, 3) else ""
            if op in (2, 3):
                tgt = pc + 1 + (imm if imm < 0x80000000 else imm - 2**32)
                extra = f" -> {tgt}"
            print(f"  pc={pc:3d} {name:12s} a={a:3d} b={b:3d} c={c:3d} imm={imm}{extra}")
        print(f"--- OBJ_SET_ATOM imm in func {target} ---")
        for pc, (op, a, b, c, imm) in enumerate(insns):
            if op == 123:
                print(f"  pc={pc} val={a} obj={b} atom={imm}")
        for pc, (op, a, b, c, imm) in enumerate(insns):
            if a >= 250 or b >= 250 or c >= 250:
                name = OP_BY_VAL.get(op, str(op))
                print(f"pc={pc} {name} a={a} b={b} c={c}")

    nregs, cap, ninsns, insns = funcs[entry]
    print(f"entry func {entry}: nregs={nregs} ninsns={ninsns}")
    for pc in range(max(0, ninsns - 40), ninsns):
        op, a, b, c, imm = insns[pc]
        name = OP_BY_VAL.get(op, str(op))
        extra = ""
        if op in (2, 3):
            extra = f" -> {pc + 1 + imm}"
        print(f"  pc={pc:3d} {name:12s} a={a:3d} b={b:3d} c={c:3d} imm={imm}{extra}")

    print("--- regs >= 250 in entry ---")
    for pc, (op, a, b, c, imm) in enumerate(insns):
        if a >= 250 or b >= 250 or c >= 250:
            name = OP_BY_VAL.get(op, str(op))
            print(f"pc={pc} {name} a={a} b={b} c={c}")


if __name__ == "__main__":
    main()

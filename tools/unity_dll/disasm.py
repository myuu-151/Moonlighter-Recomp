"""Disassemble functions of a PE DLL by RVA, annotated with public symbol names.

    py -3.13 tools/unity_dll/disasm.py UnityPlayer.dll publics.txt 00ac8850 [start-end ...]

`publics.txt` is pdb_publics.py's output. A bare RVA runs to the next symbol; "start-end" is a
range. Calls and jumps out of the function get their target's name; RIP-relative loads get their
address, symbol and the float (dword), double (qword) or four floats (xmmword) stored there.
Needs pefile and capstone (py -3.13 -m pip install --user pefile capstone).
"""

import bisect
import re
import struct
import sys

import capstone
import pefile


def main(dll, publics, ranges):
    pe = pefile.PE(dll, fast_load=True)
    image = pe.get_memory_mapped_image()
    base = pe.OPTIONAL_HEADER.ImageBase
    syms = []
    for line in open(publics, encoding="latin1"):
        rva, name = line.rstrip("\n").split(" ", 1)
        syms.append((int(rva, 16), name))
    syms.sort()
    rvas = [r for r, _ in syms]

    def name_of(rva):
        i = bisect.bisect_right(rvas, rva) - 1
        if i < 0:
            return None
        return syms[i][1] if syms[i][0] == rva else f"{syms[i][1]}+{rva - syms[i][0]:x}"

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    for arg in ranges:
        if "-" in arg:
            start, end = (int(x, 16) for x in arg.split("-"))
        else:
            start = int(arg, 16)
            i = bisect.bisect_right(rvas, start)
            end = rvas[i] if i < len(rvas) else start + 0x1000
            print(f"===== {name_of(start)}")
        for ins in md.disasm(image[start:end], base + start):
            line = f"{ins.address - base:08x}  {ins.mnemonic:<8} {ins.op_str}"
            if ins.mnemonic in ("call", "jmp") and ins.op_str.startswith("0x"):
                target = int(ins.op_str, 16) - base
                if ins.mnemonic == "call" or not start <= target < end:
                    line += f"    ; {name_of(target)}"
            m = re.search(r"rip ([+-]) (0x[0-9a-f]+)", ins.op_str)
            if m:
                target = ins.address + ins.size + int(m.group(2), 16) * (1 if m.group(1) == "+" else -1) - base
                value = ""
                raw = image[target:target + 16]
                if "xmmword" in ins.op_str and len(raw) == 16:
                    value = " = " + str(struct.unpack("<4f", raw))
                elif "qword" in ins.op_str and len(raw) >= 8:
                    value = f" = {struct.unpack('<d', raw[:8])[0]!r}"
                elif "dword" in ins.op_str and len(raw) >= 4:
                    value = f" = {struct.unpack('<f', raw[:4])[0]!r} ({raw[:4].hex()})"
                line += f"    ; [{target:08x}]{value} {name_of(target) or ''}"
            print(line)


if __name__ == "__main__":
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2], sys.argv[3:])

"""List a PDB's public symbols (S_PUB32) as "rva name" lines, sorted by RVA.

For UnityPlayer.dll's PDB from Unity's symbol server (recomp/third_party/box2d/README.md, "Reading
Unity's fork"): the names are MSVC-decorated (?SolveVelocityConstraints@b2ContactSolver@@QEAAXXZ).

    py -3.13 tools/unity_dll/pdb_publics.py UnityPlayer_Win64_mono_x64.pdb > publics.txt

Reads the MSF container itself (stream directory, DBI stream, symbol record stream, section
headers); nothing beyond the standard library.
"""

import struct
import sys

MAGIC = b"Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0"
S_PUB32 = 0x110E


def main(path):
    data = open(path, "rb").read()
    if data[: len(MAGIC)] != MAGIC:
        sys.exit(f"{path}: not an MSF 7.00 PDB")
    block_size, _, _, dir_size, _, dir_map = struct.unpack_from("<6I", data, 32)

    def blocks(numbers, size):
        return b"".join(data[b * block_size:(b + 1) * block_size] for b in numbers)[:size]

    n = (dir_size + block_size - 1) // block_size
    directory = blocks(struct.unpack_from(f"<{n}I", data, dir_map * block_size), dir_size)
    count = struct.unpack_from("<I", directory, 0)[0]
    sizes = [0 if s == 0xFFFFFFFF else s for s in struct.unpack_from(f"<{count}I", directory, 4)]
    streams, at = [], 4 + 4 * count
    for size in sizes:
        k = (size + block_size - 1) // block_size
        streams.append((struct.unpack_from(f"<{k}I", directory, at), size))
        at += 4 * k

    def stream(i):
        return blocks(*streams[i])

    dbi = stream(3)
    # DBI header: the symbol record stream, then the substream sizes up to the optional debug header.
    sym_stream = struct.unpack_from("<H", dbi, 20)[0]
    mod_info, sec_contrib, sec_map, file_info, ts_map, _, _, ec_info = struct.unpack_from("<8i", dbi, 24)
    debug_header = 64 + mod_info + sec_contrib + sec_map + file_info + ts_map + ec_info
    section_headers = stream(struct.unpack_from("<11H", dbi, debug_header)[5])
    section_rva = [struct.unpack_from("<8sII", section_headers, i * 40)[2] for i in range(len(section_headers) // 40)]

    records, pos, out = stream(sym_stream), 0, []
    while pos + 4 <= len(records):
        length, kind = struct.unpack_from("<HH", records, pos)
        if kind == S_PUB32:
            _, offset, segment = struct.unpack_from("<IIH", records, pos + 4)
            name = records[pos + 14:records.index(b"\0", pos + 14)].decode("latin1")
            if 1 <= segment <= len(section_rva):
                out.append((section_rva[segment - 1] + offset, name))
        pos += length + 2
    for rva, name in sorted(out):
        print(f"{rva:08x} {name}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])

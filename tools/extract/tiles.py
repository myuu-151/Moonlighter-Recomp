"""The textures the generator paints into a room's runtime textures (the holes, the slime
family and the floor details), exact: the game copies a tile sprite's whole
texture with GetPixels32, so these are the textures' RGBA as Unity reads them (rows from
the bottom; checked equal to the game's own GetPixels32 for every readable texture the
oracle recorded).

  tiles/<culture>/holes.bin, tiles/<culture>/details<level>.bin, tiles/common.bin (big-endian;
  split so a floor loads only its own):
    "MLTX", u32 version (2), u32 texture count, then per texture:
      u16 width, u16 height, then width * height * 4 bytes RGBA;
    u32 key count, then per key: u16 length, key, u32 its texture (sprites sharing a
    texture, an atlas the game copies whole, share one copy)
  keys: in holes.bin "<generation data field>#<index>" (holeBorderVerticalSprite#0); in
  details<level>.bin "details<level>.<lower|lower2|middle>#<index>" (that floor's details
  register's layer sprites); in common.bin "<slimeSprites|acidSprites|...>#<index>"
  (DungeonRoomGenerator's, from dungeon_unknown, the one bundle that serializes it)

Needs UnityPy.

    python tools/extract/tiles.py --game <the game's folder> --out <folder>
    (reads generation_data_counts.json and details.json from <folder>)
"""
import argparse
import json
import shutil
import struct
from pathlib import Path

import UnityPy

CULTURES = {'Golem': 'golem', 'Forest': 'forest', 'Desert': 'desert', 'Tech': 'tesla'}
HOLE_FIELDS = ('holeSprite', 'holeBorderHorizontalUpSprite', 'holeBorderHorizontalDownSprite', 'holeBorderVerticalSprite',
               'holeBorderCornerUpSprites', 'holeBorderCornerDownSprites', 'holeWallHorizontalSprite', 'holeWallVerticalSprite',
               'holeWallCornerSprite', 'holeWallBorderHorizontalUpSprite', 'holeWallBorderHorizontalDownSprite',
               'holeWallBorderVerticalSprite', 'holeSlopeSprite', 'holeWallSlopeVerticalSprite', 'holeWallSlopeHorizontalSprite',
               'holeSlopeCornerSprite')


def dxt(raw, w, h, alpha_block):
    """DXT1/DXT5 as Unity's GetPixels32 decodes them (found against the oracle's CRCs, where
    UnityPy's decoder differs): 565 colours widened by bit replication, the in-between colours
    by integer division (floor), and a block whose c0 <= c1 in three-colour mode (the middle
    colour and black), DXT5's too. Alpha blocks: the standard eight-value ramp, floored.
    Rows from the bottom (the data's order)."""
    def widen(v, bits):
        return (v << (8 - bits)) | (v >> (2 * bits - 8))

    out = bytearray(w * h * 4)
    size = 16 if alpha_block else 8
    bw = (w + 3) // 4
    for bi in range(len(raw) // size):
        bx, by = bi % bw, bi // bw
        if by * 4 >= h:
            break
        blk = raw[bi * size:bi * size + size]
        alphas = None
        if alpha_block:
            a0, a1 = blk[0], blk[1]
            abits = int.from_bytes(blk[2:8], 'little')
            if a0 > a1:
                alphas = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
            else:
                alphas = [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
            blk = blk[8:]
        c0, c1, cbits = struct.unpack('<HHI', blk[:8])
        p0 = (widen(c0 >> 11, 5), widen((c0 >> 5) & 63, 6), widen(c0 & 31, 5), 255)
        p1 = (widen(c1 >> 11, 5), widen((c1 >> 5) & 63, 6), widen(c1 & 31, 5), 255)
        if c0 > c1:
            cols = [p0, p1, tuple((2 * p0[k] + p1[k]) // 3 for k in range(3)) + (255,), tuple((p0[k] + 2 * p1[k]) // 3 for k in range(3)) + (255,)]
        else:
            cols = [p0, p1, tuple((p0[k] + p1[k]) // 2 for k in range(3)) + (255,), (0, 0, 0, 0 if not alpha_block else 255)]
        for i in range(16):
            x, y = bx * 4 + i % 4, by * 4 + i // 4
            if x >= w or y >= h:
                continue
            r, g, b, a = cols[(cbits >> (2 * i)) & 3]
            if alphas is not None:
                a = alphas[(abits >> (3 * i)) & 7]
            out[(y * w + x) * 4:(y * w + x) * 4 + 4] = bytes((r, g, b, a))
    return bytes(out)


def texture_rgba(tex):
    """The texture's pixels as GetPixels32 gives them: RGBA, rows from the bottom."""
    fmt = int(tex.m_TextureFormat)
    if fmt in (10, 12):  # DXT1, DXT5
        return tex.m_Width, tex.m_Height, dxt(bytes(tex.image_data), tex.m_Width, tex.m_Height, fmt == 12)
    img = tex.image.convert('RGBA')
    w, h = img.size
    px = img.load()
    return w, h, bytes(c for y in range(h) for x in range(w) for c in px[x, h - 1 - y])


def texture_entry(key, sprite):
    w, h, rgba = texture_rgba(sprite.m_RD.texture.read())
    return key, struct.pack('>HH', w, h) + rgba


def write(path, entries):
    textures, index, keys = [], {}, bytearray()
    for key, texture in entries:
        if texture not in index:
            index[texture] = len(textures)
            textures.append(texture)
        k = key.encode()
        keys += struct.pack('>H', len(k)) + k + struct.pack('>I', index[texture])
    path.write_bytes(b'MLTX' + struct.pack('>II', 2, len(textures)) + b''.join(textures) + struct.pack('>I', len(entries)) + bytes(keys))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    generation = json.loads((Path(args.out) / 'generation_data_counts.json').read_text(encoding='utf-8'))
    details = json.loads((Path(args.out) / 'details.json').read_text(encoding='utf-8'))
    culture_ids = {'Golem': 1, 'Forest': 2, 'Desert': 3, 'Tech': 4}
    bundles = Path(args.game) / 'Moonlighter_Data' / 'StreamingAssets' / 'AssetBundles'
    out_dir = Path(args.out) / 'tiles'
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for culture, suffix in CULTURES.items():
        sprites = {}
        for name in (f'dungeon_{suffix}', 'dungeon_common'):
            for obj in UnityPy.load(str(bundles / name)).objects:
                if obj.type.name == 'Sprite':
                    sprites.setdefault(obj.read().m_Name, obj)
        folder = out_dir / culture.lower()
        folder.mkdir(parents=True, exist_ok=True)
        entries = []
        lists = generation[culture]['sprites']
        for field in HOLE_FIELDS:
            for i, name in enumerate(lists.get(field, [])):
                obj = sprites.get(name) if name else None
                if obj is not None:
                    entries.append(texture_entry(f'{field}#{i}', obj.read()))
        write(folder / 'holes.bin', entries)
        count = len(entries)
        for reg in details:
            if reg['culture'] != culture_ids[culture]:
                continue
            entries = []
            for layer in ('lower', 'lower2', 'middle'):
                for i, s in enumerate(reg[layer]):
                    obj = sprites.get(s['name']) if s else None
                    if obj is not None:
                        entries.append(texture_entry(f"details{reg['level']}.{layer}#{i}", obj.read()))
            write(folder / f"details{reg['level']}.bin", entries)
            count += len(entries)
        print(f'   {culture}: {count} tile textures', flush=True)

    # The slime family's tiles: DungeonRoomGenerator's lists.
    entries = []
    for obj in UnityPy.load(str(bundles / 'dungeon_unknown')).objects:
        if obj.type.name != 'MonoBehaviour':
            continue
        try:
            tree = obj.read_typetree()
        except Exception:  # noqa: BLE001
            continue
        if 'slimeSprites' not in tree:
            continue
        mb = obj.read()
        for field in ('slimeSprites', 'acidSprites', 'lavaSprites', 'electricSprites'):
            for i, ref in enumerate(getattr(mb, field)):
                if ref.m_PathID != 0:
                    entries.append(texture_entry(f'{field}#{i}', ref.read()))
        break
    write(out_dir / 'common.bin', entries)
    print(f'   slime family: {len(entries)} tile textures', flush=True)


if __name__ == '__main__':
    main()

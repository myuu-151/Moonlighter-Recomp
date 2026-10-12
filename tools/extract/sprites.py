"""The sprites a dungeon shows, per culture: what its prefabs' SpriteRenderers use (its
dungeon and enemy bundles, the chests, the shared prefabs a room can hold), its
DungeonRoomGenerationData art (floors, walls, lights, shadows, hole tiles) and its details
registers' layer sprites. Each sprite is the packed part of its texture, placed relative to
the sprite's pivot, as a GameCube texture of its own file so a floor loads only what it
shows:

  sprites/<culture>/index.bin (big-endian):
    "MLSI", u32 version (2), u32 count, then per sprite:
      u16 name length, name (UTF-8), u16 width, u16 height (pixels),
      f32 left, bottom (the image's lower-left corner from the pivot, in units),
      f32 units per pixel (1 / pixelsPerUnit), u8 format (0 RGB5A3, 1 CI8)
  sprites/<culture>/<n>.tex, sprite n of the index: GX texels, rows from the top, padded to
    whole tiles (RGB5A3: 4 x 4; CI8: 8 x 4); CI8's 256-entry RGB5A3 palette after them.

CI8 when the sprite has at most 256 colours once in RGB5A3 (pixel art mostly does: then
nothing is lost but the 5-bit channels), else RGB5A3.

Needs UnityPy (and Pillow, which it brings).

    python tools/extract/sprites.py --game <the game's folder> --out <folder>
    (reads prefabs.json, generation_data_counts.json, details.json and roomdb_asset.json
    from <folder>, which the other extractors make first)
"""
import argparse
import json
import shutil
import struct
from pathlib import Path

import UnityPy

CULTURES = {'golem': 'Golem', 'forest': 'Forest', 'desert': 'Desert', 'tesla': 'Tech'}
CULTURE_IDS = {'Golem': 1, 'Forest': 2, 'Desert': 3, 'Tech': 4}
# Shared prefabs a room of any dungeon can hold (RoomScene: the room itself, the secret room's
# rope and advisors, the shrine, the shop chest); plus the props room designs name.
COMMON = ('DungeonRoom', 'Shrine', 'Rope', 'SecretRoomAdvisor', 'IntermediateAdvisor', 'DungeonToShopChest')


def rgb5a3(r, g, b, a):
    if a >= 0xE0:  # opaque: 1 RRRRR GGGGG BBBBB
        return 0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
    return ((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4)  # 0 AAA RRRR GGGG BBBB


def texels(img):
    """(format, bytes): CI8 with an RGB5A3 palette if 256 colours do, else RGB5A3."""
    w, h = img.size
    px = img.load()
    colours = [[rgb5a3(*px[x, y]) for x in range(w)] for y in range(h)]
    palette = sorted({c for row in colours for c in row})
    if len(palette) <= 256:
        index = {c: i for i, c in enumerate(palette)}
        out = bytearray()
        for ty in range(0, (h + 3) // 4 * 4, 4):
            for tx in range(0, (w + 7) // 8 * 8, 8):
                for y in range(ty, ty + 4):
                    for x in range(tx, tx + 8):
                        out.append(index[colours[y][x]] if x < w and y < h else index.get(0, 0))
        for i in range(256):
            out += struct.pack('>H', palette[i] if i < len(palette) else 0)
        return 1, bytes(out)
    out = bytearray()
    for ty in range(0, (h + 3) // 4 * 4, 4):
        for tx in range(0, (w + 3) // 4 * 4, 4):
            for y in range(ty, ty + 4):
                for x in range(tx, tx + 4):
                    out += struct.pack('>H', colours[y][x] if x < w and y < h else 0)
    return 0, bytes(out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    out_dir = Path(args.out) / 'sprites'
    if out_dir.exists():
        shutil.rmtree(out_dir)
    prefabs = json.loads((Path(args.out) / 'prefabs.json').read_text(encoding='utf-8'))
    generation = json.loads((Path(args.out) / 'generation_data_counts.json').read_text(encoding='utf-8'))
    details = json.loads((Path(args.out) / 'details.json').read_text(encoding='utf-8'))
    rooms = json.loads((Path(args.out) / 'roomdb_asset.json').read_text(encoding='utf-8'))
    room_props = {p['prefab']['name'] for r in rooms['database']['rooms'] for p in r.get('prefabSpawnPacks', [])}

    bundles_dir = Path(args.game) / 'Moonlighter_Data' / 'StreamingAssets' / 'AssetBundles'
    files = {p.name: p for p in sorted(bundles_dir.iterdir()) if p.name.startswith(('dungeon_', 'enemies_')) and p.suffix != '.manifest'}
    files['sharedassets0.assets'] = Path(args.game) / 'Moonlighter_Data' / 'sharedassets0.assets'
    sprite_objs = {}

    def sprites_in(file):
        if file not in sprite_objs:
            table = {}
            for obj in UnityPy.load(str(files[file])).objects:
                if obj.type.name == 'Sprite':
                    table.setdefault(obj.read().m_Name, obj)
            sprite_objs[file] = table
        return sprite_objs[file]

    summary = {}
    for suffix, culture in CULTURES.items():
        dungeon, enemies = f'dungeon_{suffix}', f'enemies_{suffix}'
        wanted = {}  # name -> files to look in, in order

        def want(name, *where):
            if name and name != '?external':
                for w in where:
                    if w not in wanted.setdefault(name, []):
                        wanted[name].append(w)

        for file in (dungeon, enemies, 'sharedassets0.assets'):
            for rows in prefabs['bundles'].get(file, {}).values():
                for r in rows:
                    want(r[6], file)
        for name, rows in prefabs['bundles'].get('dungeon_common', {}).items():
            if name in COMMON or name in room_props:
                for r in rows:
                    want(r[6], 'dungeon_common')
        for names in generation.get(culture, {}).get('sprites', {}).values():
            for n in names:
                want(n, dungeon, 'dungeon_common')
        for reg in details:
            if reg['culture'] == CULTURE_IDS[culture]:
                for layer in ('lower', 'lower2', 'middle'):
                    for s in reg[layer]:
                        if s:
                            want(s['name'], dungeon, 'dungeon_common')

        folder = out_dir / culture.lower()
        folder.mkdir(parents=True, exist_ok=True)
        index = bytearray()
        count, total, ci8, missing = 0, 0, 0, []
        for name in sorted(wanted):
            obj = next((sprites_in(f).get(name) for f in wanted[name] + [dungeon, 'dungeon_common'] if sprites_in(f).get(name) is not None), None)
            if obj is None:
                missing.append(name)
                continue
            s = obj.read()
            img = s.image.convert('RGBA')
            ppu = s.m_PixelsToUnits or 1.0
            rect, pivot, offset = s.m_Rect, s.m_Pivot, s.m_RD.textureRectOffset
            fmt, data = texels(img)
            (folder / f'{count}.tex').write_bytes(data)
            encoded = name.encode('utf-8')
            index += struct.pack('>H', len(encoded)) + encoded
            index += struct.pack('>HHfffB', img.size[0], img.size[1], (offset.x - pivot.x * rect.width) / ppu,
                                 (offset.y - pivot.y * rect.height) / ppu, 1.0 / ppu, fmt)
            count += 1
            total += len(data)
            ci8 += fmt
        (folder / 'index.bin').write_bytes(b'MLSI' + struct.pack('>II', 2, count) + bytes(index))
        summary[culture] = {'sprites': count, 'ci8': ci8, 'bytes': total, 'missing': missing}
        print(f'   {culture}: {count} sprites ({ci8} CI8), {total // 1024} KB' + (f', {len(missing)} not found' if missing else ''), flush=True)
    (out_dir / 'summary.json').write_text(json.dumps(summary, indent=1), encoding='utf-8')


if __name__ == '__main__':
    main()

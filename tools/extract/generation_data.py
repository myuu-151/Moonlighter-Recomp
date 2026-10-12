"""The dungeons' DungeonRoomGenerationData assets (one per culture): background, light,
shadow and hole sprite lists. The generator's draws depend on the lists' sizes ("counts");
the sprite names are for drawing.

Needs UnityPy.

    python tools/extract/generation_data.py --game <the game's folder> --out <folder>
    -> <folder>/generation_data_counts.json  {culture: {asset, counts, sprites}}
"""
import argparse
import json
from pathlib import Path

import UnityPy

# DungeonRoomGenerationData assets, by culture name (ItemMaster.Culture).
ASSETS = {'Golem': 'GolemDungeonRoomGenerationData', 'Forest': 'ForestDungeonRoomGenerationData',
          'Desert': 'DesertDungeonRoomGenerationData', 'Tech': 'TeslaDungeonRoomGenerationData',
          'Unknown': 'UnknownDungeonRoomGenerationData'}


def name_of(pptr):
    if pptr is None or pptr.m_PathID == 0:
        return None
    try:
        return pptr.read().m_Name
    except Exception:  # noqa: BLE001 - unresolvable reference counts as missing
        return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    bundles = Path(args.game) / 'Moonlighter_Data' / 'StreamingAssets' / 'AssetBundles'
    env = UnityPy.load(*[str(p) for p in sorted(bundles.glob('dungeon_*')) if p.suffix != '.manifest'])
    by_name = {}
    for obj in env.objects:
        if obj.type.name != 'MonoBehaviour':
            continue
        try:
            tree = obj.read_typetree()
        except Exception:  # noqa: BLE001
            continue
        if 'holeSlopeCornerSprite' in tree:
            by_name[tree['m_Name']] = obj
    out = {}
    for culture, asset in ASSETS.items():
        obj = by_name.get(asset)
        if obj is None:
            continue
        data = obj.read()
        tree = obj.read_typetree()
        counts, sprites = {}, {}
        for field, value in tree.items():
            if field in ('m_Name', 'm_Enabled', 'm_EditorHideFlags', 'm_EditorClassIdentifier', 'm_Script', 'm_GameObject'):
                continue
            if isinstance(value, list):
                counts[field] = len(value)
                sprites[field] = [name_of(p) for p in getattr(data, field)]
            elif isinstance(value, dict) and 'm_PathID' in value:
                counts[field] = 'null' if value['m_PathID'] == 0 else 'set'
        out[culture] = {'asset': asset, 'counts': counts, 'sprites': sprites}
    missing = [c for c, a in ASSETS.items() if c not in out]
    if missing:
        raise SystemExit(f"no DungeonRoomGenerationData for {', '.join(missing)}")
    target = Path(args.out) / 'generation_data_counts.json'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(out, indent=1), encoding='utf-8')
    print(f"   {', '.join(out)}", flush=True)


if __name__ == '__main__':
    main()

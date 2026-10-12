"""The text assets the game reads with Resources.Load<TextAsset>: "constants" (the
Constants table, FullSerializer JSON), "rooms" (the dungeon room database) and
"specialRooms" (initial room, camp, healing room, ...) and "enemy_stats" (each enemy's base
stats by name, EnemyStatManager's: size, tint, maxHealth, speed, strength, defense, weight,
shieldPushForce, culture, isModifiedByFloor), as the game has them.

Needs UnityPy.

    python tools/extract/resources_text.py --game <the game's folder> --out <folder>
    -> <folder>/constants.json, rooms.json, specialRooms.json, enemy_stats.json
"""
import argparse
from pathlib import Path

import UnityPy

WANTED = ('constants', 'rooms', 'specialRooms', 'enemy_stats')


def text_of(asset):
    script = asset.m_Script
    return script if isinstance(script, str) else bytes(script).decode('utf-8')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    env = UnityPy.load(str(Path(args.game) / 'Moonlighter_Data' / 'resources.assets'))
    # Resources.Load finds an asset by its path below a Resources folder; a player build
    # keeps no paths in resources.assets, but these names are unique in it (checked).
    found = {}
    for obj in env.objects:
        if obj.type.name != 'TextAsset':
            continue
        asset = obj.read()
        if asset.m_Name in WANTED:
            if asset.m_Name in found:
                raise SystemExit(f'resources.assets has two "{asset.m_Name}" texts')
            found[asset.m_Name] = text_of(asset)
    missing = [n for n in WANTED if n not in found]
    if missing:
        raise SystemExit(f"resources.assets has no {', '.join(missing)} text")
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for name, text in found.items():
        (out / f'{name}.json').write_text(text, encoding='utf-8')
    print(f"   {', '.join(f'{n} ({len(t) // 1024} KB)' for n, t in found.items())}", flush=True)


if __name__ == '__main__':
    main()

"""The Wanderer DLC's special rooms (WND_INTERMEDIATE and the rest): the "specialRooms"
TextAsset in the DLC's dungeon bundle, which is itself stored as a TextAsset
(wanderer_dlc_dungeon) inside dlc/wanderer_dlc.

Needs UnityPy.

    python tools/extract/wanderer_rooms.py --game <the game's folder> --out <folder>
    -> <folder>/wanderer_specialRooms.json  (nothing, and says so, without the DLC)
"""
import argparse
import io
from pathlib import Path

import UnityPy


def text_of(asset):
    script = asset.m_Script
    return script if isinstance(script, str) else bytes(script).decode('utf-8')


def bytes_of(asset):
    script = asset.m_Script
    return script.encode('utf-8', 'surrogateescape') if isinstance(script, str) else bytes(script)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    dlc = Path(args.game) / 'dlc' / 'wanderer_dlc'
    if not dlc.exists():
        print('   no Wanderer DLC (dlc/wanderer_dlc): skipped', flush=True)
        return
    for obj in UnityPy.load(str(dlc)).objects:
        if obj.type.name != 'TextAsset':
            continue
        outer = obj.read()
        if outer.m_Name != 'wanderer_dlc_dungeon':
            continue
        for inner in UnityPy.load(io.BytesIO(bytes_of(outer))).objects:
            if inner.type.name != 'TextAsset':
                continue
            asset = inner.read()
            if asset.m_Name == 'specialRooms':
                out = Path(args.out) / 'wanderer_specialRooms.json'
                out.parent.mkdir(parents=True, exist_ok=True)
                out.write_text(text_of(asset), encoding='utf-8')
                print('   the DLC\'s special rooms', flush=True)
                return
    raise SystemExit("the Wanderer DLC's bundle has no specialRooms text")


if __name__ == '__main__':
    main()

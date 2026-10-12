"""The room database as the game has it when it generates: the DungeonRoomDatabase asset
(in dungeon_common: the special rooms, the corrupted-room lists, the room prefab) with its
`database` overwritten by the "rooms" text, as DungeonRoomDatabase.LoadFromJSON does
(JsonUtility.FromJsonOverwrite; the asset's own serialized rooms are stale, with weights
already penalised). Fields the text lacks keep their defaults (`floors`: 0, 0). Checked
against the oracle's dump of the live object: equal but for the prefab reference.

Needs UnityPy, and rooms.json in --out (tools/extract/resources_text.py makes it).

    python tools/extract/room_database.py --game <the game's folder> --out <folder>
    -> <folder>/roomdb_asset.json
"""
import argparse
import json
from pathlib import Path

import UnityPy

UNITY_FIELDS = ('m_GameObject', 'm_Enabled', 'm_Script', 'm_Name', 'm_EditorHideFlags', 'm_EditorClassIdentifier')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    bundle = Path(args.game) / 'Moonlighter_Data' / 'StreamingAssets' / 'AssetBundles' / 'dungeon_common'
    for obj in UnityPy.load(str(bundle)).objects:
        if obj.type.name != 'MonoBehaviour':
            continue
        try:
            tree = obj.read_typetree()
        except Exception:  # noqa: BLE001
            continue
        if 'campRoomInfo' not in tree or 'database' not in tree:
            continue
        data = {k: v for k, v in tree.items() if k not in UNITY_FIELDS}
        rooms = json.loads((Path(args.out) / 'rooms.json').read_text(encoding='utf-8'))
        database = dict(data['database'])
        database.update(rooms)
        for room in database['rooms']:
            room.setdefault('floors', {'x': 0, 'y': 0})
        data['database'] = database
        target = Path(args.out) / 'roomdb_asset.json'
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps(data), encoding='utf-8')
        print(f"   {len(data['database']['rooms'])} rooms", flush=True)
        return
    raise SystemExit('dungeon_common has no DungeonRoomDatabase')


if __name__ == '__main__':
    main()

"""Per-culture packs of the big data, for the GameCube: one floor needs one dungeon's rooms
and prefabs, and the whole room database (2 MB of JSON, ~18 MB parsed) or every prefab
(~9 MB parsed) won't fit beside the game.

  packs/<culture>/rooms.json    roomdb_asset.json with only that culture's room designs, in
                                their order (room picks filter by culture, so the picks are
                                the same), plus every design a corrupted-room list names
  packs/<culture>/prefabs.json  prefabs.json with only that culture's dungeon and enemy
                                bundles, the chests, the shared prefabs a room can hold, the
                                hero and the weapons the port has (WEAPONS)

    python tools/extract/packs.py --out <folder>   (reads roomdb_asset.json, prefabs.json)
"""
import argparse
import json
from pathlib import Path

CULTURES = {1: ('Golem', 'golem'), 2: ('Forest', 'forest'), 3: ('Desert', 'desert'), 4: ('Tech', 'tesla')}
COMMON = ('DungeonRoom', 'Shrine', 'Rope', 'SecretRoomAdvisor', 'IntermediateAdvisor', 'DungeonToShopChest')
# The items bundle's weapons the hero can carry into a floor, in the port so far.
WEAPONS = ('Broom Spear',)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    out = Path(args.out)
    rooms = json.loads((out / 'roomdb_asset.json').read_text(encoding='utf-8'))
    prefabs = json.loads((out / 'prefabs.json').read_text(encoding='utf-8'))
    corrupted = {n for key, names in rooms['database'].items() if key.endswith('CorruptedRooms') for n in names}
    room_props = {p['prefab']['name'] for r in rooms['database']['rooms'] for p in r.get('prefabSpawnPacks', [])}
    for culture_id, (culture, suffix) in CULTURES.items():
        folder = out / 'packs' / culture.lower()
        folder.mkdir(parents=True, exist_ok=True)
        pack = dict(rooms)
        database = dict(rooms['database'])
        database['rooms'] = [r for r in rooms['database']['rooms'] if r['culture'] == culture_id or r['name'] in corrupted]
        pack['database'] = database
        (folder / 'rooms.json').write_text(json.dumps(pack, separators=(',', ':')), encoding='utf-8')
        bundles = {}
        for bundle in (f'dungeon_{suffix}', f'enemies_{suffix}', 'sharedassets0.assets'):
            bundles[bundle] = prefabs['bundles'].get(bundle, {})
        bundles['dungeon_common'] = {n: rows for n, rows in prefabs['bundles']['dungeon_common'].items() if n in COMMON or n in room_props}
        hero = prefabs['bundles'].get('hero_merchant', {}).get('HeroMerchant')
        if hero:
            bundles['hero_merchant'] = {'HeroMerchant': hero}
        weapons = {n: rows for n, rows in prefabs['bundles'].get('items', {}).items() if n in WEAPONS}
        if weapons:
            bundles['items'] = weapons
        (folder / 'prefabs.json').write_text(json.dumps({'sortingLayers': prefabs['sortingLayers'], 'bundles': bundles},
                                                        separators=(',', ':')), encoding='utf-8')
        print(f"   {culture}: {len(database['rooms'])} rooms, {sum(len(b) for b in bundles.values())} prefabs", flush=True)


if __name__ == '__main__':
    main()

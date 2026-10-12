"""The game's item masters and the new game's starting items, as the game has them.

ItemDatabase (ItemDatabase.ReadDefaultFile, from GameManager's start-up) reads its items
from two text assets in resources.assets, FullSerializer JSON with // comments, through
Resources.Load<TextAsset>:
  - "items": every item (plain ItemMaster, and by "$type" EquipmentItemMaster (armour),
    AbilityItemMaster (the dash), AmuletEquipmentMaster, the five WeaponMaster kinds,
    ConsumableItemMaster, AutoConsumableItemMaster, DecorativeItemMaster). It's read three
    times, one copy per plus level (0, and New Game+ 1 and 2: those get their goldValue and
    stats scaled by Constants' ngp*Factor values, UpgradeItemToPlusLevel; not done here);
  - "ngpitems": more weapons, appended to the plus level 1 and 2 copies only.
A DLC's items come from its bundle's items.json (ItemDatabase.ReadFromDLC): the Wanderer
DLC's, dlc/wanderer_dlc, if it's installed.

A new game's slot (GameManager.ResetSlotInfo) gets its inventory, equipment and shop chests
from GameManager's initialItems, initialEquippedItems and initialShopChestContent: item
names, serialized on the GameManager in level0. No typetrees there, so they're read from
the raw MonoBehaviour: they come after its (big) currentGameSlot, so found from the end,
whose fields are fixed in size (see initial_save).

Each item is kept as the file has it (the same values, in the same order; comments dropped,
and a number is written as Python writes it: 0.110 as 0.11); a field the file leaves out
has the C# class's default (secondaryAttackDamageMultiplier 2, the others zero or null).

Needs UnityPy.

    python tools/extract/items.py --game <the game's folder> --out <folder>
    -> <folder>/items.json
       {"items": [...], "ngpitems": [...], "dlc": {"wanderer_dlc": [...]},
        "fields": {source: {"$type": [field, ...]}},
        "initialSave": {"initialItems": [...], "initialEquippedItems": [...], "initialShopChestContent": [...]}}
"""
import argparse
import json
import struct
from pathlib import Path

import UnityPy

RESOURCE_TEXTS = ('items', 'ngpitems')
DLC_BUNDLES = {'wanderer_dlc': 'dlc/wanderer_dlc'}
GAME_MANAGER_SCENE = 'level0'
# GameManager's serialized fields after initialShopChestContent, in bytes (every field
# 4-aligned): isTownLoadedFromMainmenu, bossDefeatedCulture, isMainmenuLoadedFromGame,
# _canSave (4 each), _dungeonPortal, _townPortal, dungeonToTownPortalDoor (PPtrs, 12 each),
# debug, consoleDebug, isSceneLoadedFromEmblemPortal (4 each), portalWallWidth (4).
GAME_MANAGER_TAIL = 4 * 4 + 3 * 12 + 3 * 4 + 4


def text_of(asset):
    script = asset.m_Script
    return script if isinstance(script, str) else bytes(script).decode('utf-8')


def strip_comments(text):
    """The JSON without its // and /* */ comments (FullSerializer's parser skips them),
    strings left alone."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif text.startswith('/*', i):
            j = text.find('*/', i)
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def no_duplicates(pairs):
    keys = [k for k, _ in pairs]
    if len(set(keys)) != len(keys):
        raise SystemExit(f'an item has a field twice: {keys}')
    return dict(pairs)


def parse_items(text, what):
    items = json.loads(strip_comments(text), object_pairs_hook=no_duplicates)
    if not isinstance(items, list):
        raise SystemExit(f'{what} is not a list of items')
    return items


def fields_by_type(items):
    fields = {}
    for item in items:
        fields.setdefault(item.get('$type', 'ItemMaster'), set()).update(item)
    return {t: sorted(f) for t, f in sorted(fields.items())}


def resource_texts(game):
    env = UnityPy.load(str(Path(game) / 'Moonlighter_Data' / 'resources.assets'))
    found = {}
    for obj in env.objects:
        if obj.type.name != 'TextAsset':
            continue
        asset = obj.read()
        if asset.m_Name in RESOURCE_TEXTS:
            if asset.m_Name in found:
                raise SystemExit(f'resources.assets has two "{asset.m_Name}" texts')
            found[asset.m_Name] = text_of(asset)
    missing = [n for n in RESOURCE_TEXTS if n not in found]
    if missing:
        raise SystemExit(f"resources.assets has no {', '.join(missing)} text")
    return found


def dlc_items(bundle):
    """The bundle's items.json (what AssetBundleUtils.LoadAssetContaining finds)."""
    env = UnityPy.load(str(bundle))
    texts = []
    for obj in env.objects:
        if obj.type.name == 'AssetBundle':
            for path, info in obj.read().m_Container:
                if path.lower().endswith('items.json'):
                    texts.append((path, info.asset.read()))
    if len(texts) != 1:
        raise SystemExit(f"{bundle} has {len(texts)} items.json texts, not one")
    return text_of(texts[0][1])


def read_string(data, p):
    length = struct.unpack_from('<i', data, p)[0]
    if length < 0 or p + 4 + length > len(data):
        return None
    try:
        text = data[p + 4:p + 4 + length].decode('utf-8')
    except UnicodeDecodeError:
        return None
    return text, (p + 4 + length + 3) & ~3


def read_string_list(data, p):
    if p + 4 > len(data):
        return None
    count = struct.unpack_from('<i', data, p)[0]
    if count < 0 or count > 10000:
        return None
    p += 4
    out = []
    for _ in range(count):
        got = read_string(data, p)
        if got is None:
            return None
        out.append(got[0])
        p = got[1]
    return out, p


def initial_save(game):
    """GameManager's initialItems, initialEquippedItems and initialShopChestContent. They
    follow isTest (bool) and testScene (string), and GAME_MANAGER_TAIL bytes follow them:
    found as the offset where all of that reads and ends the object."""
    env = UnityPy.load(str(Path(game) / 'Moonlighter_Data' / GAME_MANAGER_SCENE))
    managers = []
    for obj in env.objects:
        if obj.type.name != 'MonoBehaviour':
            continue
        try:
            script = obj.read(check_read=False).m_Script.read()
        except Exception:
            continue
        if script.m_ClassName == 'GameManager':
            managers.append(bytes(obj.get_raw_data()))
    if len(managers) != 1:
        raise SystemExit(f'{GAME_MANAGER_SCENE} has {len(managers)} GameManagers, not one')
    data = managers[0]
    # Zeros read as empty lists and strings too, so a later offset inside a run of empty
    # strings can fit as well: the first fit is theirs (an offset a field earlier reads the
    # lists one field off, which doesn't end the object unless they're all empty).
    for p in range(0, len(data) - 8, 4):
        if struct.unpack_from('<i', data, p)[0] not in (0, 1):  # isTest
            continue
        got = read_string(data, p + 4)  # testScene
        if got is None:
            continue
        lists, q = [], got[1]
        for _ in range(3):
            got = read_string_list(data, q)
            if got is None:
                break
            lists.append(got[0])
            q = got[1]
        if len(lists) == 3 and q + GAME_MANAGER_TAIL == len(data):
            return {'scene': GAME_MANAGER_SCENE, 'initialItems': lists[0],
                    'initialEquippedItems': lists[1], 'initialShopChestContent': lists[2]}
    raise SystemExit("GameManager's initial save lists weren't found")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    game = Path(args.game)
    texts = resource_texts(game)
    result = {name: parse_items(texts[name], name) for name in RESOURCE_TEXTS}
    result['dlc'] = {}
    for name, path in DLC_BUNDLES.items():
        if (game / path).exists():
            result['dlc'][name] = parse_items(dlc_items(game / path), f'{path} items.json')
    result['fields'] = {name: fields_by_type(result[name]) for name in RESOURCE_TEXTS}
    result['fields'].update({f'dlc/{name}': fields_by_type(items) for name, items in result['dlc'].items()})
    result['initialSave'] = initial_save(game)
    names = {item['name'] for item in result['items']}
    unknown = [n for key in ('initialItems', 'initialEquippedItems', 'initialShopChestContent')
               for n in result['initialSave'][key] if n and n not in names]
    if unknown:
        raise SystemExit(f'the initial save names items "items" has not: {unknown}')
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / 'items.json').write_text(json.dumps(result, indent=1, ensure_ascii=False), encoding='utf-8')
    counts = ', '.join([f'{n} {len(result[n])}' for n in RESOURCE_TEXTS] +
                       [f'{n} {len(i)}' for n, i in result['dlc'].items()])
    print(f'   items: {counts}; equipped at the start: {result["initialSave"]["initialEquippedItems"]}', flush=True)


if __name__ == '__main__':
    main()

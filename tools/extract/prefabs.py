"""The prefabs in the dungeon and enemy bundles, flattened: for every root object, each
object below it (itself included) as the scene holds it after Instantiate: its path below
the root, whether it's active, its local position and scale, and its SpriteRenderer
(sprite, sorting layer, order, flips, colour, enabled). The same rows the oracle's floor
snapshot has, so what the port builds from these can be checked against the game.

Also the chests PrefabRegister holds (sharedassets0.assets: WoodChest ... MagicChest and
the other dungeon chests), and the sorting layers' names in their drawing order (the
TagManager's).

Needs UnityPy.

    python tools/extract/prefabs.py --game <the game's folder> --out <folder>
    -> <folder>/prefabs.json  {"sortingLayers": [...], "bundles": {bundle: {prefab: [rows]}}}
       row: [path, active, x, y, scale x, scale y, sprite, layer, order, flipX, flipY, rgba, enabled,
            rotation z (degrees), behaviours: [[class, {fields}], ...],
            [game object layer, tag, colliders: [[type, enabled, trigger, offset x, offset y, ...]]]]
       (fields only for the classes in FIELDS: what their Start() draws depend on; a collider
       ends with size x, size y for a box, the radius for a circle, the paths for a polygon)
"""
import argparse
import json
import math
from pathlib import Path

import UnityPy

BUNDLE_PREFIXES = ('dungeon_', 'enemies_')
# PrefabRegister's prefabs the dungeon spawns by name (SpawnChest), from the player's files.
PLAYER_FILE = 'sharedassets0.assets'
# MonoBehaviours whose Start() draws from Random when a room comes to life, and the fields
# that decide how many draws and what they pick.
FIELDS = {
    'BreakableDetail': ('destroyAtStartProb',),
    'GolemKingRockBehaviour': ('RandomRockSprites',),
    'BreakableObjectBehaviour': ('skins', 'currentSkin'),
    'BreakableBoxBehaviour': ('skins', 'currentSkin'),  # a BreakableObjectBehaviour (inherits its Start)
    'CarnivorousMutaeBossRoots': (),
    # Not a draw: sets its renderer's sortingOrder from the world y in Start (what the scene draws in front).
    'SpriteDepthOrderRender': ('offset', 'forceOrderValue', 'forcedOrderValue', 'over', 'overOffset'),
}
PLAYER_PREFABS = ('WoodChest', 'IronChest', 'GoldChest', 'MagicChest', 'BossChest', 'DungeonToShopChest', 'TimedChestSlime')


def tag_manager(game):
    env = UnityPy.load(str(Path(game) / 'Moonlighter_Data' / 'globalgamemanagers'))
    for obj in env.objects:
        if obj.type.name == 'TagManager':
            return obj.read_typetree()
    return {}


def sorting_layers(manager):
    return [(layer['name'], layer['uniqueID']) for layer in manager.get('m_SortingLayers', [])]


# GameObject.m_Tag: Unity's built-in tags by number, the project's from 20000 on.
BUILTIN_TAGS = {0: 'Untagged', 1: 'Respawn', 2: 'Finish', 3: 'EditorOnly', 5: 'MainCamera', 6: 'Player', 7: 'GameController'}


def tag_name(manager, tag):
    if tag >= 20000:
        tags = manager.get('tags', [])
        return tags[tag - 20000] if tag - 20000 < len(tags) else str(tag)
    return BUILTIN_TAGS.get(tag, str(tag))


def collider_row(kind, c):
    off = c.get('m_Offset', {'x': 0.0, 'y': 0.0})
    row = [kind, bool(c.get('m_Enabled', 1)), bool(c.get('m_IsTrigger', 0)), off['x'], off['y']]
    if kind == 'BoxCollider2D':
        row += [c['m_Size']['x'], c['m_Size']['y']]
    elif kind == 'CircleCollider2D':
        row += [c['m_Radius']]
    elif kind == 'PolygonCollider2D':
        row += [[[[p['x'], p['y']] for p in path] for path in c['m_Points']['m_Paths']]]
    return row


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    manager = tag_manager(args.game)
    layers = sorting_layers(manager)
    layer_name = {uid & 0xFFFFFFFF: name for name, uid in layers}
    bundles_dir = Path(args.game) / 'Moonlighter_Data' / 'StreamingAssets' / 'AssetBundles'
    out = {'sortingLayers': [name for name, _ in layers], 'bundles': {}}
    total = 0
    files = [b for b in sorted(bundles_dir.iterdir()) if b.name.startswith(BUNDLE_PREFIXES) and b.suffix != '.manifest']
    files.append(Path(args.game) / 'Moonlighter_Data' / PLAYER_FILE)
    for bundle in files:
        only = PLAYER_PREFABS if bundle.name == PLAYER_FILE else None
        env = UnityPy.load(str(bundle))
        by_id = {obj.path_id: obj for obj in env.objects}
        trees = {}

        def tree(path_id):
            if path_id not in trees:
                trees[path_id] = by_id[path_id].read_typetree() if path_id in by_id else None
            return trees[path_id]

        # Prefabs the game references directly win over others of their name (dungeon_common
        # has two DungeonRoom prefabs; the room database's dungeonRoomPrefab is the one used).
        referenced = set()
        for obj in env.objects:
            if obj.type.name == 'MonoBehaviour':
                try:
                    mb = obj.read_typetree()
                except Exception:  # noqa: BLE001 - no type tree
                    continue
                ref = mb.get('dungeonRoomPrefab') if isinstance(mb, dict) else None
                if isinstance(ref, dict) and ref.get('m_FileID') == 0:
                    referenced.add(ref['m_PathID'])

        def components(go):
            for c in go['m_Component']:
                ref = c['component']
                if ref['m_FileID'] == 0 and ref['m_PathID'] in by_id:
                    yield by_id[ref['m_PathID']]

        sprite_names = {}

        def sprite_name(ref):
            if ref['m_PathID'] == 0:
                return None
            if ref['m_FileID'] != 0:
                return '?external'
            if ref['m_PathID'] not in sprite_names:
                obj = by_id.get(ref['m_PathID'])
                sprite_names[ref['m_PathID']] = obj.read().m_Name if obj is not None else None
            return sprite_names[ref['m_PathID']]

        class_names = {}

        def class_name(ref):
            key = (ref['m_FileID'], ref['m_PathID'])
            if key not in class_names:
                obj = by_id.get(ref['m_PathID']) if ref['m_FileID'] == 0 else None
                class_names[key] = obj.read().m_ClassName if obj is not None else None
            return class_names[key]

        def field(value):
            if isinstance(value, dict) and 'm_PathID' in value:
                return value['m_PathID'] != 0
            if isinstance(value, list):
                return len(value)
            return value

        prefabs = {}
        for obj in env.objects:
            if obj.type.name not in ('Transform', 'RectTransform'):
                continue
            t = tree(obj.path_id)
            if t['m_Father']['m_PathID'] != 0:
                continue  # not a root
            root_name = tree(t['m_GameObject']['m_PathID'])['m_Name']
            if only is not None and root_name not in only:
                continue
            rows = []

            # The root's own row has path '', the rest paths below it.
            def walk_node(transform_id, path):
                tr = tree(transform_id)
                go = tree(tr['m_GameObject']['m_PathID'])
                pos, scale = tr['m_LocalPosition'], tr['m_LocalScale']
                row = [path, bool(go['m_IsActive']), pos['x'], pos['y'], scale['x'], scale['y'], None, None, 0, False, False, None, False]
                for comp in components(go):
                    if comp.type.name == 'SpriteRenderer':
                        sr = tree(comp.path_id)
                        col = sr.get('m_Color', {'r': 1, 'g': 1, 'b': 1, 'a': 1})
                        # (the layer's ID is signed in the renderer, unsigned in the TagManager)
                        row[6:] = [sprite_name(sr['m_Sprite']), layer_name.get(sr.get('m_SortingLayerID', 0) & 0xFFFFFFFF, 'Default'),
                                   sr.get('m_SortingOrder', 0), bool(sr.get('m_FlipX', 0)), bool(sr.get('m_FlipY', 0)),
                                   [col['r'], col['g'], col['b'], col['a']], bool(sr.get('m_Enabled', 1))]
                q = tr['m_LocalRotation']
                row.append(math.degrees(2.0 * math.atan2(q['z'], q['w'])))  # 2D: the rotation about z
                behaviours = []
                for comp in components(go):
                    if comp.type.name != 'MonoBehaviour':
                        continue
                    try:
                        mb = tree(comp.path_id)
                    except Exception:  # noqa: BLE001 - no type tree: the class name alone
                        mb = None
                    if mb is None:
                        continue
                    cls = class_name(mb['m_Script'])
                    behaviours.append([cls, {f: field(mb.get(f)) for f in FIELDS.get(cls, ())}])
                row.append(behaviours)
                colliders = [collider_row(comp.type.name, tree(comp.path_id)) for comp in components(go)
                             if comp.type.name in ('BoxCollider2D', 'CircleCollider2D', 'PolygonCollider2D')]
                row.append([go['m_Layer'], tag_name(manager, go['m_Tag']), colliders])
                rows.append(row)
                for child in tr['m_Children']:
                    ctr = tree(child['m_PathID'])
                    if ctr is None:
                        continue
                    cname = tree(ctr['m_GameObject']['m_PathID'])['m_Name']
                    walk_node(child['m_PathID'], cname if path == '' else f'{path}/{cname}')

            walk_node(obj.path_id, '')
            if t['m_GameObject']['m_PathID'] in referenced:
                prefabs[root_name] = rows
            else:
                prefabs.setdefault(root_name, rows)  # first of a name wins (the game's lookups take the first)
        out['bundles'][bundle.name] = prefabs
        total += len(prefabs)
        print(f'   {bundle.name}: {len(prefabs)} prefabs', flush=True)
    target = Path(args.out) / 'prefabs.json'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(out), encoding='utf-8')
    print(f'   {total} prefabs, {len(layers)} sorting layers', flush=True)


if __name__ == '__main__':
    main()

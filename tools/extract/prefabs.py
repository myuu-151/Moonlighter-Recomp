"""The prefabs in the dungeon and enemy bundles, flattened: for every root object, each
object below it (itself included) as the scene holds it after Instantiate: its path below
the root, whether it's active, its local position and scale, and its SpriteRenderer
(sprite, sorting layer, order, flips, colour, enabled). The same rows the oracle's floor
snapshot has, so what the port builds from these can be checked against the game.

Also the sorting layers' names in their drawing order (the TagManager's).

Needs UnityPy.

    python tools/extract/prefabs.py --game <the game's folder> --out <folder>
    -> <folder>/prefabs.json  {"sortingLayers": [...], "bundles": {bundle: {prefab: [rows]}}}
       row: [path, active, x, y, scale x, scale y, sprite, layer, order, flipX, flipY, rgba, enabled]
"""
import argparse
import json
from pathlib import Path

import UnityPy

BUNDLE_PREFIXES = ('dungeon_', 'enemies_')


def sorting_layers(game):
    env = UnityPy.load(str(Path(game) / 'Moonlighter_Data' / 'globalgamemanagers'))
    for obj in env.objects:
        if obj.type.name == 'TagManager':
            tree = obj.read_typetree()
            return [(layer['name'], layer['uniqueID']) for layer in tree.get('m_SortingLayers', [])]
    return []


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    layers = sorting_layers(args.game)
    layer_name = {uid: name for name, uid in layers}
    bundles_dir = Path(args.game) / 'Moonlighter_Data' / 'StreamingAssets' / 'AssetBundles'
    out = {'sortingLayers': [name for name, _ in layers], 'bundles': {}}
    total = 0
    for bundle in sorted(bundles_dir.iterdir()):
        if not bundle.name.startswith(BUNDLE_PREFIXES) or bundle.suffix == '.manifest':
            continue
        env = UnityPy.load(str(bundle))
        by_id = {obj.path_id: obj for obj in env.objects}
        trees = {}

        def tree(path_id):
            if path_id not in trees:
                trees[path_id] = by_id[path_id].read_typetree() if path_id in by_id else None
            return trees[path_id]

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

        prefabs = {}
        for obj in env.objects:
            if obj.type.name != 'Transform':
                continue
            t = tree(obj.path_id)
            if t['m_Father']['m_PathID'] != 0:
                continue  # not a root
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
                        row[6:] = [sprite_name(sr['m_Sprite']), layer_name.get(sr.get('m_SortingLayerID', 0), 'Default'),
                                   sr.get('m_SortingOrder', 0), bool(sr.get('m_FlipX', 0)), bool(sr.get('m_FlipY', 0)),
                                   [col['r'], col['g'], col['b'], col['a']], bool(sr.get('m_Enabled', 1))]
                rows.append(row)
                for child in tr['m_Children']:
                    ctr = tree(child['m_PathID'])
                    if ctr is None:
                        continue
                    cname = tree(ctr['m_GameObject']['m_PathID'])['m_Name']
                    walk_node(child['m_PathID'], cname if path == '' else f'{path}/{cname}')

            walk_node(obj.path_id, '')
            root_name = tree(t['m_GameObject']['m_PathID'])['m_Name']
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

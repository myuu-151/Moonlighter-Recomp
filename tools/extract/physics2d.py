"""The 2D physics settings (globalgamemanagers' Physics2DSettings): the layer collision
matrix, which layers' colliders meet (and what Physics2D queries and contacts skip), and
the solver's settings; with the layers' names (the TagManager's).

    python tools/extract/physics2d.py --game <the game's folder> --out <folder>
    -> <folder>/physics2d.json
       {"layers": [32 names, "" unnamed],
        "layerCollisionMatrix": [32 row bitmasks: bit j of row i set = layers i and j collide],
        "gravity": [x, y], "velocityIterations", "positionIterations", "queriesHitTriggers",
        "queriesStartInColliders", "callbacksOnDisable", ... (the rest of the settings, their
        m_ dropped; defaultMaterial: whether one is set)}

Needs UnityPy.
"""
import argparse
import json
from pathlib import Path

import UnityPy


def key(name):
    name = name[2:] if name.startswith('m_') else name
    return name[0].lower() + name[1:]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    env = UnityPy.load(str(Path(args.game) / 'Moonlighter_Data' / 'globalgamemanagers'))
    settings = manager = None
    for obj in env.objects:
        if obj.type.name == 'Physics2DSettings':
            settings = obj.read_typetree()
        elif obj.type.name == 'TagManager':
            manager = obj.read_typetree()
    if settings is None or manager is None:
        raise SystemExit('globalgamemanagers has no Physics2DSettings or TagManager')
    matrix = [m & 0xFFFFFFFF for m in settings['m_LayerCollisionMatrix']]
    if len(matrix) != 32:
        raise SystemExit(f'the layer collision matrix has {len(matrix)} rows, not 32')
    asymmetric = [(i, j) for i in range(32) for j in range(i) if (matrix[i] >> j & 1) != (matrix[j] >> i & 1)]
    if asymmetric:  # Unity keeps it symmetric (Physics2D.IgnoreLayerCollision sets both)
        raise SystemExit(f'the layer collision matrix is not symmetric: layers {asymmetric[:8]}')
    out = {'layers': list(manager['layers']), 'layerCollisionMatrix': matrix}
    for name, value in settings.items():
        if name == 'm_LayerCollisionMatrix':
            continue
        if isinstance(value, dict) and 'm_PathID' in value:
            value = value['m_PathID'] != 0
        elif isinstance(value, dict) and set(value) == {'x', 'y'}:
            value = [value['x'], value['y']]
        elif isinstance(value, dict):
            value = {key(k): v for k, v in value.items()}
        out[key(name)] = value
    target = Path(args.out) / 'physics2d.json'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(out, indent=1), encoding='utf-8')
    named = sum(1 for n in out['layers'] if n)
    print(f'   {named} named layers, collision matrix, {len(out) - 2} settings', flush=True)


if __name__ == '__main__':
    main()

"""Extract the dungeons' DetailsPrefabRegister assets (one per culture and floor):
wall/particle/creature/breakable prefab names, the floor-layer sprites (name and texture
size, which SpawnDetails uses to fit middle-layer sprites in their zone) and the
generation-probability images (red channel, GetPixels32 order: bottom row first).

Needs UnityPy.

    python tools/extract/details.py --game <the game's folder> --out <folder>
    -> <folder>/details.json
"""
import argparse
import base64
import json
import os
import sys

import UnityPy

CULTURES = {"Golem": 1, "Forest": 2, "Desert": 3, "Tech": 4, "Unknown": 5}


def deref(pptr):
    if pptr is None or pptr.m_PathID == 0:
        return None
    try:
        return pptr.read()
    except Exception:  # noqa: BLE001 - unresolvable reference counts as missing
        return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    BUNDLES = os.path.join(args.game, "Moonlighter_Data", "StreamingAssets", "AssetBundles")
    OUT = os.path.join(args.out, "details.json")
    os.makedirs(args.out, exist_ok=True)
    env = UnityPy.load(*[os.path.join(BUNDLES, n) for n in os.listdir(BUNDLES) if n.startswith("dungeon_") and not n.endswith(".manifest")])
    out = []
    for obj in env.objects:
        if obj.type.name != "MonoBehaviour":
            continue
        try:
            mb = obj.read()
        except Exception:  # noqa: BLE001
            continue
        if not hasattr(mb, "wallDetails"):
            continue
        name = mb.m_Name  # e.g. GolemDetails0
        culture = next((c for c in CULTURES if name.startswith(c + "Details")), None)
        if culture is None:
            print("skip", name, file=sys.stderr)
            continue
        level = int(name[len(culture) + len("Details"):])

        def objects(lst):
            return [None if (o := deref(p)) is None else o.m_Name for p in lst]

        def sprites(lst):
            res = []
            for p in lst:
                s = deref(p)
                if s is None:
                    res.append(None)
                    continue
                tex = s.m_RD.texture.read()
                res.append({"name": s.m_Name, "texture": tex.m_Name, "w": tex.m_Width, "h": tex.m_Height})
            return res

        def probs(lst):
            res = []
            for p in lst:
                s = deref(p)
                if s is None:
                    res.append(None)
                    continue
                tex = s.m_RD.texture.read()
                img = tex.image.convert("RGBA")
                w, h = img.size
                px = img.load()
                red = bytes(px[x, h - 1 - y][0] for y in range(h) for x in range(w))
                res.append({"name": s.m_Name, "w": w, "h": h, "red": base64.b64encode(red).decode()})
            return res

        out.append({
            "culture": CULTURES[culture], "level": level, "name": name,
            "walls": objects(mb.wallDetails), "particles": objects(mb.particlesDetails),
            "creatures": objects(mb.creatureDetails), "breakables": objects(mb.breakableDetails),
            "lower": sprites(mb.lowerLayerDetails), "lower2": sprites(mb.lower2LayerDetails), "middle": sprites(mb.middleLayerDetails),
            "lowerProb": probs(mb.lowerGenerationProbabilityImages), "lower2Prob": probs(mb.lower2GenerationProbabilityImages),
        })
    out.sort(key=lambda d: (d["culture"], d["level"]))
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1)
    print("  ", len(out), "registers:", ", ".join(d["name"] for d in out), flush=True)


if __name__ == "__main__":
    main()

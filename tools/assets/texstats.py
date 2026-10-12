"""Per-texture stats to judge downscaling / format options for a GameCube port.

For each Texture2D: occupied 8x8 tiles (repack estimate), colour count (CI4/CI8 fit),
whether it is an exact 2x upscale (lossless halving), and the error of a 2x
box-downscale + nearest upscale (how visible halving would be).
"""
import os, sys, csv, gc
import numpy as np
import UnityPy

ROOT = r"C:\Users\NoSig\Documents\moonlighter\Moonlighter"
DATA = os.path.join(ROOT, "Moonlighter_Data")
OUT = sys.argv[1]

sources = [(f, os.path.join(DATA, f)) for f in ["resources.assets", "sharedassets0.assets", "sharedassets3.assets"]]
ab = os.path.join(DATA, "StreamingAssets", "AssetBundles")
for f in sorted(os.listdir(ab)):
    if not f.endswith(".manifest") and f != "AssetBundles":
        sources.append(("bundle:" + f, os.path.join(ab, f)))
sources.append(("dlc:wanderer_dlc", os.path.join(ROOT, "dlc", "wanderer_dlc")))


def stats(a):
    h, w = a.shape[:2]
    alpha = a[..., 3]
    vis = alpha > 0
    # occupied 8x8 tiles
    ph, pw = (h + 7) // 8 * 8, (w + 7) // 8 * 8
    v = np.zeros((ph, pw), bool); v[:h, :w] = vis
    tiles = v.reshape(ph // 8, 8, pw // 8, 8).any(axis=(1, 3)).sum()
    # colour count over visible pixels (premultiply-free, exact RGBA)
    px = a[vis]
    ncol = len(np.unique(px.view(np.uint32))) if len(px) else 0
    # exact 2x upscale check at any of the 4 alignments
    is2x = False
    for oy in (0, 1):
        for ox in (0, 1):
            b = a[oy:oy + (h - oy) // 2 * 2, ox:ox + (w - ox) // 2 * 2]
            if b.size == 0:
                continue
            if (np.all(b[0::2, 0::2] == b[1::2, 0::2]) and np.all(b[0::2, 0::2] == b[0::2, 1::2])
                    and np.all(b[0::2, 0::2] == b[1::2, 1::2])):
                is2x = True
    # error of 2x box downscale -> nearest upscale, over visible pixels (RGB, alpha-weighted)
    hh, ww = h // 2 * 2, w // 2 * 2
    f = a[:hh, :ww].astype(np.float32)
    d = (f[0::2, 0::2] + f[1::2, 0::2] + f[0::2, 1::2] + f[1::2, 1::2]) / 4
    u = d.repeat(2, 0).repeat(2, 1)
    wgt = (f[..., 3] + u[..., 3]) / 510.0
    err = np.abs(f - u)
    e = (err[..., :3].mean(-1) * wgt + err[..., 3] * 0.5).sum() / max(wgt.sum(), 1)
    return tiles * 64, ncol, is2x, float(e), int(vis.sum())


with open(OUT, "w", newline="") as fh:
    wr = csv.writer(fh)
    wr.writerow(["source", "name", "w", "h", "fmt", "tile_px", "visible_px", "colors", "is2x", "halve_err"])
    seen = set()
    for src, path in sources:
        try:
            env = UnityPy.load(path)
        except Exception as e:
            print("skip", src, e, flush=True); continue
        n = 0
        for obj in env.objects:
            if obj.type.name != "Texture2D":
                continue
            try:
                t = obj.read()
                key = (t.m_Name, t.m_Width, t.m_Height)
                if key in seen or t.m_Width == 0:
                    continue
                seen.add(key)
                a = np.asarray(t.image.convert("RGBA"))
                tp, vp, nc, x2, e = stats(a)
                wr.writerow([src, t.m_Name, t.m_Width, t.m_Height, str(t.m_TextureFormat).split(".")[-1], tp, vp, nc, int(x2), round(e, 2)])
                n += 1
            except Exception as ex:
                print("err", src, getattr(t, "m_Name", "?"), ex, flush=True)
        print(src, n, "unique textures", flush=True)
        del env; gc.collect()

import csv, os, re, collections, statistics
P = os.path.join(os.path.dirname(__file__), "..", "..", "data", "texstats.csv")
rows = list(csv.DictReader(open(P)))
for r in rows:
    # texstats.py wrote stats() results shifted: header visible_px,colors,is2x,halve_err
    # actually hold colors,is2x,halve_err,visible_px
    r["colors"], r["is2x"], r["halve_err"], r["visible_px"] = r["visible_px"], r["colors"], r["is2x"], r["halve_err"]
    for k in ("w", "h", "tile_px", "visible_px", "colors", "is2x"):
        r[k] = int({"True": 1, "False": 0}.get(r[k], r[k]))
    r["halve_err"] = float(r["halve_err"])
    r["soft"] = bool(re.search(r"light|shadow|glow|fog|gradient|vignette|halo", r["name"], re.I))
MB = 2 ** 20
tile = sum(r["tile_px"] for r in rows); allpx = sum(r["w"] * r["h"] for r in rows)
bucket = lambda r: "<=16" if r["colors"] <= 16 else "<=256" if r["colors"] <= 256 else ">256"

print(f"textures {len(rows)}  stored RGBA32 {allpx*4/MB:.0f} MB")
print(f"occupied 8x8 tiles = {tile/allpx*100:.0f}% of texture area")
ca = collections.Counter()
for r in rows: ca[bucket(r)] += r["tile_px"]
print("colour buckets, % of occupied area:", {k: round(v / tile * 100) for k, v in ca.items()})
print(f"exact 2x upscales: {sum(r['is2x'] for r in rows)} textures, {sum(r['tile_px'] for r in rows if r['is2x'])/tile*100:.1f}% of area")
soft = [r for r in rows if r["soft"]]
print(f"light/shadow/glow layers: {len(soft)} textures, {sum(r['tile_px'] for r in soft)/tile*100:.0f}% of area")
for lab, sub in [("soft", soft), ("other", [r for r in rows if not r["soft"]])]:
    es = sorted(r["halve_err"] for r in sub)
    print(f"  halving error {lab:6s}: p25 {es[len(es)//4]:.1f}  median {statistics.median(es):.1f}  p75 {es[3*len(es)//4]:.1f}")
big = sorted([r for r in rows if bucket(r) == ">256" and not r["soft"]], key=lambda r: -r["tile_px"])[:12]
print("largest >256-colour non-soft:", [(r["name"], r["w"], r["h"], r["colors"], r["halve_err"]) for r in big])

def cost(r, mode):
    px = r["tile_px"]
    b = 0.5 if r["colors"] <= 16 else 1.0
    if mode == "1x":
        return px * b
    if mode == "half":
        return px / 4 * b
    if mode == "smart":
        if r["is2x"]: return px / 4 * b
        if r["soft"]: return px / 16 * 0.5          # quarter-res I4/IA4, bilinear on GPU
        return px * b
groups = collections.defaultdict(list)
for r in rows: groups[r["source"]].append(r)
print(f"\n{'source':26s} {'stored':>8s} {'1x repack':>10s} {'smart':>8s} {'all 1/2':>8s}   (MB; CI4/CI8, 8x8-tile repack)")
tot = collections.Counter()
for s in sorted(groups, key=lambda s: -sum(cost(r, '1x') for r in groups[s])):
    g = groups[s]
    v = [sum(r["w"] * r["h"] * 4 for r in g)] + [sum(cost(r, m) for r in g) for m in ("1x", "smart", "half")]
    for i, x in enumerate(v): tot[i] += x
    print(f"{s:26s} " + " ".join(f"{x/MB:8.1f}" for x in v))
print(f"{'TOTAL':26s} " + " ".join(f"{tot[i]/MB:8.1f}" for i in range(4)))

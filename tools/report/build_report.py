"""Builds the dungeon verification report (a single HTML page) from oracle data.

Inputs (data/oracle): trace.jsonl, cave_game.jsonl; test executables in recomp/build/Release.
Output: data/report/dungeon_report.html (template: tools/report/template.html, with the
collected data embedded as JSON).

Usage: python tools/report/build_report.py
"""
import collections
import datetime
import json
import os
import re
import struct
import subprocess

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ORACLE = os.path.join(ROOT, "data", "oracle")
EXTRACTED = os.path.join(ROOT, "data", "extracted")
BUILD = os.path.join(ROOT, "recomp", "build", "Release")
OUT_DIR = os.path.join(ROOT, "data", "report")
CULTURES = {1: "Golem", 2: "Forest", 3: "Desert", 4: "Tech", 5: "Unknown"}


M32 = 0xFFFFFFFF


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def bits_to_f32(b):
    return struct.unpack("<f", struct.pack("<i", b))[0]


class UnityRandom:
    """UnityEngine.Random (xorshift128), for replaying a recorded state in the report."""

    def __init__(self, state):
        self.s = list(state)

    def next(self):
        s = self.s
        t = s[0] ^ ((s[0] << 11) & M32)
        s[0], s[1], s[2] = s[1], s[2], s[3]
        s[3] = (s[3] ^ (s[3] >> 19) ^ t ^ (t >> 8)) & M32
        return s[3]

    def value(self):
        return f32((self.next() & 0x7FFFFF) / 8388607.0)

    def range_int(self, a, b):
        if a < b:
            return a + self.next() % (b - a)
        if a > b:
            return a - self.next() % (a - b)
        return a


def fixed_drops(cfg):
    """Chest.GenerateFixedDrop from the recorded state: each roll and what it produced."""
    rng = UnityRandom(cfg["state"])
    inc = bits_to_f32(cfg["dropIncrement"])
    rolls = []
    for d in cfg["fixedDrop"]:
        v = rng.value()
        prob = bits_to_f32(d["prob"])
        threshold = f32(prob * f32(1.0 + inc))
        hit = (d["ngpOnly"] and cfg["ngp"] and v < threshold) or (not d["ngpOnly"] and v < threshold)
        qty = 0
        if hit and not d["ownedAmulet"]:
            qty = rng.range_int(max(1, d["min"]), d["max"] + 1)
        rolls.append({"item": d["item"], "prob": round(threshold, 4), "qty": qty, "ownedAmulet": d["ownedAmulet"], "ngpOnly": d["ngpOnly"]})
    return rolls


def read_jsonl(path):
    if not os.path.exists(path):
        return []
    with open(path, encoding="utf-8") as f:
        return [json.loads(line) for line in f if line.strip()]


def run_test(exe, *args):
    path = os.path.join(BUILD, exe)
    try:
        out = subprocess.run([path, *args], capture_output=True, text=True, timeout=600).stdout
    except Exception as e:  # noqa: BLE001 - surfaced in the report
        return {"exe": exe, "error": str(e), "rows": [], "notes": [], "verdict": "NOT RUN"}
    rows, notes, draws = [], [], []
    for line in out.splitlines():
        m = re.match(r"^(\S.*?)\s{2,}(\d+)\s+(?:pass|match)\s+(\d+)\s+(?:fail|differ)", line)
        if m:
            rows.append({"name": m.group(1).strip(), "pass": int(m.group(2)), "fail": int(m.group(3))})
            continue
        m = re.match(r"^\s{2}(\S.*?)\s{2,}(\d+)-(\d+)\s+draws\s+\((\d+) samples", line)
        if m:
            draws.append({"what": m.group(1).strip(), "min": int(m.group(2)), "max": int(m.group(3)), "n": int(m.group(4))})
            continue
        if line.startswith("    ") or line.startswith("  ") and ("mismatch" in line or "floor" in line):
            notes.append(line.strip())
    verdict = "ALL MATCH" if "ALL MATCH" in out else ("NO DATA" if "NO" in out.splitlines()[-1] else "MISMATCHES")
    return {"exe": exe, "rows": rows, "notes": notes[:20], "draws": draws, "verdict": verdict}


def floors_from_trace(trace, caves):
    by_state = collections.defaultdict(list)
    for c in caves:
        by_state[json.dumps(c["stateAtCaveInit"])].append(c)

    def cave_for(session, state):
        # Harvests reuse seeds: take the first matching record written after the session began.
        start = f"{session[0:4]}-{session[4:6]}-{session[6:8]}T{session[9:11]}:{session[11:13]}:{session[13:15]}"
        for c in by_state.get(json.dumps(state), []):
            if c.get("time", "")[:19] >= start:
                return c
        return None
    floors = collections.OrderedDict()
    harvest_meta = {}
    for e in trace:
        key = (e["session"], e["floor"])
        if e["ev"] == "harvestFloor":
            harvest_meta[(e["session"], e["floor"] + 1)] = e  # announced before the floor begins
        floors.setdefault(key, []).append(e)

    out = []
    for (session, number), events in floors.items():
        begin = next((e for e in events if e["ev"] == "floorBegin"), None)
        if not begin:
            continue
        cave = cave_for(session, begin["state"])
        if not cave or not cave.get("layout"):
            continue
        L = cave["layout"]
        minx, miny = L["minPos"], L["maxPos"]  # bits; recomputed below from rooms
        rooms_ev = [e for e in events if e["ev"] == "room"]
        # cell -> placed rooms (a secret room shares its parent's cell)
        cells = collections.defaultdict(list)
        for r in rooms_ev:
            if "x" in r:
                cells[(r["x"], r["y"])].append(r)

        def f2(b):
            import struct
            return struct.unpack("<f", struct.pack("<i", b))[0]

        cave_rooms = L["rooms"]
        mn = (f2(L["minPos"][0]), f2(L["minPos"][1]))
        special = L["special"]
        role = {}
        for k, v in special.items():
            if isinstance(v, int) and v >= 0:
                role[v] = k.replace("CaveRoom", "")
        for k in ("chestCaveRooms", "timedChestCaveRooms"):
            for v in special.get(k, []):
                role.setdefault(v, "chest")

        stage_by_room = collections.defaultdict(list)
        for e in events:
            if e["ev"] in ("stage", "result") and e.get("room", -1) >= 0:
                stage_by_room[e["room"]].append(e)

        # Chest.Awake runs inside SpawnChest: attach each recorded chest config to the
        # room/tile of the SpawnChest call that created it.
        chest_cfg = {}
        last_spawn = None
        for e in events:
            if e["ev"] == "stage" and e["name"] == "DungeonRoom.SpawnChest" and e["phase"] == "pre" and e.get("args") and isinstance(e["args"][0], int):
                last_spawn = (e["room"], e["args"][0])
            elif e["ev"] == "chestConfig" and last_spawn is not None:
                chest_cfg[last_spawn] = e
                last_spawn = None

        def room_detail(rid):
            ev = stage_by_room.get(rid, [])
            grid = next((e.get("grid") for e in ev if e["ev"] == "stage" and e["name"] == "DungeonRoom.LoadGridFromInfo" and e["phase"] == "post"), None)
            enemies = []
            for e in ev:
                if e["ev"] == "stage" and e["name"] == "DungeonRoom.SpawnEnemy" and e["phase"] == "pre" and e.get("args"):
                    a = e["args"]
                    name = (a[0] or "").split("@")[0]
                    enemies.append({"name": name, "tile": a[1], "champion": bool(a[2]) if len(a) > 2 else False})
            chests = []
            names = [e.get("value") for e in ev if e["ev"] == "result" and e["name"] == "DungeonRoom.GetChestNameByFloor"]
            # SpawnChest(tile, name) calls SpawnChest(tile, prefab): both log, keep one per call.
            tiles = [e["args"][0] for e in ev if e["ev"] == "stage" and e["name"] == "DungeonRoom.SpawnChest" and e["phase"] == "pre"
                     and e.get("args") and isinstance(e["args"][0], int)]
            tiles = [t for i, t in enumerate(tiles) if i == 0 or tiles[i - 1] != t]
            for i, n in enumerate(names):
                tile = tiles[i] if i < len(tiles) else None
                chest = {"name": n, "tile": tile, "mimic": "Mimic" in (n or "")}
                cfg = chest_cfg.get((rid, tile))
                if cfg:
                    chest["capacity"] = cfg["capacity"]
                    chest["randomContents"] = cfg["generateRandomContentOnInit"]
                    chest["rolls"] = fixed_drops(cfg)
                chests.append(chest)
            return {"grid": grid, "enemies": enemies, "chests": chests}

        rooms = []
        for idx, cr in enumerate(cave_rooms):
            x = int(round(f2(cr["p"][0]) - mn[0]))
            y = int(round(f2(cr["p"][1]) - mn[1]))
            units = []
            for u in cr["u"]:
                ux = int(round(f2(u["l"][0])))
                uy = int(round(f2(u["l"][1])))
                units.append({"dx": ux, "dy": uy, "n": u["n"], "s": u["s"], "e": u["e"], "w": u["w"]})
            placed = cells.get((x, y), [])
            entry = {"i": idx, "x": x, "y": y, "type": cr["t"], "role": role.get(idx, ""), "units": units, "placed": []}
            for p in placed:
                d = room_detail(p["room"])
                entry["placed"].append({"id": p["room"], "name": p.get("name"), "culture": p.get("culture"), **d})
            rooms.append(entry)

        meta = harvest_meta.get((session, number), {})
        out.append({
            "session": session, "floor": number, "culture": CULTURES.get(cave["culture"], str(cave["culture"])),
            "level": cave["level"], "run": meta.get("run"), "seed": meta.get("seed"),
            "source": "harvest" if meta else "play", "rooms": rooms,
            "picks": sum(1 for e in events if e["ev"] == "getRandomRoom"),
            "lookupPatched": sum(1 for e in events if e["ev"] == "harvestLookupNull"),
        })
    return out


def main():
    trace = read_jsonl(os.path.join(ORACLE, "trace.jsonl"))
    caves = read_jsonl(os.path.join(ORACLE, "cave_game.jsonl"))
    rooms_json = os.path.join(EXTRACTED, "rooms.json")
    tests = [
        run_test("cave_oracle_test.exe", ORACLE),
        run_test("room_oracle_test.exe", ORACLE, rooms_json),
        run_test("fill_oracle_test.exe", ORACLE, rooms_json, os.path.join(EXTRACTED, "generation_data_counts.json")),
        run_test("room_sync_test.exe", ORACLE, rooms_json, os.path.join(EXTRACTED, "generation_data_counts.json")),
        run_test("floor_sync_test.exe", ORACLE, os.path.join(EXTRACTED, "generation_data_counts.json"), os.path.join(EXTRACTED, "wanderer_specialRooms.json"), os.path.join(EXTRACTED, "bundle_assets.json")),
    ]
    floors = floors_from_trace(trace, caves)

    # Chest totals across every recorded floor.
    kinds = collections.Counter()
    by_floor = collections.Counter()
    items = collections.defaultdict(lambda: {"rolls": 0, "hits": 0, "qty": 0, "chests": collections.Counter()})
    with_config = 0
    for f in floors:
        for r in f["rooms"]:
            for p in r["placed"]:
                for c in p["chests"]:
                    kinds[c["name"]] += 1
                    by_floor[(f["level"] + 1, c["name"])] += 1
                    if "rolls" in c:
                        with_config += 1
                        for roll in c["rolls"]:
                            it = items[roll["item"]]
                            it["rolls"] += 1
                            it["chests"][c["name"]] += 1
                            if roll["qty"] > 0:
                                it["hits"] += 1
                                it["qty"] += roll["qty"]
    chest_summary = {
        "kinds": kinds.most_common(),
        "byFloor": sorted([[lv, name, n] for (lv, name), n in by_floor.items()]),
        "withConfig": with_config,
        "items": sorted(
            [{"item": k, "rolls": v["rolls"], "hits": v["hits"], "qty": v["qty"], "chests": ", ".join(sorted(v["chests"]))} for k, v in items.items()],
            key=lambda x: (-x["hits"], -x["rolls"], x["item"]),
        ),
    }

    data = {
        "generated": datetime.datetime.now().strftime("%Y-%m-%d %H:%M"),
        "tests": tests,
        "floors": floors,
        "chests": chest_summary,
    }
    os.makedirs(OUT_DIR, exist_ok=True)
    with open(os.path.join(os.path.dirname(__file__), "template.html"), encoding="utf-8") as f:
        html = f.read()
    blob = json.dumps(data, separators=(",", ":")).replace("</", "<\\/")
    html = html.replace("/*__DATA__*/null", blob)
    out = os.path.join(OUT_DIR, "dungeon_report.html")
    with open(out, "w", encoding="utf-8") as f:
        f.write(html)
    print(out, f"{len(data['floors'])} floors, {len(html) // 1024} KB")


if __name__ == "__main__":
    main()

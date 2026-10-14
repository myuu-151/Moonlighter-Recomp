#!/usr/bin/env python3
"""The translated game's size on the GameCube, measured (docs/gamecube_translated.md, "Size").

Translates the units (default: `all`, the whole game) with tools/translate, compiles the result
and the layers it runs on (recomp/src: cs, unity, util, game, render, bind; Box2D, libtess2) with
devkitPPC at MLGC's flags, links them with --gc-sections around size_main.cpp (what the game's
scenes reach: every MonoBehaviour by name), and reads the link map: the bytes each loaded section
takes, by area (progress/classes.tsv's categories) and by kind of machinery (the game's methods,
List<T> instantiations, interface thunks, ...).

  py -3.13 tools/gcsize/gcsize.py [--units f] [--work dir] [-j N] [--opt -O2] [--no-translate]
                                   [--report out.md] [--json out.json]

Nothing it writes is committed: the translation, the objects and the program go to --work
(default ../gcsize next to the repository, outside it).
"""
import argparse
import collections
import concurrent.futures
import json
import os
import re
import subprocess
import sys
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(REPO, "recomp", "src")


def msys_to_win(p):
    m = re.match(r"^/([a-zA-Z])/(.*)$", p or "")
    return f"{m.group(1).upper()}:/{m.group(2)}" if m else p


def toolchain():
    ppc = msys_to_win(os.environ.get("DEVKITPPC", "")) or "C:/DolphinWorks/gekko-toolchain/devkitPPC"
    pro = msys_to_win(os.environ.get("DEVKITPRO", "")) or os.path.dirname(ppc)
    return ppc, pro


PPC, PRO = toolchain()
BIN = os.path.join(PPC, "bin")
CXX = os.path.join(BIN, "powerpc-eabi-g++.exe")
CC = os.path.join(BIN, "powerpc-eabi-gcc.exe")
CXXFILT = os.path.join(BIN, "powerpc-eabi-c++filt.exe")
SIZE = os.path.join(BIN, "powerpc-eabi-size.exe")
MACHDEP = ["-DGEKKO", "-mogc", "-mcpu=750", "-meabi", "-mhard-float"]
GEN_WARN = ["-Wno-unused-parameter", "-Wno-unused-variable", "-Wno-unused-but-set-variable", "-Wno-unused-label",
            "-Wno-switch", "-w"]


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        raise RuntimeError(f"{' '.join(cmd[:3])} ... failed:\n{r.stdout}{r.stderr}")
    return r


def flags(opt, extra_inc):
    inc = [f"-I{SRC}", f"-I{os.path.join(REPO, 'recomp', 'third_party', 'box2d')}",
           f"-I{os.path.join(REPO, 'recomp', 'third_party', 'libtess2', 'Include')}",
           f"-I{os.path.join(PRO, 'libogc', 'include')}"] + [f"-I{d}" for d in extra_inc]
    c = [opt, "-ffp-contract=off"] + MACHDEP + inc + ["-DPLATFORM_GAMECUBE=1", "-DML_GCSIZE=1"]
    return c, c + ["-std=gnu++20"]


def compile_all(jobs, work, gen, opt, rebuild, genflags=()):
    obj = os.path.join(work, "obj")
    os.makedirs(obj, exist_ok=True)
    cflags, cxxflags = flags(opt, [gen])
    tasks = []  # (src, out, cmd)

    # the translation: game.h precompiled first (every generated file includes it first)
    pch = os.path.join(gen, "game.h.gch")
    gen_files = sorted(f for f in os.listdir(gen) if f.endswith(".cpp"))
    for f in gen_files:
        tasks.append((os.path.join(gen, f), os.path.join(obj, "gen_" + f[:-4] + ".o"), "gen"))
    for d in ("cs", "unity", "util", "game", "render", "bind"):
        for f in sorted(os.listdir(os.path.join(SRC, d))):
            if f.endswith(".cpp"):
                tasks.append((os.path.join(SRC, d, f), os.path.join(obj, f"{d}_{f[:-4]}.o"), "rt"))
    b2 = os.path.join(REPO, "recomp", "third_party", "box2d", "Box2D")
    for root, _, files in os.walk(b2):
        for f in files:
            if f.endswith(".cpp"):
                tasks.append((os.path.join(root, f), os.path.join(obj, "b2_" + f[:-4] + ".o"), "rt"))
    tess = os.path.join(REPO, "recomp", "third_party", "libtess2", "Source")
    for f in os.listdir(tess):
        if f.endswith(".c"):
            tasks.append((os.path.join(tess, f), os.path.join(obj, "tess_" + f[:-2] + ".o"), "c"))
    tasks.append((os.path.join(REPO, "tools", "gcsize", "size_main.cpp"), os.path.join(obj, "size_main.o"), "gen"))

    stamp = os.path.join(work, "flags.txt")
    want = " ".join(cxxflags + list(genflags))
    if not os.path.exists(stamp) or open(stamp).read() != want:
        rebuild = True
    # (every object depends on the runtime's headers: the newest of them)
    headers = max(os.path.getmtime(os.path.join(r, f)) for r, _, fs in os.walk(SRC) for f in fs if f.endswith(".h"))
    if rebuild or not os.path.exists(pch) or os.path.getmtime(pch) < max(os.path.getmtime(os.path.join(gen, "game.h")), headers):
        t = time.time()
        run([CXX] + cxxflags + list(genflags) + GEN_WARN + ["-x", "c++-header", os.path.join(gen, "game.h"), "-o", pch])
        print(f"  game.h precompiled ({time.time() - t:.0f} s, {os.path.getsize(pch) >> 20} MB)", flush=True)
    with open(stamp, "w") as fh:
        fh.write(want)

    def one(task):
        src, out, kind = task
        if not rebuild and os.path.exists(out) and os.path.getmtime(out) >= max(os.path.getmtime(src), headers) and \
                (kind != "gen" or os.path.getmtime(out) >= os.path.getmtime(pch)):
            return None
        if kind == "c":
            cmd = [CC] + cflags + ["-w", "-c", src, "-o", out]
        elif kind == "gen":
            cmd = [CXX] + cxxflags + list(genflags) + GEN_WARN + ["-c", src, "-o", out]
        else:
            cmd = [CXX] + cxxflags + ["-w", "-c", src, "-o", out]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            return f"{os.path.basename(src)}:\n{r.stderr}"
        return None

    t = time.time()
    errors = []
    done = 0
    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        for e in ex.map(one, tasks):
            done += 1
            if e:
                errors.append(e)
            if done % 200 == 0:
                print(f"  {done}/{len(tasks)} compiled ({time.time() - t:.0f} s)", flush=True)
    print(f"  {len(tasks)} files compiled in {time.time() - t:.0f} s, {len(errors)} failed", flush=True)
    if errors:
        with open(os.path.join(work, "errors.txt"), "w", encoding="utf-8") as fh:
            fh.write("\n".join(errors))
    return [out for _, out, _ in tasks], errors


# ---- what doesn't compile yet ------------------------------------------------------------------------
# The whole game translates with the methods whose bindings are missing stubbed; a few more fail to
# compile (a binding names an overload the facade doesn't have). They're found from the compiler's
# errors and translated as throwing stubs too (the translator's --broken), so the rest can be measured;
# the list (work/broken.txt) is reported.

def enclosing_function(path, line, cache={}):
    if path not in cache:
        cache[path] = open(path, encoding="utf-8", errors="replace").read().split("\n")
    lines = cache[path]
    for i in range(min(line, len(lines)) - 1, -1, -1):
        l = lines[i]
        if l and not l[0].isspace() and l.rstrip().endswith("{") and "(" in l:
            head = l.split("(")[0] + "("
            for _ in range(3):
                head = re.sub(r"<[^<>()]*>", "", head)
            m = re.search(r"(gen::(?:\w+::)*\w+)\($", head)
            return m.group(1) if m else None
    return None


def broken_from_errors(errors, gen):
    found, unplaced = set(), []
    for block in errors:
        for l in block.split("\n"):
            if "error:" not in l and "required from here" not in l:
                continue
            m = re.search(r"gen[\\/]([^\\/:]+\.cpp):(\d+):\d+:", l)
            if not m:
                continue
            f = enclosing_function(os.path.join(gen, m.group(1)), int(m.group(2)))
            if f:
                found.add(f)
            else:
                unplaced.append(l)
    return found, unplaced


def translate(units, gen, broken):
    """Translates into gen, rewriting only the files that changed (so the rest isn't compiled again)."""
    tmp = gen + ".new"
    t = time.time()
    cmd = ["dotnet", "run", "--project", os.path.join(REPO, "tools", "translate"), "-c", "Release", "--",
           "translate", "--units", units, "--out", tmp]
    if broken:
        cmd += ["--broken", broken]
    r = run(cmd)
    first = next((l for l in r.stdout.split("\n") if l.startswith("translated ")), "")
    print(" ", first[:200], f"({time.time() - t:.0f} s)", flush=True)
    os.makedirs(gen, exist_ok=True)
    new = set(os.listdir(tmp))
    changed = 0
    for f in new:
        a_, b_ = os.path.join(tmp, f), os.path.join(gen, f)
        data = open(a_, "rb").read()
        if not os.path.exists(b_) or open(b_, "rb").read() != data:
            with open(b_, "wb") as fh:
                fh.write(data)
            changed += 1
        os.remove(a_)
    for f in os.listdir(gen):
        if f not in new and not f.endswith(".gch"):
            os.remove(os.path.join(gen, f))
    print(f"  {changed} files changed", flush=True)


def link(work, objs):
    elf = os.path.join(work, "whole.elf")
    mp = os.path.join(work, "whole.map")
    rsp = os.path.join(work, "objs.rsp")
    with open(rsp, "w") as fh:
        fh.write("\n".join(o.replace("\\", "/") for o in objs))
    libs = os.path.join(PRO, "libogc", "lib", "cube")
    run([CXX] + MACHDEP + [f"@{rsp}", f"-Wl,-Map,{mp}", "-Wl,--gc-sections", "-Wl,--no-eh-frame-hdr",
                            f"-L{libs}", "-logc", "-lm", "-o", elf])
    return elf, mp


# ---- the map ---------------------------------------------------------------------------------------

LOADED = {".text", ".init", ".fini", ".rodata", ".sdata2", ".sbss2", ".eh_frame", ".gcc_except_table", ".ctors", ".dtors",
          ".data", ".sdata", ".sbss", ".bss", ".init_array", ".fini_array", ".jcr", ".got2", ".got", ".fixup"}
GROUP = {".text": "code", ".init": "code", ".fini": "code",
         ".rodata": "rodata", ".sdata2": "rodata", ".sbss2": "rodata",
         ".eh_frame": "eh", ".gcc_except_table": "eh",
         ".data": "data", ".sdata": "data", ".ctors": "data", ".dtors": "data", ".init_array": "data",
         ".fini_array": "data", ".got2": "data", ".got": "data", ".fixup": "data", ".jcr": "data",
         ".sbss": "bss", ".bss": "bss"}


def parse_map(path):
    """[(output section, input section name, size, object)] for the loaded sections."""
    out = []
    lines = open(path, encoding="utf-8", errors="replace").read().split("\n")
    i = 0
    while i < len(lines) and not lines[i].startswith("Linker script and memory map"):
        i += 1
    cur = None
    pending = None
    for line in lines[i:]:
        m = re.match(r"^(\.[\w.]+)\s+0x[0-9a-f]+\s+0x[0-9a-f]+", line)
        if m or re.match(r"^\.[\w.]+\s*$", line):
            name = line.split()[0]
            cur = name if name in LOADED else None
            pending = None
            continue
        if cur is None:
            continue
        m = re.match(r"^ (\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(.+)$", line)
        if m:
            out.append((cur, m.group(1), int(m.group(3), 16), m.group(4).strip()))
            pending = None
            continue
        m = re.match(r"^ (\S+)\s*$", line)
        if m and not line.startswith(" *"):
            pending = m.group(1)
            continue
        m = re.match(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$", line)
        if m and pending:
            out.append((cur, pending, int(m.group(2), 16), m.group(3).strip()))
            pending = None
    return out


def demangle(names):
    names = list(names)
    res = {}
    for k in range(0, len(names), 4000):
        chunk = names[k:k + 4000]
        r = subprocess.run([CXXFILT], input="\n".join(chunk), capture_output=True, text=True)
        for a, b in zip(chunk, r.stdout.split("\n")):
            res[a] = b
    return res


def symbol_of(section):
    # .text._ZN3gen5Enemy6UpdateEv -> _ZN3gen5Enemy6UpdateEv; .rodata._ZTVN3gen5EnemyE -> _ZTV...
    for p in (".text.unlikely.", ".text.startup.", ".text.", ".rodata.", ".sdata2.", ".data.rel.ro.", ".data.", ".sdata.",
              ".bss.", ".sbss.", ".gcc_except_table.", ".sbss2."):
        if section.startswith(p):
            return section[len(p):]
    return None


def layer_of(obj):
    b = os.path.basename(obj.replace("\\", "/"))
    if b.startswith("gen_"):
        return "translated"
    for p, layer in (("cs_", ".NET runtime (cs)"), ("bind_", "facade (bind)"), ("unity_", "Unity layer (unity)"),
                     ("game_", "hand port (game)"), ("render_", "render"), ("util_", "util"), ("b2_", "Box2D"),
                     ("tess_", "libtess2"), ("size_main", "driver")):
        if b.startswith(p):
            return layer
    if "libstdc++" in obj or "libsupc++" in obj:
        return "libstdc++"
    if "libogc" in obj or "/cube/" in obj.replace("\\", "/"):
        return "libogc"
    return "libc, libgcc, crt"


def kind_of(dm, section, out_sec):
    """The kind of machinery a translated input section is."""
    if out_sec == ".eh_frame":
        return "exception tables (.eh_frame)"
    if out_sec == ".gcc_except_table" or section.startswith(".gcc_except_table"):
        return "exception tables (LSDA)"
    if dm is None:
        return "other (no symbol)"
    if dm.startswith("vtable for") or dm.startswith("VTT for") or dm.startswith("construction vtable"):
        return "vtables"
    if dm.startswith("typeinfo"):
        return "RTTI"
    if "virtual thunk to" in dm or "non-virtual thunk to" in dm:
        return "interface thunks"
    if dm.startswith("guard variable") or dm.startswith("_GLOBAL__sub_I"):
        return "static init"
    if "::cs_Message(" in dm or "::cs_Coroutine(" in dm or re.search(r"::cs_(Awake|OnEnable|Start|Update|FixedUpdate|LateUpdate|OnDisable|OnDestroy|On\w+)\(", dm):
        return "MonoBehaviour messages"
    if "::cs_trace" in dm or "cs_trace_value" in dm:
        return "cs_trace (collector)"
    if "::cs_SetField(" in dm or "::cs_SerialInit(" in dm or "UnityEngine::Serial" in dm:
        return "serialized fields"
    if "::cs_Clone(" in dm or "::cs_Remap(" in dm:
        return "Instantiate (clone, remap)"
    if "::cs_typename(" in dm or "::cs_traced(" in dm or "cs_static_typename" in dm or "cs_box_typename" in dm:
        return "type names"
    if "cs_AddComponent" in dm:
        return "AddComponent table"
    if "::__si()" in dm or "::__cctor()" in dm or "cs_cctor_state_" in dm or "cs_roots_" in dm:
        return "static init"
    head = dm.split("(")[0]
    if re.match(r"^(\w+ )*cs::(List|Array|Array2)<", head) or "cs::List<" in head.split("::")[0:3].__str__():
        pass
    for pat, k in ((r"cs::List<", "List<T>"), (r"cs::Dictionary<", "Dictionary, HashSet"), (r"cs::HashSet<", "Dictionary, HashSet"),
                   (r"cs::Array2?<", "Array<T>"), (r"cs::Delegate<", "delegates"), (r"cs::Box<", "boxing"),
                   (r"cs::BoxedEnumerator<", "List<T>"), (r"cs::Queue<|cs::Stack<", "Queue, Stack"),
                   (r"cs::Linq|cs::Enumerable", "LINQ"), (r"cs::KeyValuePair<", "Dictionary, HashSet"),
                   (r"cs::EqualityComparer<|cs::Comparer<", "comparers")):
        if re.search(pat, head) and not head.startswith("gen::"):
            return k
    if re.match(r"^(\w+ )*gen::", head) or head.startswith("gen::"):
        return "the game's methods"
    if "CS_LIT" in dm or "cs_lit" in dm or dm.startswith(".L"):
        return "string literals"
    return "other"


def owner_of(dm):
    """The game class a symbol belongs to: the first gen::Name in it."""
    if not dm:
        return None
    m = re.search(r"gen::((?:\w+::)*?)(\w+)", dm)
    if not m:
        return None
    return m.group(2)


def load_areas():
    areas = {}
    path = os.path.join(REPO, "progress", "classes.tsv")
    for line in open(path, encoding="utf-8"):
        if line.startswith("#"):
            continue
        f = line.rstrip("\n").split("\t")
        if len(f) >= 3:
            areas[f[0]] = f[2]
    return areas


def class_files(gen):
    """class name -> C# file, from the generated .cpp files' definitions (gen::Name::member)."""
    res = {}
    for f in os.listdir(gen):
        if not f.endswith(".cpp"):
            continue
        cs = f[:-4].replace("_", "/") + ".cs"
        txt = open(os.path.join(gen, f), encoding="utf-8", errors="replace").read()
        for m in re.finditer(r"\bgen::(?:\w+::)*?(\w+)::\w+\(", txt):
            res.setdefault(m.group(1), f[:-4])
    return res


def analyze(mp, gen, work):
    entries = parse_map(mp)
    syms = {s: symbol_of(s) for _, s, _, _ in entries}
    dm = demangle(sorted({v for v in syms.values() if v and v.startswith("_Z")}))
    areas = load_areas()
    cf = class_files(gen)
    cs_files = {}  # generated file stem -> C# path in classes.tsv
    for p in areas:
        cs_files[p[:-3].replace("/", "_")] = p

    def area_of_stem(stem):
        p = cs_files.get(stem)
        return areas.get(p, "Core & other") if p else None

    by_layer = collections.defaultdict(lambda: collections.Counter())
    by_area = collections.defaultdict(lambda: collections.Counter())
    by_kind = collections.defaultdict(lambda: collections.Counter())
    area_kind = collections.defaultdict(lambda: collections.Counter())
    biggest = collections.Counter()
    for out_sec, sec, size, obj in entries:
        if size == 0:
            continue
        g = GROUP[out_sec]
        layer = layer_of(obj)
        by_layer[layer][g] += size
        if layer != "translated":
            continue
        s = syms.get(sec)
        d = dm.get(s, s) if s else None
        k = kind_of(d, sec, out_sec)
        by_kind[k][g] += size
        owner = owner_of(d)
        stem = cf.get(owner) if owner else None
        if stem is None:
            stem = os.path.basename(obj.replace("\\", "/"))[4:-2]
        a = area_of_stem(stem) or "Core & other"
        by_area[a][g] += size
        area_kind[a][k] += size
        if d and g == "code":
            biggest[d] += size
    return dict(by_layer=by_layer, by_area=by_area, by_kind=by_kind, area_kind=area_kind, biggest=biggest)


def kb(n):
    return f"{n / 1024:,.0f}"


def report(res, elf, label):
    groups = ["code", "rodata", "eh", "data", "bss"]
    lines = [f"# gcsize: {label}", "", f"program: {elf}", "",
             "Loaded bytes (KB): code = .text; rodata = .rodata, .sdata2 (vtables, RTTI, literals); eh = .eh_frame and "
             ".gcc_except_table; data = .data, .sdata, .ctors; bss = .bss, .sbss.", ""]

    def table(title, rows):
        lines.append(f"## {title}")
        lines.append("")
        lines.append("| | " + " | ".join(groups) + " | loaded (no bss) | all |")
        lines.append("|---|" + "---:|" * (len(groups) + 2))
        tot = collections.Counter()
        for name, c in sorted(rows.items(), key=lambda kv: -sum(kv[1].values())):
            tot.update(c)
            ld = sum(c[g] for g in groups if g != "bss")
            lines.append(f"| {name} | " + " | ".join(kb(c[g]) for g in groups) + f" | {kb(ld)} | {kb(sum(c.values()))} |")
        ld = sum(tot[g] for g in groups if g != "bss")
        lines.append("| **total** | " + " | ".join(f"**{kb(tot[g])}**" for g in groups) + f" | **{kb(ld)}** | **{kb(sum(tot.values()))}** |")
        lines.append("")

    table("By layer", res["by_layer"])
    table("The translated code, by area (progress/classes.tsv)", res["by_area"])
    table("The translated code, by kind", res["by_kind"])
    lines.append("## The biggest translated functions (code)")
    lines.append("")
    for d, n in res["biggest"].most_common(25):
        lines.append(f"- {n:,} {d[:160]}")
    lines.append("")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--units", help="units file (default: the whole game, `all`)")
    ap.add_argument("--work", default=os.path.abspath(os.path.join(REPO, "..", "gcsize")))
    ap.add_argument("--gen", help="an existing translation to measure (skips translating)")
    ap.add_argument("-j", type=int, default=12)
    ap.add_argument("--opt", default="-O2")
    ap.add_argument("--genflags", default="-fno-threadsafe-statics -fno-ipa-sra",
                    help="the translated code's own flags (as MLGC's GEN_CXXFLAGS)")
    ap.add_argument("--no-translate", action="store_true")
    ap.add_argument("--rebuild", action="store_true")
    ap.add_argument("--label", default="the whole game")
    ap.add_argument("--report")
    ap.add_argument("--json")
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    gen = a.gen or os.path.join(a.work, "gen")
    broken = os.path.join(a.work, "broken.txt")
    units = a.units
    if not units:
        units = os.path.join(a.work, "units_all.txt")
        with open(units, "w") as fh:
            fh.write("all\n")
    for attempt in range(6):
        if not a.gen and not a.no_translate:
            translate(units, gen, broken if os.path.exists(broken) else None)
        objs, errors = compile_all(a.j, a.work, gen, a.opt, a.rebuild, a.genflags.split())
        if not errors:
            break
        found, unplaced = broken_from_errors(errors, gen)
        have = set(open(broken).read().split()) if os.path.exists(broken) else set()
        if a.gen or a.no_translate or unplaced or not (found - have):
            for e in errors[:5]:
                print(e[:3000])
            for l in unplaced[:20]:
                print("  not in a method:", l[:300])
            print(f"  {len(errors)} files failed (all in {os.path.join(a.work, 'errors.txt')})")
            raise SystemExit(1)
        with open(broken, "w") as fh:
            fh.write("\n".join(sorted(have | found)) + "\n")
        print(f"  {len(found - have)} more methods don't compile: translated as stubs ({broken})", flush=True)
    nbroken = len(open(broken).read().split()) if os.path.exists(broken) else 0
    elf, mp = link(a.work, objs)
    res = analyze(mp, gen, a.work)
    text = report(res, elf, a.label + f" ({a.opt}{' ' + a.genflags if a.genflags else ''})" +
                  (f"; {nbroken} methods that don't compile yet stubbed: {broken}" if nbroken else ""))
    print(text)
    if a.report:
        with open(a.report, "w", encoding="utf-8") as fh:
            fh.write(text)
    if a.json:
        with open(a.json, "w") as fh:
            json.dump({k: {n: dict(c) for n, c in v.items()} if k != "biggest" else dict(v.most_common(200))
                       for k, v in res.items()}, fh, indent=1)


if __name__ == "__main__":
    main()

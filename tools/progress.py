"""The port's progress, as decompilation projects show theirs: how much of the game's own C#
the C++ port covers, and how much of that is verified against the real game.

    python tools/progress.py --scan   (needs decomp/: re-measures the game's code)
    python tools/progress.py          (from the committed tables: the README section and badges)

progress/classes.tsv: the game's own classes (path under decomp/Assembly-CSharp, its
    non-blank lines, its category), written by --scan. Third-party libraries, platform code,
    editor tools and examples are left out (EXCLUDED below). Only names and counts are
    committed, no code.
progress/ported.tsv: what the port covers, one class a line: path, the fraction of the class
    ported (0-1), verified (y: the oracle's recordings check it), the port's files, a note.
    Edit it as classes are ported; the fractions of partly ported classes are estimates.

Outputs: progress/badge.json and progress/badge_verified.json (shields.io endpoint badges),
progress/progress.json, and the README's section between <!-- progress:start --> and
<!-- progress:end -->. The pre-commit hook (tools/hooks/pre-commit; `git config core.hooksPath
tools/hooks`) runs this on every commit, so the bar follows the work.
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DECOMP = ROOT / 'decomp' / 'Assembly-CSharp'
OUT = ROOT / 'progress'
README = ROOT / 'README.md'
BARS = ROOT / 'progress' / 'bars'

# Folders (namespaces) that aren't the game's own code.
EXCLUDED_DIRS = (
    'BasicExample', 'BindingsExample', 'ChromaSDK', 'DG.Tweening', 'DigitalRuby.SimpleLUT',
    'DigitalSunGames.Languages.I2', 'DigitalSunGames.Platforms', 'DigitalSunGames.Platforms.PS4',
    'FullSerializer', 'FullSerializer.Internal', 'FullSerializer.Internal.Converters',
    'FullSerializer.Internal.DirectConverters', 'I2.Loc', 'I2.Loc.SimpleJSON', 'InControl',
    'InControl.Internal', 'InControl.NativeProfile', 'InterfaceMovement', 'MultiplayerBasicExample',
    'MultiplayerWithBindingsExample', 'PixelArtRotation', 'PixelArtRotation.Internal', 'Properties',
    'TMPro', 'TMPro.Examples', 'TouchExample', 'UnityStandardAssets.Utility',
    'UnityStandardAssets.Utility.Utils', 'VirtualDeviceExample', 'XInputDotNetPure',
)
# Single files that aren't (Razer Chroma lighting, editor-only tools, debug draws).
EXCLUDED_FILES = re.compile(r'^(Chroma.*|RazerChroma.*|UnityNativeChromaSDK.*|.*EditorGUI|DrawPolygonsDebug)\.cs$')

# Categories by path, first match wins.
CATEGORIES = (
    ('Wanderer DLC', r'^Moonlighter\.DLC'),
    ('Dungeon generation', r'^Moonlighter\.DungeonGeneration/|^(Cave|DungeonGenerat|DungeonRoom|DungeonSpawn|ChestGenerat|DetailsPrefab|RoomDetail)|^(Constants|WeightedList|GaussianRandom)\.cs'),
    ('Bosses', r'Boss|^GolemKing|^CarnivorousMutae|^Gizmo|^Wanderer.*Boss'),
    ('Enemies', r'Enemy|Slime|Golem|Mimic|Tangle|Turret|Kamikaze|Projectile|^PolyNav|^AStar|^Heap|^IHeapItem|Behaviour\.cs$'),
    ('Player', r'^HeroMerchant|^Will|^Weapon|^Bow|^Spear|^Gloves|^BigSword|^ShortSword|^Dash|^Ability|^Stat\.cs|^SMAnimator|^PickOnWalk|^ActionArea|^ActionTrigger'),
    ('Dungeon runtime', r'^Dungeon|^Chest|^NextLevel|^Breakable|^Hole|^Room|^Trap'),
    ('Items & effects', r'Item|Effect|Amulet|Potion|Recipe|Equipment|Inventory(?!Panel)'),
    ('Town, shop & villagers', r'Shop|Town|Villager|Citizen|Interactive|Banker|Blacksmith|Witch|Merchant|Customer|Bed\.cs|Moonlighter\.Shop'),
    ('UI', r'Panel|GUI|HUD|Menu|Button|Slot|Popup|Text|Image|Cursor|Tooltip|Minimap|Dialog'),
)
OTHER = 'Core & other'


def nonblank_lines(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        return sum(1 for line in f if line.strip())


def category(rel):
    for name, pattern in CATEGORIES:
        if re.search(pattern, rel):
            return name
    return OTHER


def scan():
    if not DECOMP.is_dir():
        sys.exit(f'no {DECOMP}: --scan needs the decompiled game (see README)')
    rows = []
    for path in sorted(DECOMP.rglob('*.cs')):
        rel = path.relative_to(DECOMP).as_posix()
        top = rel.split('/')[0] if '/' in rel else ''
        if top in EXCLUDED_DIRS or EXCLUDED_FILES.match(path.name):
            continue
        rows.append((rel, nonblank_lines(path), category(rel)))
    OUT.mkdir(exist_ok=True)
    with open(OUT / 'classes.tsv', 'w', encoding='utf-8', newline='\n') as f:
        f.write('# path\tlines\tcategory  (tools/progress.py --scan)\n')
        for rel, lines, cat in rows:
            f.write(f'{rel}\t{lines}\t{cat}\n')
    print(f'{len(rows)} classes, {sum(r[1] for r in rows)} lines')


def read_tsv(path):
    out = []
    for line in open(path, encoding='utf-8'):
        if not line.strip() or line.startswith('#'):
            continue
        out.append(line.rstrip('\n').split('\t'))
    return out


def bar_svg(name, ported, verified, width=160, height=12, label=None, fill='#e8b43c'):
    """progress/bars/<name>.svg: a rounded bar, the ported share in gold and the verified share in
    green over it (the tray icon's colours), on a grey track that reads on light and dark pages.
    With a label, the text sits to the right of the bar. Returns the README's relative path."""
    BARS.mkdir(parents=True, exist_ok=True)
    text_w = 56 if label else 0
    r = height / 2
    pw = max(0.0, min(1.0, ported)) * width
    vw = max(0.0, min(1.0, verified)) * width
    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width + text_w}" height="{height}" viewBox="0 0 {width + text_w} {height}">',
           f'<clipPath id="c"><rect width="{width}" height="{height}" rx="{r}"/></clipPath>',
           '<g clip-path="url(#c)">',
           f'<rect width="{width}" height="{height}" fill="#8b949e" fill-opacity="0.3"/>',
           f'<rect width="{pw:.2f}" height="{height}" fill="{fill}"/>',
           f'<rect width="{vw:.2f}" height="{height}" fill="#5ac86e"/>',
           '</g>']
    if label:
        svg.append(f'<text x="{width + 8}" y="{height * 0.8:.1f}" font-family="Segoe UI,Helvetica,Arial,sans-serif" '
                   f'font-size="{height * 0.85:.1f}" font-weight="600" fill="#8b949e">{label}</text>')
    svg.append('</svg>')
    (BARS / f'{name}.svg').write_text('\n'.join(svg) + '\n', encoding='utf-8', newline='\n')
    return f'progress/bars/{name}.svg'


def read_translated():
    """{path: the share of its lines that tools/translate translates today} from the translator's
    survey (progress/class_deps.tsv), or {} before there is one."""
    path = OUT / 'class_deps.tsv'
    if not path.exists():
        return {}
    return {r[0]: float(r[6].rstrip('%')) / 100.0 for r in read_tsv(path) if len(r) > 6}


def slug(name):
    return re.sub(r'[^a-z0-9]+', '_', name.lower()).strip('_')


def colour(pct):
    return 'red' if pct < 10 else 'orange' if pct < 25 else 'yellow' if pct < 50 else 'yellowgreen' if pct < 75 else 'brightgreen'


def report():
    classes = {r[0]: (int(r[1]), r[2]) for r in read_tsv(OUT / 'classes.tsv')}
    ported = read_tsv(OUT / 'ported.tsv')
    total = sum(v[0] for v in classes.values())
    cats = {}
    shares = read_translated()
    translated = 0.0
    for rel, (lines, cat) in classes.items():
        c = cats.setdefault(cat, {'lines': 0, 'ported': 0.0, 'verified': 0.0, 'translated': 0.0, 'classes': 0, 'done': 0})
        c['lines'] += lines
        c['classes'] += 1
        c['translated'] += lines * shares.get(rel, 0.0)
        translated += lines * shares.get(rel, 0.0)
    done = verified = 0.0
    unknown = []
    for row in ported:
        rel, frac, ver = row[0], float(row[1]), row[2].strip().lower() == 'y'
        if rel not in classes:
            unknown.append(rel)
            continue
        lines, cat = classes[rel]
        c = cats[cat]
        c['ported'] += lines * frac
        done += lines * frac
        if ver:
            c['verified'] += lines * frac
            verified += lines * frac
        if frac >= 1.0:
            c['done'] += 1
    if unknown:
        print('ported.tsv names classes not in classes.tsv: ' + ', '.join(unknown), file=sys.stderr)
    pct, vpct, tpct = 100.0 * done / total, 100.0 * verified / total, 100.0 * translated / total
    full = sum(1 for r in ported if float(r[1]) >= 1.0 and r[0] in classes)

    OUT.mkdir(exist_ok=True)
    json.dump({'schemaVersion': 1, 'label': 'ported', 'message': f'{pct:.1f}%', 'color': colour(pct)},
              open(OUT / 'badge.json', 'w', encoding='utf-8'))
    json.dump({'schemaVersion': 1, 'label': 'verified vs. game', 'message': f'{vpct:.1f}%', 'color': colour(vpct)},
              open(OUT / 'badge_verified.json', 'w', encoding='utf-8'))
    json.dump({'lines': total, 'ported': round(done), 'verified': round(verified), 'percent': round(pct, 2),
               'verifiedPercent': round(vpct, 2), 'translated': round(translated), 'translatedPercent': round(tpct, 2), 'classes': len(classes), 'classesDone': full,
               'categories': {k: {kk: (round(vv, 1) if isinstance(vv, float) else vv) for kk, vv in v.items()} for k, v in cats.items()}},
              open(OUT / 'progress.json', 'w', encoding='utf-8'), indent=1)

    # The table's order: the game's systems as the port reaches them.
    order = ['Dungeon generation', 'Player', 'Enemies', 'Dungeon runtime', 'Bosses', 'Items & effects',
             'Town, shop & villagers', 'UI', 'Wanderer DLC', OTHER]
    lines = [
        '<!-- progress:start -->',
        '## Progress',
        '',
        '![ported](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/myuu-151/Moonlighter-Recomp/main/progress/badge.json)',
        '![verified](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/myuu-151/Moonlighter-Recomp/main/progress/badge_verified.json)',
        '',
        f'**{pct:.1f}%** of the game\'s own code is ported ({round(done):,} of {total:,} lines of C#, '
        f'{full} of {len(classes):,} classes whole); **{vpct:.1f}%** is verified against the real game by the '
        'oracle\'s recordings. Third-party libraries, platform code and editor tools aren\'t counted; '
        'partly ported classes count by an estimated fraction (`progress/ported.tsv`).',
        '',
        f'<img src="{bar_svg("total", done / total, verified / total, width=360, height=18, label=f"{pct:.1f}%")}" alt="{pct:.1f}% ported, {vpct:.1f}% verified">',
        '',
        '<sub>gold: ported · green: verified against the game</sub>',
        '',
    ]
    if shares:
        lines += [
            f"**{tpct:.1f}%** translates today: the share of the game's lines that `tools/translate` turns into C++ "
            'with every API it calls bound to the hand-written Unity layer ([docs/translator_plan.md](docs/translator_plan.md); '
            'per class in `progress/class_deps.tsv`, what blocks the rest in `progress/unity_api_gaps.tsv`; '
            'each class counts by its share to a tenth of a percent).',
            '',
            f'<img src="{bar_svg("translated", translated / total, 0.0, width=360, height=18, label=f"{tpct:.1f}%", fill="#58a6ff")}" alt="{tpct:.1f}% translates">',
            '',
            '<sub>blue: translates today</sub>',
            '',
        ]
    lines += [
        '| Area | Lines | Ported | Verified | Translates | |',
        '|---|---:|---:|---:|---:|---|',
    ]
    for name in order:
        c = cats.get(name)
        if not c:
            continue
        p = c['ported'] / c['lines'] if c['lines'] else 0.0
        v = c['verified'] / c['lines'] if c['lines'] else 0.0
        img = bar_svg(slug(name), p, v)
        t = c['translated'] / c['lines'] if c['lines'] else 0.0
        lines.append(f'| {name} | {c["lines"]:,} | {100 * p:.1f}% | {100 * v:.1f}% | {100 * t:.1f}% | <img src="{img}" alt="{100 * p:.1f}%"> |')
    lines += ['', '_Updated by `tools/progress.py` on every commit._ On Windows, `py tools/tray_progress_startup.py` puts the same numbers next to the taskbar clock (`tools/tray_progress.ps1`), starting with Windows.', '<!-- progress:end -->']
    section = '\n'.join(lines)

    text = README.read_text(encoding='utf-8')
    if '<!-- progress:start -->' in text:
        text = re.sub(r'<!-- progress:start -->.*?<!-- progress:end -->', lambda m: section, text, flags=re.S)
    else:
        # After the title block: before the first "## " heading.
        at = text.find('\n## ')
        text = text[:at + 1] + section + '\n\n' + text[at + 1:] if at >= 0 else text + '\n' + section + '\n'
    README.write_text(text, encoding='utf-8', newline='\n')
    print(f'{pct:.1f}% ported, {vpct:.1f}% verified ({round(done):,} / {total:,} lines)')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--scan', action='store_true', help='re-measure the game\'s code from decomp/')
    args = ap.parse_args()
    if args.scan:
        scan()
    report()


if __name__ == '__main__':
    main()

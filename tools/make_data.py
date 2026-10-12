"""Makes the game's data from your Steam copy of Moonlighter, into data/extracted/ (which
git ignores), for the port and its tests.

    python tools/make_data.py [--game <the game's folder>]

1. Finds the game: the folder given (--game, or ML_GAME in the environment: a depot
   download, say, which Steam doesn't list), else through Steam (app 606150); and checks
   some of its files are the Steam copy's.
2. Extracts from it (tools/extract/), into data/extracted/:
   - constants.json, rooms.json, specialRooms.json: the text assets the game loads from
     Resources (the Constants table, the room designs, the special rooms);
   - roomdb_asset.json: the room database as the generator sees it;
   - generation_data_counts.json: each dungeon's background, light, shadow and hole lists;
   - bundle_assets.json: every asset bundle's asset paths (the prefab lookups);
   - details.json: the dungeons' details registers (prefab names, sprite sizes,
     probability images);
   - wanderer_specialRooms.json: the Wanderer DLC's rooms, if the DLC is installed;
   - prefabs.json: the dungeon and enemy prefabs, flattened (objects, sprites, sorting).

Nothing from the game is stored in this repository. Needs Python 3 with UnityPy
(py -m pip install UnityPy).
"""
import hashlib
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
EXTRACT = HERE / 'tools' / 'extract'
OUT = HERE / 'data' / 'extracted'

STEAM_APP = 606150  # Moonlighter
# The Steam copy's (v1.15.8), checked before anything is made from it.
KNOWN_FILES = {
    'Moonlighter_Data/Managed/Assembly-CSharp.dll': 'd834f0ae78eab04e2d741b30e352d515f1814aa2330c8ed684d7c59027e55cdc',
    'Moonlighter_Data/globalgamemanagers': 'f208aa6933e1a86d5fa866e9635446ce2dd288eb6b03d2a3303b8ed42eaf5351',
    'Moonlighter_Data/StreamingAssets/AssetBundles/dungeon_golem': 'b644016bc1f5b2d8df808846c2965d9afa180bf878640369a504d8ca865cf40b',
    'Moonlighter_Data/StreamingAssets/AssetBundles/enemies_golem': '9af599416a04454c444abd1e81e2a5222f839265adb84dc174c8c40a4f6a8e81',
}
# Checked too when it's there (the DLC is optional).
KNOWN_DLC = {'dlc/wanderer_dlc': '62655a42d79fbd12c80bc955314335be6a3329adc39a3ba4973f6e390d0eaf00'}


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def check_game(game):
    """The folder, if it holds the game: its Moonlighter.exe and Moonlighter_Data/, and some
    files the Steam copy's. Exits, saying why, if not."""
    game = Path(game)
    if not (game / 'Moonlighter.exe').exists() or not (game / 'Moonlighter_Data').is_dir():
        sys.exit(f"{game} isn't Moonlighter's folder (no Moonlighter.exe and Moonlighter_Data/ in it).")
    known = dict(KNOWN_FILES)
    known.update({name: digest for name, digest in KNOWN_DLC.items() if (game / name).exists()})
    for name, digest in known.items():
        file = game / name
        if not file.exists() or sha256(file) != digest:
            sys.exit(f"{file} is missing or not the Steam copy's: a different build of the game (1.15.8 is "
                     "the one supported), or not all of it (verify the files in Steam, or download the depot again).")
    return game


def find_game(chosen=None):
    """The game's folder: the one chosen (an argument, or ML_GAME), else Steam's."""
    chosen = chosen or os.environ.get('ML_GAME')
    return check_game(chosen) if chosen else steam_game()


def steam_game():
    """The game's folder, from Steam: Steam's path (the registry), its libraries
    (steamapps/libraryfolders.vdf), the library holding the app's manifest
    (appmanifest_606150.acf) and the folder that names; then its files checked. Exits,
    saying why, if any of it isn't there."""
    steam = None
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam') as key:
            steam = Path(winreg.QueryValueEx(key, 'SteamPath')[0])
    except OSError:
        pass
    if steam is None or not steam.exists():
        steam = Path(r'C:\Program Files (x86)\Steam')
    libraries = [steam]
    vdf = steam / 'steamapps' / 'libraryfolders.vdf'
    if vdf.exists():
        for path in re.findall(r'"path"\s+"([^"]+)"', vdf.read_text(encoding='utf-8', errors='replace')):
            libraries.append(Path(path.replace('\\\\', '\\')))  # the file doubles its backslashes
    for library in libraries:
        manifest = library / 'steamapps' / f'appmanifest_{STEAM_APP}.acf'
        if not manifest.exists():
            continue
        found = re.search(r'"installdir"\s+"([^"]+)"', manifest.read_text(encoding='utf-8', errors='replace'))
        game = library / 'steamapps' / 'common' / found.group(1) if found else None
        if game is None or not game.is_dir():
            sys.exit(f'Steam lists Moonlighter ({manifest}) but its folder is missing: install it in Steam.')
        return check_game(game)
    sys.exit(f'Moonlighter (Steam app {STEAM_APP}) is not installed through Steam on this PC: '
             'install it, or choose the folder your copy is in (a depot download, say).')


def step(title, args):
    print(f'-- {title}', flush=True)
    # No console window of its own; its output into ours.
    out = sys.stdout if sys.stdout is not None else None
    subprocess.run([sys.executable, '-u'] + [str(a) for a in args], check=True, stdout=out, stderr=out,
                   creationflags=0x08000000 if sys.platform == 'win32' else 0)


def main():
    import argparse
    parser = argparse.ArgumentParser(description='Makes the data from your copy of Moonlighter.')
    parser.add_argument('--game', help="the game's folder (else ML_GAME, else Steam's)")
    game = find_game(parser.parse_args().game)
    print(f'The game: {game}', flush=True)
    try:
        import UnityPy  # noqa: F401
    except ImportError:
        sys.exit('UnityPy is needed to read the game\'s asset bundles: py -m pip install UnityPy')
    OUT.mkdir(parents=True, exist_ok=True)
    step("the Resources texts", [EXTRACT / 'resources_text.py', '--game', game, '--out', OUT])
    step('the room database', [EXTRACT / 'room_database.py', '--game', game, '--out', OUT])
    step("the dungeons' generation data", [EXTRACT / 'generation_data.py', '--game', game, '--out', OUT])
    step('indexing the asset bundles', [EXTRACT / 'bundle_index.py', '--game', game, '--out', OUT])
    step("the dungeons' details", [EXTRACT / 'details.py', '--game', game, '--out', OUT])
    step("the Wanderer DLC's rooms", [EXTRACT / 'wanderer_rooms.py', '--game', game, '--out', OUT])
    step('the prefabs', [EXTRACT / 'prefabs.py', '--game', game, '--out', OUT])
    print(f'-- done: {OUT}', flush=True)


if __name__ == '__main__':
    main()

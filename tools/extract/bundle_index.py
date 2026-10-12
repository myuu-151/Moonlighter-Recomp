"""Index the game's asset bundles: bundle name -> asset paths, from the .manifest files
Unity writes next to each bundle. The game looks prefabs up by path substring
(AssetBundleUtils.LoadAssetContaining), so this is what the port's prefab lookups need.

    python tools/extract/bundle_index.py --game <the game's folder> --out <folder>
    -> <folder>/bundle_assets.json
"""
import argparse
import json
from pathlib import Path


def assets(manifest):
    out, inside = [], False
    for line in manifest.read_text(encoding='utf-8').splitlines():
        if line.startswith('Assets:'):
            inside = True
            continue
        if inside:
            if not line.startswith('- '):
                break
            out.append(line[2:].strip())
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    bundles = Path(args.game) / 'Moonlighter_Data' / 'StreamingAssets' / 'AssetBundles'
    index = {m.name[:-len('.manifest')]: assets(m) for m in sorted(bundles.glob('*.manifest'))
             if m.name != 'AssetBundles.manifest'}
    out = Path(args.out) / 'bundle_assets.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(index, indent=1), encoding='utf-8')
    print(f'   {sum(len(v) for v in index.values())} assets in {len(index)} bundles', flush=True)


if __name__ == '__main__':
    main()

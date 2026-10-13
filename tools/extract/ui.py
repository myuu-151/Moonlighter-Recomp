"""The game's UI as Unity loads it: every hierarchy with a RectTransform in it (the scenes'
canvases, the prefabs of resources.assets, sharedassets and the bundles), object by object,
with its RectTransform (anchors, pivot, sizeDelta, anchoredPosition, local position, rotation
and scale), and its components: the native ones the UI is built from (Canvas, CanvasGroup,
CanvasRenderer) and every MonoBehaviour with all its serialized fields (UnityEngine.UI's Image,
Text, CanvasScaler, Selectable/Button/Toggle/Slider/Scrollbar/ScrollRect/Dropdown, the layout
groups, ContentSizeFitter, LayoutElement, Mask, RectMask2D, Shadow/Outline; TextMeshPro's; the
game's own UI scripts), references turned into paths or asset names. Also the sprites and
fonts they reference (what drawing them needs: a sprite's rect, pivot, border, pixels per unit
and where it sits in its texture; a font's metrics and whether it's dynamic).

The level files and resources.assets keep no type trees for MonoBehaviours: they're made from
the game's own Managed assemblies (UnityPy's TypeTreeGenerator, which needs the
TypeTreeGeneratorAPI package: py -m pip install TypeTreeGeneratorAPI). UnityPy checks every
object is read to its last byte.

Needs UnityPy and TypeTreeGeneratorAPI.

    python tools/extract/ui.py --game <the game's folder> --out <folder>
    -> <folder>/ui/<source>.json, one per file with UI in it: the scenes by their names
       (scene_Managers.json, scene_MainMenu.json, scene_Dungeon.json, scene_Town.json:
       level0..3 in BuildSettings' order), resources.assets.json, sharedassets<n>.assets.json,
       bundle_<name>.json:
         {"file": the game's file, "scene": its scene's path or null,
          "roots": [{"name", "id" (the root GameObject's path id in its file),
                     "resources": its Resources.Load path or null,
                     "nodes": [node, ...] (depth first, the root first)}]}
         node: {"path" (below the root, "" the root), "parent" (index, -1 the root), "id" (the
                GameObject's path id), "active", "layer", "tag",
                "rect": {"anchorMin", "anchorMax", "anchoredPosition", "sizeDelta", "pivot"}
                        or null (a plain Transform),
                "localPosition" [x, y, z], "localRotation" [x, y, z, w], "localScale" [x, y, z],
                "components": [{"type": a native type's name or a script's class name,
                                "assembly" (scripts: UnityEngine.UI, Assembly-CSharp...),
                                "enabled" (Behaviours), fields...}, ...]}
         A reference is null (unset), {"path": p} (an object of the same tree; with
         "component": type for a component), {"root": name, "path": p} (another tree of the
         same file), or {"asset": type, "name": n, "file": f, "id": path id} (anything else:
         a sprite, a font, a material, a prefab of another file).
       ui/sprites.json  {"<file>:<id>": {"name", "rect" [x, y, w, h], "pivot" [x, y],
                         "border" [left, bottom, right, top], "pixelsPerUnit", "offset" [x, y],
                         "texture": {"name", "file", "id", "width", "height", "format"},
                         "textureRect" [x, y, w, h], "textureRectOffset" [x, y],
                         "packed", "packingMode", "packingRotation", "meshType" (from
                         settingsRaw), "atlasTags", "extrude", "isPolygon"}}
         (every sprite a UI component references)
       ui/fonts.json    {"<file>:<id>": {"name", "fontSize", "lineSpacing", "ascent", "descent",
                         "characterSpacing", "characterPadding", "convertCase", "pixelScale",
                         "tracking", "dynamic" (it embeds font data: glyphs rendered at run time),
                         "fontDataBytes", "fontNames", "fallbackFonts", "renderingMode"
                         (0 smooth, 1 hinted smooth, 2 hinted raster, 3 OS default),
                         "characterRects" (a bitmap font's glyphs), "kerning", "texture",
                         "defaultStyle", "useLegacyBounds", "roundAdvance"}}
                        plus TextMeshPro's font assets ("tmp": true, its face info, glyph count)
       ui/index.json    per source: its roots and node count, every Canvas (path, render mode,
                        sorting, pixel perfect) with its CanvasScaler (mode, reference
                        resolution, match, reference pixels per unit, scale factor)
Floats are written as Python writes the float32 values (exact).
"""
import argparse
import json
import math
from collections import Counter
from pathlib import Path

import UnityPy
from UnityPy.helpers.TypeTreeGenerator import TypeTreeGenerator

# Native components whose fields are written whole (the others: their type alone).
NATIVE_FIELDS = ('Canvas', 'CanvasGroup', 'CanvasRenderer', 'SpriteRenderer', 'SpriteMask', 'Animator', 'Camera')
# Fields every component has (written as "enabled", or not at all).
SKIP_FIELDS = ('m_GameObject', 'm_Script', 'm_Name', 'm_ObjectHideFlags', 'm_CorrespondingSourceObject',
               'm_PrefabInstance', 'm_PrefabAsset', 'm_PrefabParentObject', 'm_PrefabInternal', 'm_Enabled')
# Big arrays written as their length (a font's TrueType data, a texture's bytes).
BLOB_FIELDS = ('m_FontData', 'image data', 'm_StreamData')
BUILTIN_TAGS = {0: 'Untagged', 1: 'Respawn', 2: 'Finish', 3: 'EditorOnly', 5: 'MainCamera', 6: 'Player', 7: 'GameController'}
SCRIPT_ASSEMBLY_SUFFIX = '.dll'


def v2(v):
    return [v['x'], v['y']]


def v3(v):
    return [v['x'], v['y'], v['z']]


def q4(v):
    return [v['x'], v['y'], v['z'], v['w']]


class Files:
    """The game's serialized files, loaded once each, with the type tree generator set; and
    the asset bundles by their CAB names (what a bundle's external references name)."""

    def __init__(self, game):
        self.game = Path(game)
        self.data = self.game / 'Moonlighter_Data'
        self.envs = {}
        self.generator = None
        self.cabs = None

    def generator_for(self, env):
        if self.generator is None:
            sf = next(iter(env.files.values()))
            self.generator = TypeTreeGenerator(str(sf.unity_version))
            self.generator.load_local_game(str(self.game))
        return self.generator

    def load(self, path):
        key = str(path)
        if key not in self.envs:
            env = UnityPy.load(key)
            env.typetree_generator = self.generator_for(env)
            self.envs[key] = env
        return self.envs[key]

    def bundles(self):
        root = self.data / 'StreamingAssets' / 'AssetBundles'
        out = [p for p in sorted(root.iterdir()) if p.is_file() and p.suffix != '.manifest' and p.name != 'AssetBundles']
        dlc = self.game / 'dlc'
        if dlc.is_dir():
            out += [p for p in sorted(dlc.iterdir()) if p.is_file() and p.suffix != '.manifest']
        return out

    def serialized(self, env):
        """The serialized files of an environment (a bundle holds one or more)."""
        out = []

        def walk(f):
            if getattr(f, 'objects', None) is not None and hasattr(f, 'externals'):
                out.append(f)
            for child in (getattr(f, 'files', None) or {}).values():
                walk(child)

        for f in env.files.values():
            walk(f)
        return out

    def external(self, sf, file_id):
        """(file name, its SerializedFile) of an external reference of sf; the file None if
        it can't be found."""
        ext = sf.externals[file_id - 1]
        path = ext.path
        name = path.replace('\\', '/').split('/')[-1]
        if path.startswith('archive:/'):
            cab = path.split('/')[1]
            if self.cabs is None:
                self.cabs = {}
                for bundle in self.bundles():
                    for f in self.serialized(self.load(bundle)):
                        self.cabs[f.name.split('/')[-1]] = (bundle.name, f)
            if cab in self.cabs:
                return self.cabs[cab]
            return name, None
        lower = path.lower()
        if lower.startswith('library/'):
            candidate = self.data / 'Resources' / name
        else:
            candidate = self.data / name
        if not candidate.exists():
            return name, None
        files = self.serialized(self.load(candidate))
        return name, files[0] if files else None


class Source:
    """One serialized file's UI trees."""

    def __init__(self, files, sf, label, scene=None, resources_paths=None):
        self.files = files
        self.sf = sf
        self.label = label
        self.scene = scene
        self.objects = sf.objects
        self.trees = {}
        self.script_names = {}
        self.resources_paths = resources_paths or {}
        self.sprites = {}
        self.fonts = {}
        self.asset_cache = {}

    def tree(self, path_id):
        if path_id not in self.trees:
            obj = self.objects.get(path_id)
            self.trees[path_id] = obj.read_typetree() if obj is not None else None
        return self.trees[path_id]

    def script(self, obj):
        """(assembly, class name) of a MonoBehaviour object."""
        head = obj.parse_monobehaviour_head()
        key = (head.m_Script.m_FileID, head.m_Script.m_PathID)
        if key not in self.script_names:
            try:
                sc = head.m_Script.deref_parse_as_object()
                asm = sc.m_AssemblyName
                if asm.endswith(SCRIPT_ASSEMBLY_SUFFIX):
                    asm = asm[:-len(SCRIPT_ASSEMBLY_SUFFIX)]
                name = sc.m_ClassName if not sc.m_Namespace else f'{sc.m_Namespace}.{sc.m_ClassName}'
                self.script_names[key] = (asm, name)
            except Exception:  # noqa: BLE001 - a missing script
                self.script_names[key] = (None, None)
        return self.script_names[key]

    # --- references ---

    def asset(self, file_id, path_id):
        key = (file_id, path_id)
        if key in self.asset_cache:
            return self.asset_cache[key]
        if file_id == 0:
            fname, sf = self.sf.name.replace('\\', '/').split('/')[-1], self.sf
        else:
            fname, sf = self.files.external(self.sf, file_id)
        out = {'asset': None, 'name': None, 'file': fname, 'id': path_id}
        obj = sf.objects.get(path_id) if sf is not None else None
        if obj is not None:
            out['asset'] = obj.type.name
            try:
                t = obj.read_typetree()
            except Exception:  # noqa: BLE001 - unreadable: its type alone
                t = None
            if isinstance(t, dict):
                out['name'] = t.get('m_Name')
                if obj.type.name == 'MonoBehaviour':
                    asm, cls = Source(self.files, sf, '').script(obj)
                    out['script'] = cls
                if obj.type.name == 'Sprite':
                    self.sprites[f'{fname}:{path_id}'] = sprite_info(self.files, sf, t)
                if obj.type.name == 'Font':
                    self.fonts[f'{fname}:{path_id}'] = font_info(self.files, sf, t)
                if obj.type.name == 'MonoBehaviour' and out.get('script') == 'TMPro.TMP_FontAsset':
                    self.fonts[f'{fname}:{path_id}'] = tmp_font_info(t)
        self.asset_cache[key] = out
        return out

    def reference(self, ref, here):
        """A PPtr as the module docstring says; `here` maps this file's object ids to
        (root name, path, component type or None)."""
        if ref['m_PathID'] == 0:
            return None
        if ref['m_FileID'] == 0 and ref['m_PathID'] in here:
            root, path, comp = here[ref['m_PathID']]
            out = {'path': path} if root is None else {'root': root, 'path': path}
            if comp is not None:
                out['component'] = comp
            return out
        return self.asset(ref['m_FileID'], ref['m_PathID'])

    def values(self, value, here):
        if isinstance(value, dict):
            if set(value.keys()) == {'m_FileID', 'm_PathID'}:
                return self.reference(value, here)
            return {k: (len(v) if k in BLOB_FIELDS and isinstance(v, (list, bytes)) else self.values(v, here))
                    for k, v in value.items()}
        if isinstance(value, list):
            return [self.values(v, here) for v in value]
        if isinstance(value, (bytes, bytearray)):
            return len(value)
        return value


def sprite_info(files, sf, t):
    rd = t['m_RD']
    raw = rd.get('settingsRaw', 0)
    tex = rd['texture']
    texture = None
    if tex['m_PathID'] != 0:
        tsf = sf if tex['m_FileID'] == 0 else files.external(sf, tex['m_FileID'])[1]
        tfile = sf.name.replace('\\', '/').split('/')[-1] if tex['m_FileID'] == 0 else files.external(sf, tex['m_FileID'])[0]
        texture = {'name': None, 'file': tfile, 'id': tex['m_PathID']}
        tobj = tsf.objects.get(tex['m_PathID']) if tsf is not None else None
        if tobj is not None:
            tt = tobj.read_typetree()
            texture.update({'name': tt.get('m_Name'), 'width': tt.get('m_Width'), 'height': tt.get('m_Height'),
                            'format': tt.get('m_TextureFormat'), 'filter': tt.get('m_TextureSettings', {}).get('m_FilterMode')})
    rect, b = t['m_Rect'], t['m_Border']
    tr = rd['textureRect']
    return {'name': t['m_Name'], 'rect': [rect['x'], rect['y'], rect['width'], rect['height']], 'pivot': v2(t['m_Pivot']),
            'border': [b['x'], b['y'], b['z'], b['w']], 'pixelsPerUnit': t['m_PixelsToUnits'], 'offset': v2(t['m_Offset']),
            'texture': texture, 'textureRect': [tr['x'], tr['y'], tr['width'], tr['height']],
            'textureRectOffset': v2(rd['textureRectOffset']),
            # SpriteSettings: packed (bit 0), packingMode (bit 1: 0 tight, 1 rectangle),
            # packingRotation (bits 2-5), meshType (bit 6: 0 full rect, 1 tight)
            'packed': raw & 1, 'packingMode': (raw >> 1) & 1, 'packingRotation': (raw >> 2) & 0xF, 'meshType': (raw >> 6) & 1,
            'atlasTags': t.get('m_AtlasTags', []), 'extrude': t.get('m_Extrude'), 'isPolygon': t.get('m_IsPolygon')}


def font_info(files, sf, t):
    tex = t.get('m_Texture', {'m_PathID': 0})
    texture = None
    if tex['m_PathID'] != 0 and tex['m_FileID'] == 0:
        tobj = sf.objects.get(tex['m_PathID'])
        if tobj is not None:
            tt = tobj.read_typetree()
            texture = {'name': tt.get('m_Name'), 'width': tt.get('m_Width'), 'height': tt.get('m_Height'),
                       'format': tt.get('m_TextureFormat')}
    fallbacks = []
    for ref in t.get('m_FallbackFonts', []):
        obj = sf.objects.get(ref['m_PathID']) if ref['m_FileID'] == 0 else None
        fallbacks.append(obj.read_typetree().get('m_Name') if obj is not None else None)
    data = t.get('m_FontData', [])
    return {'name': t['m_Name'], 'fontSize': t.get('m_FontSize'), 'lineSpacing': t.get('m_LineSpacing'),
            'ascent': t.get('m_Ascent'), 'descent': t.get('m_Descent'), 'characterSpacing': t.get('m_CharacterSpacing'),
            'characterPadding': t.get('m_CharacterPadding'), 'convertCase': t.get('m_ConvertCase'),
            'pixelScale': t.get('m_PixelScale'), 'tracking': t.get('m_Tracking'), 'asciiStartOffset': t.get('m_AsciiStartOffset'),
            'dynamic': len(data) > 0, 'fontDataBytes': len(data), 'fontNames': t.get('m_FontNames', []),
            'fallbackFonts': fallbacks, 'renderingMode': t.get('m_FontRenderingMode'),
            'characterRects': [[c['index'], c['uv']['x'], c['uv']['y'], c['uv']['width'], c['uv']['height'],
                                c['vert']['x'], c['vert']['y'], c['vert']['width'], c['vert']['height'], c['advance'],
                                bool(c.get('flipped', False))] for c in t.get('m_CharacterRects', [])],
            'kerning': [kerning_pair(k) for k in t.get('m_KerningValues', [])],
            'texture': texture, 'defaultStyle': t.get('m_DefaultStyle'),
            'useLegacyBounds': t.get('m_UseLegacyBoundsCalculation'), 'roundAdvance': t.get('m_ShouldRoundAdvanceValue')}


def kerning_pair(k):
    """A kerning entry ((first char, second char), amount), as a tuple or as a dict."""
    if isinstance(k, dict):
        return [k['first']['first'], k['first']['second'], k['second']]
    (a, b), amount = k
    return [a, b, amount]


def tmp_font_info(t):
    return {'name': t.get('m_Name'), 'tmp': True, 'faceInfo': t.get('m_fontInfo'),
            'glyphs': len(t.get('m_glyphInfoList', [])), 'atlas': (t.get('atlas') or {}).get('m_PathID')}


def tag_names(files):
    env = files.load(files.data / 'globalgamemanagers')
    for obj in env.objects:
        if obj.type.name == 'TagManager':
            tm = obj.read_typetree()
            return tm.get('tags', []), {(layer['uniqueID'] & 0xFFFFFFFF): layer['name'] for layer in tm.get('m_SortingLayers', [])}
    return [], {}


def build_settings(files):
    env = files.load(files.data / 'globalgamemanagers')
    scenes, resources = [], {}
    for obj in env.objects:
        if obj.type.name == 'BuildSettings':
            scenes = obj.read_typetree().get('scenes', [])
        if obj.type.name == 'ResourceManager':
            for path, ref in obj.read_typetree()['m_Container']:
                resources.setdefault((ref['m_FileID'], ref['m_PathID']), path)
    return scenes, resources


def extract(files, source, tags, sorting_layers):
    """The UI trees of one file: (output dict, index entry)."""
    sf = source.sf
    transforms = [o for o in sf.objects.values() if o.type.name in ('Transform', 'RectTransform')]
    rect_ids = {o.path_id for o in transforms if o.type.name == 'RectTransform'}
    if not rect_ids:
        return None, None
    # Roots, and which hold a RectTransform somewhere below.
    parent = {}
    for o in transforms:
        t = source.tree(o.path_id)
        parent[o.path_id] = t['m_Father']['m_PathID'] if t['m_Father']['m_FileID'] == 0 else 0
    ui_roots = set()
    for pid in rect_ids:
        p = pid
        while parent.get(p, 0) != 0 and parent[p] in parent:
            p = parent[p]
        ui_roots.add(p)

    def tag_name(tag):
        if tag >= 20000:
            return tags[tag - 20000] if tag - 20000 < len(tags) else str(tag)
        return BUILTIN_TAGS.get(tag, str(tag))

    # Every object's (root name, path, component) for references; per root, the paths.
    here = {}
    root_names = {}
    order = sorted(ui_roots, key=lambda pid: (source.tree(source.tree(pid)['m_GameObject']['m_PathID'])['m_Name'], pid))
    for root in order:
        rname = source.tree(source.tree(root)['m_GameObject']['m_PathID'])['m_Name']
        root_names[root] = rname

        def mark(tid, path):
            tr = source.tree(tid)
            go_id = tr['m_GameObject']['m_PathID']
            here[go_id] = (root, path, None)
            for c in source.tree(go_id)['m_Component']:
                ref = c['component']
                if ref['m_FileID'] == 0 and ref['m_PathID'] in sf.objects:
                    obj = sf.objects[ref['m_PathID']]
                    ctype = obj.type.name
                    if ctype == 'MonoBehaviour':
                        ctype = source.script(obj)[1] or 'MonoBehaviour'
                    here[ref['m_PathID']] = (root, path, ctype)
            for child in tr['m_Children']:
                ctr = source.tree(child['m_PathID'])
                if ctr is None:
                    continue
                cname = source.tree(ctr['m_GameObject']['m_PathID'])['m_Name']
                mark(child['m_PathID'], cname if path == '' else f'{path}/{cname}')

        mark(root, '')

    roots_out, canvases = [], []
    counts = Counter()
    for root in order:
        rname = root_names[root]
        # References into this tree are plain paths; into other trees, root and path.
        local = {k: ((None if r == root else root_names[r]), p, c) for k, (r, p, c) in here.items()}
        nodes = []

        def walk(tid, path, parent_index):
            tr = source.tree(tid)
            go_id = tr['m_GameObject']['m_PathID']
            go = source.tree(go_id)
            obj = sf.objects[tid]
            node = {'path': path, 'parent': parent_index, 'id': go_id, 'active': bool(go['m_IsActive']),
                    'layer': go['m_Layer'], 'tag': tag_name(go['m_Tag']), 'rect': None,
                    'localPosition': v3(tr['m_LocalPosition']), 'localRotation': q4(tr['m_LocalRotation']),
                    'localScale': v3(tr['m_LocalScale']), 'components': []}
            if obj.type.name == 'RectTransform':
                node['rect'] = {'anchorMin': v2(tr['m_AnchorMin']), 'anchorMax': v2(tr['m_AnchorMax']),
                                'anchoredPosition': v2(tr['m_AnchoredPosition']), 'sizeDelta': v2(tr['m_SizeDelta']),
                                'pivot': v2(tr['m_Pivot'])}
            index = len(nodes)
            nodes.append(node)
            for c in go['m_Component']:
                ref = c['component']
                cobj = sf.objects.get(ref['m_PathID']) if ref['m_FileID'] == 0 else None
                if cobj is None or cobj.type.name in ('Transform', 'RectTransform'):
                    continue
                ctype = cobj.type.name
                comp = {'type': ctype}
                fields = None
                if ctype == 'MonoBehaviour':
                    asm, cls = source.script(cobj)
                    comp = {'type': cls or 'MonoBehaviour', 'assembly': asm}
                    try:
                        fields = source.tree(cobj.path_id)
                    except Exception as e:  # noqa: BLE001 - recorded, not hidden
                        comp['error'] = f'{type(e).__name__}: {e}'
                elif ctype in NATIVE_FIELDS:
                    fields = source.tree(cobj.path_id)
                if isinstance(fields, dict):
                    if 'm_Enabled' in fields:
                        comp['enabled'] = bool(fields['m_Enabled'])
                    for k, v in fields.items():
                        if k not in SKIP_FIELDS:
                            comp[k] = source.values(v, local)
                    if ctype == 'Canvas':
                        comp['sortingLayerName'] = sorting_layers.get(fields.get('m_SortingLayerID', 0) & 0xFFFFFFFF, 'Default')
                counts[comp['type']] += 1
                node['components'].append(comp)
            for child in tr['m_Children']:
                ctr = source.tree(child['m_PathID'])
                if ctr is None:
                    continue
                cname = source.tree(ctr['m_GameObject']['m_PathID'])['m_Name']
                walk(child['m_PathID'], cname if path == '' else f'{path}/{cname}', index)

        walk(root, '', -1)
        go_id = source.tree(root)['m_GameObject']['m_PathID']
        res = source.resources_paths.get(go_id)
        roots_out.append({'name': rname, 'id': go_id, 'resources': res, 'nodes': nodes})
        for node in nodes:
            canvas = next((c for c in node['components'] if c['type'] == 'Canvas'), None)
            if canvas is None:
                continue
            scaler = next((c for c in node['components'] if c['type'] == 'UnityEngine.UI.CanvasScaler'), None)
            entry = {'root': rname, 'path': node['path'], 'active': node['active'], 'renderMode': canvas.get('m_RenderMode'),
                     'sortingLayer': canvas.get('sortingLayerName'), 'sortingOrder': canvas.get('m_SortingOrder'),
                     'overrideSorting': canvas.get('m_OverrideSorting'), 'pixelPerfect': canvas.get('m_PixelPerfect'),
                     'camera': canvas.get('m_Camera') is not None, 'planeDistance': canvas.get('m_PlaneDistance'),
                     'nested': node['parent'] != -1}
            if scaler is not None:
                entry['scaler'] = {'mode': scaler.get('m_UiScaleMode'), 'referenceResolution': v2(scaler['m_ReferenceResolution']),
                                   'screenMatchMode': scaler.get('m_ScreenMatchMode'), 'match': scaler.get('m_MatchWidthOrHeight'),
                                   'referencePixelsPerUnit': scaler.get('m_ReferencePixelsPerUnit'),
                                   'scaleFactor': scaler.get('m_ScaleFactor'), 'dynamicPixelsPerUnit': scaler.get('m_DynamicPixelsPerUnit')}
            canvases.append(entry)
    out = {'file': source.label, 'scene': source.scene, 'roots': roots_out}
    index = {'file': source.label, 'scene': source.scene, 'roots': [[r['name'], len(r['nodes']), r['resources']] for r in roots_out],
             'canvases': canvases, 'components': dict(counts.most_common())}
    return out, index


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    files = Files(args.game)
    tags, sorting_layers = tag_names(files)
    scenes, resources = build_settings(files)
    out_dir = Path(args.out) / 'ui'
    out_dir.mkdir(parents=True, exist_ok=True)
    sources = []
    for i, scene in enumerate(scenes):
        sources.append((files.data / f'level{i}', f'scene_{Path(scene).stem}', scene))
    for name in ('resources.assets',) + tuple(sorted(p.name for p in files.data.glob('sharedassets*.assets'))):
        sources.append((files.data / name, name, None))
    for bundle in files.bundles():
        sources.append((bundle, f'bundle_{bundle.name}', None))
    sprites, fonts, index = {}, {}, []
    for path, label, scene in sources:
        env = files.load(path)
        for k, sf in enumerate(files.serialized(env)):
            res = {}
            if label == 'resources.assets':
                # The container's references are from globalgamemanagers: its file ids name
                # resources.assets through its externals; keep those that point here.
                gg = files.serialized(files.load(files.data / 'globalgamemanagers'))[0]
                for (fid, pid), rpath in resources.items():
                    if fid != 0 and gg.externals[fid - 1].path.endswith('resources.assets'):
                        res[pid] = rpath
            source = Source(files, sf, label if k == 0 else f'{label}#{k}', scene, res)
            data, entry = extract(files, source, tags, sorting_layers)
            sprites.update(source.sprites)
            fonts.update(source.fonts)
            if data is None:
                continue
            target = out_dir / f"{source.label.replace('#', '_')}.json"
            target.write_text(json.dumps(data, separators=(',', ':')), encoding='utf-8')
            index.append(entry)
            nodes = sum(len(r['nodes']) for r in data['roots'])
            print(f'   {source.label}: {len(data["roots"])} UI trees, {nodes} objects, {len(entry["canvases"])} canvases', flush=True)
    (out_dir / 'sprites.json').write_text(json.dumps(sprites, separators=(',', ':')), encoding='utf-8')
    (out_dir / 'fonts.json').write_text(json.dumps(fonts, indent=1), encoding='utf-8')
    (out_dir / 'index.json').write_text(json.dumps(index, indent=1), encoding='utf-8')
    print(f'   {len(sprites)} sprites, {len(fonts)} fonts', flush=True)


if __name__ == '__main__':
    main()

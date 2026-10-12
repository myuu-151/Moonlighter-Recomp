"""Animator controllers and their clips, for the port's Animator (unity/Animator), and the
sprites they show as a bank of their own. Any AnimatorController, or AnimatorOverrideController
(its controller with the override's clips), found by bundle and name; by default the ones
the port uses (TARGETS):

  hero_merchant / HeroMerchant (override of EmptyHeroMerchant) -> animators/hero_merchant.json,
      sprites/hero/ (what the clips show, plus every SpriteRenderer's sprite in the bundle)
  enemies_golem / BabySlime (controller)                       -> animators/BabySlime.json,
      sprites/enemies/BabySlime/ (what the clips show, plus its prefab's renderers' sprites
      and its Enemy's dead sprite)
  enemies_golem / GolemSoldier (controller), exact             -> animators/GolemSoldier.json,
      sprites/enemies/GolemSoldier/ (as BabySlime's; its clips drive the AttackCollider's
      BoxCollider2D enabled flag and size and its Transform's local position)
  items / BroomSpear (override of EmptyTrainingSpear), exact   -> animators/broom_spear.json,
      sprites/weapons/broom_spear/ (what the clips show: the Broom Spear prefab's renderers
      have no sprite of their own)
  items / NewGlovesForestGloves (override of EmptyTrainingGloves), exact
                                                               -> animators/forest_gloves.json,
      sprites/weapons/forest_gloves/ (the Forest Gloves', what the oracle's save slot holds)
  enemies_golem / Tangle (controller), exact                   -> animators/Tangle.json,
      sprites/enemies/Tangle/ (its cycle and its dead sprite)
  enemies_golem / HermitSlime (controller), exact              -> animators/HermitSlime.json,
      sprites/enemies/HermitSlime/ (its clips also drive the root's BoxCollider2D and the
      AttackCollider's)

  animators/<name>.json:
    controller, override (the override's name, null for a plain controller)
    params      [{name, id, type (1 float, 3 int, 4 bool, 9 trigger), default}]
    states      [{name, hash (shortNameHash), fullPathHash, tag, tagHash, speed, speedParam,
                  loop, cycleOffset, clip (index into clips, -1 none), transitions [T]}]
    anyState    [T], defaultState
                T = {dest, duration, offset, exitTime, hasExitTime, fixedDuration,
                     interruption (0 none, 1 source, 2 destination, 3 source then destination,
                     4 destination then source), ordered, canSelf, conditions [[mode, param,
                     threshold]]}; mode 1 If, 2 IfNot, 3 Greater, 4 Less, 6 Equals, 7 NotEqual
    clips       [{name, length, loop, rate, sprites [[time, sprite]], events [[time, function,
                  string, float, int]], curves [{path, type, attribute, keys [[time, value]]}]}]
  sprites/<bank>/: index.bin and <n>.tex as sprites.py writes them.

An exact target (--exact) adds what the port needs to play the clips' other curves as Unity
does (the targets before it keep the format above, unchanged):
    updateMode  the Animator's (0 normal, 1 animate physics, 2 unscaled time; null if no
                prefab of the bundle plays it)
    states      each also has writeDefaults (m_WriteDefaultValues)
    clips       each also has spritePath (the renderer its sprite curve drives, null if none)
                and its curves are, in the clip's curve order,
                {path, type, attribute, component, source, ...}:
      path       the object's path below the Animator's (the bindings hold its CRC32; the
                 paths of the prefabs whose Animator plays the controller), else the number
      attribute  the property's name (ATTRIBUTES by CRC32; a Transform's by number,
                 TRANSFORM_ATTRIBUTES, component its x, y, z (w)), else the number
      source 'streamed': keys [[time, c0, c1, c2, c3]], the first at -FLT_MAX: the value at t
                 is the cubic of the last key at or before t, in d = t - time:
                 ((c0 d + c1) d + c2) d + c3 (a step or a constant segment has c0..c2 0)
      source 'dense':    begin, rate, samples [the value per frame from begin, 1 / rate apart]
      source 'constant': value

Enemies get a bank each (sprites/enemies/<name>/) rather than one per enemy bundle: a floor
holds the few enemies its rooms spawn, and an enemy bundle's every clip frame (enemies_golem:
2050 sprites) would take megabytes of the GameCube's 24 MB beside the dungeon's own sprites;
per enemy, a floor loads only the banks of the enemies it can spawn.

The compiled clips keep their curves as Unity's streamed clip (per key: curve index and a
cubic's four coefficients from the key's time), dense samples and constants, in that curve
order; bindings map them to properties, a sprite curve's value indexing pptrCurveMapping.

One layer, one clip per state: blend trees and further layers are refused (an error naming
the controller and state), the port's Animator plays neither.

    python tools/extract/animators.py --game <the game's folder> --out <folder>
        (the TARGETS)
    python tools/extract/animators.py --game ... --out ... --bundle <bundle> --name <name>
        [--kind controller|override] [--json <file name>] [--sprites <bank folder>] [--exact]
"""
import argparse
import json
import shutil
import struct
import zlib
from pathlib import Path

import UnityPy

from sprites import texels

# (bundle, controller name, kind, json name, sprite bank, where the bank's extra sprites come from:
#  'bundle' every SpriteRenderer in the bundle, 'prefab' the prefabs whose Animator plays it;
#  exact: the exact curves, paths, write defaults and update mode)
TARGETS = (
    ('hero_merchant', 'HeroMerchant', 'override', 'hero_merchant', 'hero', 'bundle', False),
    ('enemies_golem', 'BabySlime', 'controller', 'BabySlime', 'enemies/BabySlime', 'prefab', False),
    ('enemies_golem', 'GolemSoldier', 'controller', 'GolemSoldier', 'enemies/GolemSoldier', 'prefab', True),
    ('items', 'BroomSpear', 'override', 'broom_spear', 'weapons/broom_spear', 'prefab', True),
    ('items', 'NewGlovesForestGloves', 'override', 'forest_gloves', 'weapons/forest_gloves', 'prefab', True),
    ('enemies_golem', 'Tangle', 'controller', 'Tangle', 'enemies/Tangle', 'prefab', True),
    ('enemies_golem', 'HermitSlime', 'controller', 'HermitSlime', 'enemies/HermitSlime', 'prefab', True),
)
KINDS = {'controller': 'AnimatorController', 'override': 'AnimatorOverrideController'}
# Sprite fields of a prefab's MonoBehaviours the bank needs too (Enemy: what it leaves when it dies).
SPRITE_FIELDS = ('enemyDeadSprite',)

# Transform attributes take several curves (position, rotation quaternion, scale, euler).
TRANSFORM_CURVES = {1: 3, 2: 4, 3: 3, 4: 3}
ATTRIBUTES = {zlib.crc32(n.encode()): n for n in (
    'm_IsActive', 'm_Enabled', 'm_FlipX', 'm_FlipY', 'm_Color.r', 'm_Color.g', 'm_Color.b', 'm_Color.a',
    'm_SortingOrder', 'm_Size.x', 'm_Size.y', 'm_Offset.x', 'm_Offset.y', 'm_Sprite', 'm_Radius',
    'm_IsTrigger', 'm_Density', 'm_EdgeRadius', 'm_Simulated', 'm_Mass', 'm_GravityScale')}
# A Transform binding's attribute is a number, not a CRC32 (its curves the x, y, z (w) components).
TRANSFORM_ATTRIBUTES = {1: 'm_LocalPosition', 2: 'm_LocalRotation', 3: 'm_LocalScale', 4: 'localEulerAnglesRaw'}


def signed(h):
    return h - (1 << 32) if h >= 1 << 31 else h


def curve_count(binding):
    if binding['typeID'] == 4:
        return TRANSFORM_CURVES.get(binding['attribute'], 1)
    return 1


def streamed_keys(words):
    """[(time, [(curve, (c0, c1, c2, c3))])] from the streamed clip's u32 words."""
    raw = struct.pack('<%dI' % len(words), *words)
    pos, frames = 0, []
    while pos + 8 <= len(raw):
        time, n = struct.unpack_from('<fI', raw, pos)
        pos += 8
        keys = []
        for _ in range(n):
            curve = struct.unpack_from('<I', raw, pos)[0]
            keys.append((curve, struct.unpack_from('<4f', raw, pos + 4)))
            pos += 20
        frames.append((time, keys))
    return frames


def clip_curves(clip):
    """{curve index: [(time, value)]}: each key's value at its own time (the segment's
    constant term; sprite curves are steps)."""
    data = clip['m_MuscleClip']['m_Clip']['data']
    out = {}
    for time, keys in streamed_keys(data['m_StreamedClip']['data']):
        if time == float('inf') or time == float('-inf'):
            continue
        for curve, c in keys:
            out.setdefault(curve, []).append((time, c[3]))
    base = data['m_StreamedClip']['curveCount']
    dense = data['m_DenseClip']
    for f in range(dense['m_FrameCount']):
        t = dense['m_BeginTime'] + f / dense['m_SampleRate']
        for k in range(dense['m_CurveCount']):
            out.setdefault(base + k, []).append((t, dense['m_SampleArray'][f * dense['m_CurveCount'] + k]))
    base += dense['m_CurveCount']
    for k, v in enumerate(data['m_ConstantClip']['data']):
        out.setdefault(base + k, []).append((0.0, v))
    return out


def clip_curves_exact(clip):
    """{curve index: what the clip holds for it}: a streamed curve's keys (each key's time and
    its cubic's four coefficients, from the -FLT_MAX key on), a dense curve's samples, a constant."""
    data = clip['m_MuscleClip']['m_Clip']['data']
    out = {}
    for time, keys in streamed_keys(data['m_StreamedClip']['data']):
        if time == float('inf'):
            if keys:  # the end marker holds no keys (and JSON has no infinity to write them at)
                raise SystemExit(f"{clip['m_Name']}: streamed keys at +inf")
            continue
        for curve, c in keys:
            out.setdefault(curve, {'source': 'streamed', 'keys': []})['keys'].append([time, *c])
    base = data['m_StreamedClip']['curveCount']
    dense = data['m_DenseClip']
    for k in range(dense['m_CurveCount']):
        out[base + k] = {'source': 'dense', 'begin': dense['m_BeginTime'], 'rate': dense['m_SampleRate'],
                         'samples': [dense['m_SampleArray'][f * dense['m_CurveCount'] + k] for f in range(dense['m_FrameCount'])]}
    base += dense['m_CurveCount']
    for k, v in enumerate(data['m_ConstantClip']['data']):
        out[base + k] = {'source': 'constant', 'value': v}
    return out


def animator_paths(env, controller_ids):
    """({CRC32 of a path below the Animator's object: the path}, {update modes}) of the Animators
    playing the controller: a clip binding's path is such a CRC32."""
    objs = {o.path_id: o for o in env.objects}
    paths, modes = {}, set()

    def walk(transform_id, path):
        t = objs[transform_id].read_typetree()
        paths[zlib.crc32(path.encode())] = path
        for child in t['m_Children']:
            if child['m_FileID'] == 0 and child['m_PathID'] in objs:
                c = objs[child['m_PathID']].read_typetree()
                name = objs[c['m_GameObject']['m_PathID']].read_typetree()['m_Name']
                walk(child['m_PathID'], name if path == '' else f'{path}/{name}')

    for o in env.objects:
        if o.type.name != 'Animator':
            continue
        a = o.read_typetree()
        if a['m_Controller']['m_FileID'] != 0 or a['m_Controller']['m_PathID'] not in controller_ids:
            continue
        modes.add(a['m_UpdateMode'])
        go = objs[a['m_GameObject']['m_PathID']].read_typetree()
        for comp in go['m_Component']:
            ref = comp['component']
            if ref['m_FileID'] == 0 and ref['m_PathID'] in objs and objs[ref['m_PathID']].type.name in ('Transform', 'RectTransform'):
                walk(ref['m_PathID'], '')
    return paths, modes


def find_controller(env, name, kind=None):
    """The AnimatorController or AnimatorOverrideController of that name (of that kind, if
    given). Exits if there's none, or more than one and no kind to choose."""
    types = [KINDS[kind]] if kind else list(KINDS.values())
    found = [o for o in env.objects if o.type.name in types and o.peek_name() == name]
    if not found:
        raise SystemExit(f"no {' or '.join(types)} named {name}")
    if len(found) > 1:
        raise SystemExit(f"{len(found)} controllers named {name} ({', '.join(o.type.name for o in found)}): "
                         "choose one with --kind")
    return found[0]


def extract(env, name, kind=None, exact=False):
    """(controller json, {sprite name: path id} its clips show, the controllers' path ids);
    exact: with the exact curves, paths, write defaults and update mode (the module's doc)."""
    objs = {o.path_id: o for o in env.objects}
    names = {}

    def name_of(pptr):
        if pptr['m_FileID'] != 0 or pptr['m_PathID'] not in objs:
            return None
        if pptr['m_PathID'] not in names:
            names[pptr['m_PathID']] = objs[pptr['m_PathID']].read_typetree()['m_Name']
        return names[pptr['m_PathID']]

    found = find_controller(env, name, kind)
    if found.type.name == 'AnimatorOverrideController':
        override = found.read_typetree()
        replace = {p['m_OriginalClip']['m_PathID']: p['m_OverrideClip']['m_PathID'] for p in override['m_Clips']}
        ac_obj = objs[override['m_Controller']['m_PathID']]
    else:
        override, replace, ac_obj = None, {}, found
    ac = ac_obj.read_typetree()
    label = ac['m_Name'] + (f" (override {override['m_Name']})" if override else '')
    tos = {int(k): v for k, v in ac['m_TOS']}
    tos.update({zlib.crc32(v.encode()): v for v in tos.values()})
    paths, modes = animator_paths(env, {found.path_id, ac_obj.path_id}) if exact else ({}, set())
    if len(modes) > 1:
        raise SystemExit(f'{label}: its Animators have different update modes {sorted(modes)}')

    def path_name(crc):
        return paths.get(crc, tos.get(crc, crc))

    c = ac['m_Controller']
    if len(c['m_LayerArray']) != 1 or len(c['m_StateMachineArray']) != 1:
        raise SystemExit(f"{label}: {len(c['m_LayerArray'])} layers, {len(c['m_StateMachineArray'])} state machines: "
                         "only one layer is supported")
    values = c['m_Values']['data']['m_ValueArray']
    defaults = c['m_DefaultValues']['data']
    param_index = {v['m_ID']: i for i, v in enumerate(values)}
    params = []
    for v in values:
        t, i = v['m_Type'], v['m_Index']
        default = (defaults['m_FloatValues'][i] if t == 1 else defaults['m_IntValues'][i] if t == 3
                   else bool(defaults['m_BoolValues'][i]))
        params.append({'name': tos.get(v['m_ID'], str(v['m_ID'])), 'id': signed(v['m_ID']), 'type': t, 'default': default})

    clips, clip_index, sprites_wanted = [], {}, {}

    def clip(path_id):
        path_id = replace.get(path_id, path_id)
        if path_id in clip_index:
            return clip_index[path_id]
        t = objs[path_id].read_typetree()
        b = t['m_ClipBindingConstant']
        mapping = [name_of(p) for p in b['pptrCurveMapping']]
        for p in b['pptrCurveMapping']:
            if p['m_FileID'] == 0 and p['m_PathID'] in objs:
                sprites_wanted[name_of(p)] = p['m_PathID']
        curves = clip_curves_exact(t) if exact else clip_curves(t)
        entry = {'name': t['m_Name'], 'length': t['m_MuscleClip']['m_StopTime'] - t['m_MuscleClip']['m_StartTime'],
                 'loop': bool(t['m_MuscleClip']['m_LoopTime']), 'rate': t['m_SampleRate'], 'sprites': [],
                 'events': [[e['time'], e['functionName'], e['data'], e['floatParameter'], e['intParameter']]
                            for e in t['m_Events']], 'curves': []}
        first = 0
        if exact:
            entry['spritePath'] = None
            for binding in b['genericBindings']:
                n = curve_count(binding)
                if binding['isPPtrCurve']:
                    if binding['typeID'] == 212 and binding['customType'] == 23:
                        if entry['spritePath'] is not None:
                            raise SystemExit(f"{label}: clip {t['m_Name']} has more than one sprite curve")
                        entry['spritePath'] = path_name(binding['path'])
                        source = curves.get(first, {})
                        if source and source['source'] != 'streamed':
                            raise SystemExit(f"{label}: clip {t['m_Name']}'s sprite curve is {source['source']}")
                        entry['sprites'] = [[k[0], mapping[int(k[4])]] for k in source.get('keys', [])]
                else:
                    attribute = (TRANSFORM_ATTRIBUTES.get(binding['attribute'], binding['attribute']) if binding['typeID'] == 4
                                 else ATTRIBUTES.get(binding['attribute'], binding['attribute']))
                    for k in range(n):
                        entry['curves'].append({'path': path_name(binding['path']), 'type': binding['typeID'],
                                                'attribute': attribute, 'component': k, **curves.get(first + k, {})})
                first += n
            clip_index[path_id] = len(clips)
            clips.append(entry)
            return clip_index[path_id]
        for binding in b['genericBindings']:
            n = curve_count(binding)
            if binding['isPPtrCurve']:
                if binding['typeID'] == 212 and binding['customType'] == 23:
                    entry['sprites'] = [[time, mapping[int(v)]] for time, v in curves.get(first, [])]
            else:
                for k in range(n):
                    entry['curves'].append({'path': tos.get(binding['path'], binding['path']), 'type': binding['typeID'],
                                            'attribute': ATTRIBUTES.get(binding['attribute'], binding['attribute']),
                                            'component': k, 'keys': curves.get(first + k, [])})
            first += n
        clip_index[path_id] = len(clips)
        clips.append(entry)
        return clip_index[path_id]

    def transition(t):
        return {'dest': t['m_DestinationState'], 'duration': t['m_TransitionDuration'], 'offset': t['m_TransitionOffset'],
                'exitTime': t['m_ExitTime'], 'hasExitTime': bool(t['m_HasExitTime']),
                'fixedDuration': bool(t['m_HasFixedDuration']), 'interruption': t['m_InterruptionSource'],
                'ordered': bool(t['m_OrderedInterruption']), 'canSelf': bool(t['m_CanTransitionToSelf']),
                'conditions': [[k['data']['m_ConditionMode'], param_index.get(k['data']['m_EventID'], -1),
                                k['data']['m_EventThreshold']] for k in t['m_ConditionConstantArray']]}

    sm = c['m_StateMachineArray'][0]['data']
    states = []
    for s in sm['m_StateConstantArray']:
        s = s['data']
        if len(s['m_BlendTreeConstantArray']) > 1:
            raise SystemExit(f"{label}: state {tos.get(s['m_NameID'], s['m_NameID'])} has "
                             f"{len(s['m_BlendTreeConstantArray'])} blend trees: blend trees aren't supported")
        nodes = s['m_BlendTreeConstantArray'][0]['data']['m_NodeArray'] if s['m_BlendTreeConstantArray'] else []
        if len(nodes) > 1:
            raise SystemExit(f"{label}: state {tos.get(s['m_NameID'], s['m_NameID'])} is a blend tree ({len(nodes)} nodes): "
                             "blend trees aren't supported (the port's Animator plays one clip per state)")
        clip_id = nodes[0]['data']['m_ClipID'] if len(nodes) == 1 else -1
        exact_fields = {'writeDefaults': bool(s['m_WriteDefaultValues'])} if exact else {}
        states.append({'name': tos.get(s['m_NameID'], str(s['m_NameID'])), 'hash': signed(s['m_NameID']), 'fullPathHash': signed(s['m_FullPathID']),
                       'tag': tos.get(s['m_TagID'], '') if s['m_TagID'] else '', 'tagHash': signed(s['m_TagID']), 'speed': s['m_Speed'],
                       'speedParam': param_index.get(s['m_SpeedParamID'], -1), 'loop': bool(s['m_Loop']),
                       'cycleOffset': s['m_CycleOffset'],
                       'clip': clip(ac['m_AnimationClips'][clip_id]['m_PathID']) if clip_id >= 0 and clip_id != 4294967295 else -1,
                       'transitions': [transition(t['data']) for t in s['m_TransitionConstantArray']]} | exact_fields)
    controller = {'controller': ac['m_Name'], 'override': override['m_Name'] if override else None, 'params': params,
                  'defaultState': sm['m_DefaultState'], 'states': states,
                  'anyState': [transition(t['data']) for t in sm['m_AnyStateTransitionConstantArray']], 'clips': clips}
    if exact:
        controller['updateMode'] = modes.pop() if modes else None
    return controller, sprites_wanted, {found.path_id, ac_obj.path_id}, name_of


def extra_sprites(env, mode, controller_ids, name_of):
    """{sprite name: path id} the bank needs beside the clips': every SpriteRenderer's in the
    bundle ('bundle'), or what the prefabs whose Animator plays the controller show
    ('prefab': their SpriteRenderers' sprites and SPRITE_FIELDS)."""
    objs = {o.path_id: o for o in env.objects}
    out = {}

    def add(pptr):
        if pptr and pptr['m_FileID'] == 0 and pptr['m_PathID'] in objs:
            out.setdefault(name_of(pptr), pptr['m_PathID'])

    if mode == 'bundle':
        for o in env.objects:
            if o.type.name == 'SpriteRenderer':
                add(o.read_typetree()['m_Sprite'])
        return out
    transforms = {}
    for o in env.objects:
        if o.type.name in ('Transform', 'RectTransform'):
            t = o.read_typetree()
            transforms[t['m_GameObject']['m_PathID']] = t
    roots = set()
    for o in env.objects:
        if o.type.name == 'Animator':
            a = o.read_typetree()
            if a['m_Controller']['m_FileID'] == 0 and a['m_Controller']['m_PathID'] in controller_ids:
                t = transforms[a['m_GameObject']['m_PathID']]
                while t['m_Father']['m_PathID'] != 0:
                    t = objs[t['m_Father']['m_PathID']].read_typetree()
                roots.add(t['m_GameObject']['m_PathID'])
    if not roots:
        raise SystemExit('no prefab plays this controller (an Animator with it)')

    def walk(go_id):
        go = objs[go_id].read_typetree()
        for comp in go['m_Component']:
            ref = comp['component']
            o = objs.get(ref['m_PathID']) if ref['m_FileID'] == 0 else None
            if o is None:
                continue
            if o.type.name == 'SpriteRenderer':
                add(o.read_typetree()['m_Sprite'])
            elif o.type.name == 'MonoBehaviour':
                try:
                    mb = o.read_typetree()
                except Exception:  # noqa: BLE001 - no type tree
                    continue
                for f in SPRITE_FIELDS:
                    if isinstance(mb.get(f), dict):
                        add(mb[f])
        for child in transforms[go_id]['m_Children']:
            walk(objs[child['m_PathID']].read_typetree()['m_GameObject']['m_PathID'])

    for root in sorted(roots):
        walk(root)
    return out


def write_bank(env, folder, sprites_wanted):
    """sprites.py's bank (index.bin, <n>.tex) of those sprites, in name order: (count, bytes)."""
    objs = {o.path_id: o for o in env.objects}
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    index, count, total = bytearray(), 0, 0
    for name in sorted(n for n in sprites_wanted if n):
        s = objs[sprites_wanted[name]].read()
        img = s.image.convert('RGBA')
        ppu = s.m_PixelsToUnits or 1.0
        rect, pivot, offset = s.m_Rect, s.m_Pivot, s.m_RD.textureRectOffset
        fmt, data = texels(img)
        (folder / f'{count}.tex').write_bytes(data)
        encoded = name.encode('utf-8')
        index += struct.pack('>H', len(encoded)) + encoded
        index += struct.pack('>HHfffB', img.size[0], img.size[1], (offset.x - pivot.x * rect.width) / ppu,
                             (offset.y - pivot.y * rect.height) / ppu, 1.0 / ppu, fmt)
        count += 1
        total += len(data)
    (folder / 'index.bin').write_bytes(b'MLSI' + struct.pack('>II', 2, count) + bytes(index))
    return count, total


def run(game, out, bundle, name, kind, json_name, bank, mode, exact=False):
    env = UnityPy.load(str(Path(game) / 'Moonlighter_Data' / 'StreamingAssets' / 'AssetBundles' / bundle))
    controller, sprites_wanted, ids, name_of = extract(env, name, kind, exact)
    (out / 'animators').mkdir(parents=True, exist_ok=True)
    (out / 'animators' / f'{json_name}.json').write_text(json.dumps(controller), encoding='utf-8')
    count = total = 0
    if bank:
        for n, path_id in extra_sprites(env, mode, ids, name_of).items():
            sprites_wanted.setdefault(n, path_id)
        count, total = write_bank(env, out / 'sprites' / bank, sprites_wanted)
    print(f"   {json_name}: {len(controller['states'])} states, {len(controller['clips'])} clips, "
          f"{count} sprites, {total // 1024} KB", flush=True)
    if exact:
        numeric = sorted({(c['type'], c['attribute']) for clip in controller['clips'] for c in clip['curves']
                          if not isinstance(c['attribute'], str)})
        unresolved = sorted({c['path'] for clip in controller['clips'] for c in clip['curves'] + [{'path': clip['spritePath']}]
                             if c['path'] is not None and not isinstance(c['path'], str)})
        if numeric or unresolved:
            print(f'   {json_name}: unresolved attributes (type, attribute) {numeric}, paths {unresolved}', flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', required=True)
    parser.add_argument('--out', required=True)
    parser.add_argument('--bundle', help='one controller, from this bundle (else the TARGETS)')
    parser.add_argument('--name', help="the controller's or override's name")
    parser.add_argument('--kind', choices=sorted(KINDS), help='if the name is both a controller and an override')
    parser.add_argument('--json', help='the output file name, without .json (default: --name)')
    parser.add_argument('--sprites', help="the sprite bank's folder below sprites/ (default: none)")
    parser.add_argument('--sprites-from', choices=('bundle', 'prefab'), default='prefab',
                        help="the bank's sprites beside the clips': every renderer in the bundle, or the prefabs playing it")
    parser.add_argument('--exact', action='store_true',
                        help='the exact curves (paths, attributes, keys), write defaults and update mode')
    args = parser.parse_args()
    out = Path(args.out)
    if args.bundle:
        if not args.name:
            parser.error('--bundle needs --name')
        run(args.game, out, args.bundle, args.name, args.kind, args.json or args.name, args.sprites, args.sprites_from,
            args.exact)
    else:
        for target in TARGETS:
            run(args.game, out, *target)


if __name__ == '__main__':
    main()

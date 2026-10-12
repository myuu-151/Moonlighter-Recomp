"""Animator controllers and their clips, for the port's Animator (unity/Animator): the
hero's (bundle hero_merchant: the EmptyHeroMerchant controller with the HeroMerchant
override's clips), and the hero's sprites as a bank of their own.

  animators/<name>.json:
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
  sprites/hero/: index.bin and <n>.tex as sprites.py writes them.

The compiled clips keep their curves as Unity's streamed clip (per key: curve index and a
cubic's four coefficients from the key's time), dense samples and constants, in that curve
order; bindings map them to properties, a sprite curve's value indexing pptrCurveMapping.

    python tools/extract/animators.py --game <the game's folder> --out <folder>
"""
import argparse
import json
import shutil
import struct
import zlib
from pathlib import Path

import UnityPy

from sprites import texels

# Transform attributes take several curves (position, rotation quaternion, scale, euler).
TRANSFORM_CURVES = {1: 3, 2: 4, 3: 3, 4: 3}
ATTRIBUTES = {zlib.crc32(n.encode()): n for n in (
    'm_IsActive', 'm_Enabled', 'm_FlipX', 'm_FlipY', 'm_Color.r', 'm_Color.g', 'm_Color.b', 'm_Color.a',
    'm_SortingOrder', 'm_Size.x', 'm_Size.y', 'm_Offset.x', 'm_Offset.y', 'm_Sprite')}


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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    out = Path(args.out)
    bundle = Path(args.game) / 'Moonlighter_Data' / 'StreamingAssets' / 'AssetBundles' / 'hero_merchant'
    env = UnityPy.load(str(bundle))
    objs = {o.path_id: o for o in env.objects}
    names = {}

    def name_of(pptr):
        if pptr['m_FileID'] != 0 or pptr['m_PathID'] not in objs:
            return None
        if pptr['m_PathID'] not in names:
            names[pptr['m_PathID']] = objs[pptr['m_PathID']].read_typetree()['m_Name']
        return names[pptr['m_PathID']]

    override = next(o.read_typetree() for o in env.objects
                    if o.type.name == 'AnimatorOverrideController' and o.peek_name() == 'HeroMerchant')
    replace = {p['m_OriginalClip']['m_PathID']: p['m_OverrideClip']['m_PathID'] for p in override['m_Clips']}
    ac = objs[override['m_Controller']['m_PathID']].read_typetree()
    tos = {int(k): v for k, v in ac['m_TOS']}
    tos.update({zlib.crc32(v.encode()): v for v in tos.values()})
    c = ac['m_Controller']
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
        curves = clip_curves(t)
        entry = {'name': t['m_Name'], 'length': t['m_MuscleClip']['m_StopTime'] - t['m_MuscleClip']['m_StartTime'],
                 'loop': bool(t['m_MuscleClip']['m_LoopTime']), 'rate': t['m_SampleRate'], 'sprites': [],
                 'events': [[e['time'], e['functionName'], e['data'], e['floatParameter'], e['intParameter']]
                            for e in t['m_Events']], 'curves': []}
        first = 0
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
        nodes = s['m_BlendTreeConstantArray'][0]['data']['m_NodeArray'] if s['m_BlendTreeConstantArray'] else []
        clip_id = nodes[0]['data']['m_ClipID'] if len(nodes) == 1 else -1
        if len(nodes) > 1:
            raise SystemExit(f"state {tos.get(s['m_NameID'])}: blend trees aren't supported")
        states.append({'name': tos.get(s['m_NameID'], str(s['m_NameID'])), 'hash': signed(s['m_NameID']), 'fullPathHash': signed(s['m_FullPathID']),
                       'tag': tos.get(s['m_TagID'], '') if s['m_TagID'] else '', 'tagHash': signed(s['m_TagID']), 'speed': s['m_Speed'],
                       'speedParam': param_index.get(s['m_SpeedParamID'], -1), 'loop': bool(s['m_Loop']),
                       'cycleOffset': s['m_CycleOffset'],
                       'clip': clip(ac['m_AnimationClips'][clip_id]['m_PathID']) if clip_id >= 0 and clip_id != 4294967295 else -1,
                       'transitions': [transition(t['data']) for t in s['m_TransitionConstantArray']]})
    controller = {'controller': ac['m_Name'], 'override': override['m_Name'], 'params': params,
                  'defaultState': sm['m_DefaultState'], 'states': states,
                  'anyState': [transition(t['data']) for t in sm['m_AnyStateTransitionConstantArray']], 'clips': clips}
    (out / 'animators').mkdir(parents=True, exist_ok=True)
    (out / 'animators' / 'hero_merchant.json').write_text(json.dumps(controller), encoding='utf-8')

    # The hero's sprites: what its clips show plus its prefab's SpriteRenderers.
    for o in env.objects:
        if o.type.name == 'SpriteRenderer':
            r = o.read_typetree()
            if r['m_Sprite']['m_FileID'] == 0 and r['m_Sprite']['m_PathID'] in objs:
                sprites_wanted.setdefault(name_of(r['m_Sprite']), r['m_Sprite']['m_PathID'])
    folder = out / 'sprites' / 'hero'
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
    print(f'   hero: {len(states)} states, {len(clips)} clips, {count} sprites, {total // 1024} KB', flush=True)


if __name__ == '__main__':
    main()

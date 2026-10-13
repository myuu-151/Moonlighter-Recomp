"""MasterAudio's dynamic sound groups (DynamicSoundGroupCreator prefabs in resources.assets:
"Sounds - GolemDungeon", "Sounds - Wanderer", ...; and the creators in the first scene, level0,
under its PermanentSoundsContainer: "Sounds - Will" (the general dungeon sounds, Chest's
"dungeon_chest_unlock" among them) and "Sounds - interface", which make their groups at the
game's start, before any dungeon's): for each group, the settings that decide
how many UnityEngine.Random draws MasterAudio.PlaySound makes (MasterAudio.cs
PlaySoundAtVolume / PlaySoundIfAvailable / RefillSoundGroupPool, SoundGroupVariation.Play,
SoundGroupVariationUpdater), and its variations' with their clips' lengths.

The player's files carry no type trees: DynamicSoundGroup's and DynamicGroupVariation's
serialized fields are read in their declaration order (decomp/Assembly-CSharp-firstpass/
DarkTonic.MasterAudio), a bool, int, float or enum as 4 bytes, a string as its length and
bytes (aligned to 4), a PPtr as 12 bytes, a list as its count and items.

Needs UnityPy.

    python tools/extract/sound_groups.py --game <the game's folder> --out <folder>
    -> <folder>/sound_groups.json  {creator: {group: {settings..., variations: [{...}]}}}
"""
import argparse
import json
import struct
from pathlib import Path

import UnityPy

# The fields of the two classes, in their order (P a PPtr, B bool, I int / enum, F float,
# S string, SL list of strings, PL list of PPtrs).
GROUP_FIELDS = (
    ('variationTemplate', 'P'), ('alwaysHighestPriority', 'B'), ('groupMasterVolume', 'F'), ('retriggerPercentage', 'I'),
    ('curVariationSequence', 'I'), ('useNoRepeatRefill', 'B'), ('useInactivePeriodPoolRefill', 'B'), ('inactivePeriodSeconds', 'F'),
    ('curVariationMode', 'I'), ('bulkVariationMode', 'I'), ('chainLoopDelayMin', 'F'), ('chainLoopDelayMax', 'F'),
    ('chainLoopMode', 'I'), ('chainLoopNumLoops', 'I'), ('useDialogFadeOut', 'B'), ('dialogFadeOutTime', 'F'),
    ('resourceClipsAllLoadAsync', 'B'), ('comments', 'S'), ('logSound', 'B'), ('soundPlayedEventActive', 'B'),
    ('soundPlayedCustomEvent', 'S'), ('busIndex', 'I'), ('spatialBlendType', 'I'), ('spatialBlend', 'F'), ('busName', 'S'),
    ('isExistingBus', 'B'), ('isCopiedFromDGSC', 'B'), ('limitMode', 'I'), ('limitPerXFrames', 'I'), ('minimumTimeBetween', 'F'),
    ('limitPolyphony', 'B'), ('voiceLimitCount', 'I'), ('targetDespawnedBehavior', 'I'), ('despawnFadeTime', 'F'),
    ('isUsingOcclusion', 'B'), ('willOcclusionOverrideRaycastOffset', 'B'), ('occlusionRayCastOffset', 'F'),
    ('willOcclusionOverrideFrequencies', 'B'), ('occlusionMaxCutoffFreq', 'F'), ('occlusionMinCutoffFreq', 'F'),
    ('copySettingsExpanded', 'B'), ('expandLinkedGroups', 'B'), ('childSoundGroups', 'SL'), ('endLinkedGroups', 'SL'),
    ('linkedStartGroupSelectionType', 'I'), ('linkedStopGroupSelectionType', 'I'), ('groupVariations', 'PL'),
)
VARIATION_FIELDS = (
    ('probabilityToPlay', 'I'), ('useLocalization', 'B'), ('useRandomPitch', 'B'), ('randomPitchMode', 'I'),
    ('randomPitchMin', 'F'), ('randomPitchMax', 'F'), ('useRandomVolume', 'B'), ('randomVolumeMode', 'I'),
    ('randomVolumeMin', 'F'), ('randomVolumeMax', 'F'), ('weight', 'I'), ('audLocation', 'I'), ('resourceFileName', 'S'),
    ('internetFileUrl', 'S'), ('isExpanded', 'B'), ('isChecked', 'B'), ('useFades', 'B'), ('fadeInTime', 'F'),
    ('fadeOutTime', 'F'), ('useCustomLooping', 'B'), ('minCustomLoops', 'I'), ('maxCustomLoops', 'I'),
    ('useIntroSilence', 'B'), ('introSilenceMin', 'F'), ('introSilenceMax', 'F'), ('useRandomStartTime', 'B'),
    ('randomStartMinPercent', 'F'), ('randomStartMaxPercent', 'F'), ('randomEndPercent', 'F'),
)
# What the port keeps of a group (the rest is presentation).
GROUP_KEEP = ('retriggerPercentage', 'curVariationSequence', 'useNoRepeatRefill', 'curVariationMode', 'soundPlayedEventActive',
              'soundPlayedCustomEvent', 'limitMode', 'limitPerXFrames', 'minimumTimeBetween', 'limitPolyphony',
              'voiceLimitCount', 'childSoundGroups', 'linkedStartGroupSelectionType', 'chainLoopDelayMin', 'chainLoopDelayMax')
VARIATION_KEEP = ('probabilityToPlay', 'useRandomPitch', 'randomPitchMin', 'randomPitchMax', 'useRandomVolume', 'randomVolumeMin',
                  'randomVolumeMax', 'weight', 'audLocation', 'minCustomLoops', 'maxCustomLoops', 'useIntroSilence',
                  'introSilenceMin', 'introSilenceMax', 'useRandomStartTime', 'randomStartMinPercent', 'randomStartMaxPercent')


class Reader:
    def __init__(self, raw):
        self.raw, self.at = raw, 0

    def word(self, fmt):
        v = struct.unpack_from(fmt, self.raw, self.at)[0]
        self.at += 4
        return v

    def string(self):
        n = self.word('<i')
        s = self.raw[self.at:self.at + n].decode('utf-8', 'replace')
        self.at += (n + 3) & ~3
        return s

    def pptr(self):
        file_id = self.word('<i')
        path_id = struct.unpack_from('<q', self.raw, self.at)[0]
        self.at += 8
        return file_id, path_id

    def fields(self, spec):
        out = {}
        for name, kind in spec:
            if kind == 'P':
                out[name] = self.pptr()
            elif kind == 'B':
                out[name] = self.word('<I') & 0xFF != 0
            elif kind == 'I':
                out[name] = self.word('<i')
            elif kind == 'F':
                out[name] = self.word('<f')
            elif kind == 'S':
                out[name] = self.string()
            elif kind == 'SL':
                out[name] = [self.string() for _ in range(self.word('<i'))]
            elif kind == 'PL':
                out[name] = [self.pptr() for _ in range(self.word('<i'))]
        return out


def behaviour(raw, spec):
    r = Reader(raw)
    r.pptr()                 # m_GameObject
    r.word('<I')             # m_Enabled (aligned)
    script = r.pptr()        # m_Script
    r.string()               # m_Name
    try:
        fields = r.fields(spec)
    except struct.error:
        return script, None
    return script, (fields if r.at == len(raw) else None)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    data = Path(args.game) / 'Moonlighter_Data'
    env = UnityPy.load(str(data / 'resources.assets'))
    objs = {o.path_id: o for o in env.objects}
    out = {}
    read_creators(env, objs, out, roots_only=True)
    # The first scene's creators (not roots: under PermanentSoundsContainer).
    scene = UnityPy.load(str(data / 'level0'))
    read_creators(scene, {o.path_id: o for o in scene.objects}, out, roots_only=False)
    Path(args.out).mkdir(parents=True, exist_ok=True)
    (Path(args.out) / 'sound_groups.json').write_text(json.dumps(out, indent=1), encoding='utf-8')


def read_creators(env, objs, out, roots_only):
    def components(go):
        return [objs.get(c.m_PathID if hasattr(c, 'm_PathID') else c.component.m_PathID) for c in go.m_Component]

    def transform_of(go):
        return next(c for c in components(go) if c is not None and c.type.name == 'Transform').read()

    def clip_length(go):
        for c in components(go):
            if c is not None and c.type.name == 'AudioSource':
                src = c.read()
                try:
                    return float(src.m_audioClip.read().m_Length)
                except Exception:
                    return None
        return None

    for o in env.objects:
        if o.type.name != 'GameObject':
            continue
        go = o.read()
        if not go.m_Name.startswith('Sounds - '):
            continue
        tr = transform_of(go)
        if roots_only and tr.m_Father.m_PathID:
            continue  # only roots
        if not tr.m_Children:
            continue  # (an empty one: level0's MusicAndSounds/Sounds - Interface)
        creator = {}
        # The groups anywhere below the creator (some sit under organizing objects), in
        # hierarchy order.
        stack = list(tr.m_Children)
        while stack:
            gtr = stack.pop(0).read()
            ggo = gtr.m_GameObject.read()
            group = None
            for c in components(ggo):
                if c is not None and c.type.name == 'MonoBehaviour':
                    _, fields = behaviour(c.get_raw_data(), GROUP_FIELDS)
                    if fields is not None:
                        group = fields
            if group is None:
                stack[0:0] = list(gtr.m_Children)  # depth first, as GetComponentsInChildren
                continue
            g = {k: group[k] for k in GROUP_KEEP}
            g['variations'] = []
            for vchild in gtr.m_Children:
                vgo = vchild.read().m_GameObject.read()
                for c in components(vgo):
                    if c is not None and c.type.name == 'MonoBehaviour':
                        _, fields = behaviour(c.get_raw_data(), VARIATION_FIELDS)
                        if fields is not None:
                            v = {k: fields[k] for k in VARIATION_KEEP}
                            v['name'] = vgo.m_Name
                            v['clipLength'] = clip_length(vgo)
                            g['variations'].append(v)
            creator[ggo.m_Name] = g
        out[go.m_Name] = creator
        print(f'   {go.m_Name}: {len(creator)} groups', flush=True)


if __name__ == '__main__':
    main()

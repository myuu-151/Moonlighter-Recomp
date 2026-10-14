"""The game's audio: every AudioClip decoded to PCM, and who plays it.

The clips (AudioClip objects in the scenes, resources.assets and sharedassets: FSB5 banks,
nearly all Vorbis, in the .resource files) are decoded with FMOD (UnityPy's AudioClip export,
fmod_toolkit) to 16-bit PCM WAV files, at their own rate and channels. Who plays them:
  - MasterAudio's sound groups: the DynamicSoundGroupCreators (resources.assets' prefabs, which
    DungeonGenerator.GenerateSounds loads from Resources/Sounds/Dungeon for a dungeon, and the
    first scene's, made at the game's start) with each group's settings and each variation's
    (its AudioSource: clip, volume, pitch, loop);
  - MasterAudio's playlists (the first scene's MasterAudio: musicPlaylists), with the
    PlaylistControllers that play them;
  - any other AudioSource (a scene's or a prefab's own: the main menu's door, say).
The level files and resources.assets keep no type trees for MonoBehaviours: they're made from
the game's own Managed assemblies (UnityPy's TypeTreeGenerator: the TypeTreeGeneratorAPI
package).

Needs UnityPy (with fmod_toolkit, which it installs) and TypeTreeGeneratorAPI.

    python tools/extract/audio.py --game <the game's folder> --out <folder> [--no-pcm]
    -> <folder>/audio/audio.json
         {"clips": {key: {"name", "file", "pathId", "format" (Vorbis, ADPCM, PCM...),
                          "loadType" (DecompressOnLoad, CompressedInMemory, Streaming),
                          "rate", "channels", "length" (seconds), "bytes" (compressed),
                          "frames" (decoded sample frames; null with --no-pcm),
                          "pcm" ("pcm/<key>.wav"), "uses": [use, ...]}},
          "creators": {name (sound_groups.json's: the "Sounds - ..." object; a second of a
                              name "<file>:<its path>"):
                              {"file", "path", "scene" (a scene's name, or null), "resources"
                               (its Resources.Load path, or null), "groups": {name: group}}},
          "playlists": {name: {"songTransitionType", "crossfadeMode", "crossFadeTime",
                               "fadeInFirstSong", "fadeOutLastSong",
                               "songs": [{"name", "clip", "volume", "pitch", "loop",
                                          "songStartTimeMode", "customStartTime",
                                          "customStartTimeMax"}]}},
          "controllers": {name: {"startPlaylistName", "playlistVolume", "isShuffle",
                                 "isAutoAdvance", "loopPlaylist", "syncGroupNum",
                                 "mixerGroup" (its mixerChannel)}},
          "playlistManager": {its PlaylistController fields: the controller's object's name},
          "master": {"masterVolume", "playlistMasterVolume", "crossFadeTime", "buses",
                     "enableMusicDucking", "musicDucking"},
          "mixer": {"name", "file", "groups": [{"name", "parent" (index, -1 the master),
                                                "volume" (dB, the start snapshot)}],
                    "exposed": {CRC32 of a parameter's name: the group whose volume it sets}},
          "options": GlobalAudioManager's ranges (min/maxMusicVolume, min/maxSoundsVolume,
                     min/maxUIVolume, muteVolume: dB)}
       group: "creator" (the path of the DynamicSoundGroupCreator that makes it), "mixerGroup"
              (its bus's AudioMixerGroup: the mixer's group index, or null), the
              DynamicSoundGroup's settings (busName, groupMasterVolume, spatialBlendType,
              spatialBlend, the variation mode and sequence, retrigger, limits, polyphony, linked
              groups, chain loops, ...) and "variations": [{"name", "clip" (a key of "clips"),
              "volume", "pitch", "loop", "mixerGroup" (the AudioSource's), the DynamicGroupVariation's
              settings (weight, random pitch and volume, custom loops, intro silence, random
              start, fades, probability)}]
       use:   {"kind": "variation", "creator", "group", "variation"}
              {"kind": "playlist", "playlist", "song"}
              {"kind": "source", "file", "object"} (another AudioSource's)
       Keys are the clips' names; a name several clips share gets "~<pathId>" on each.
       <folder>/audio/pcm/<key>.wav  (16-bit PCM; a clip whose WAV is there from an earlier run
                                      with the same compressed size isn't decoded again)
"""
import argparse
import io
import json
import re
import sys
import wave
from pathlib import Path

import UnityPy
from UnityPy.helpers.TypeTreeGenerator import TypeTreeGenerator

FORMATS = {0: 'PCM', 1: 'Vorbis', 2: 'ADPCM', 3: 'MP3', 4: 'VAG', 5: 'HEVAG', 6: 'XMA', 7: 'AAC', 8: 'GCADPCM', 9: 'ATRAC9'}
LOAD_TYPES = {0: 'DecompressOnLoad', 1: 'CompressedInMemory', 2: 'Streaming'}
# A group's settings kept (the rest is the editor's).
GROUP_KEEP = ('busName', 'busIndex', 'groupMasterVolume', 'spatialBlendType', 'spatialBlend', 'retriggerPercentage',
              'curVariationSequence', 'useNoRepeatRefill', 'useInactivePeriodPoolRefill', 'inactivePeriodSeconds',
              'curVariationMode', 'chainLoopDelayMin', 'chainLoopDelayMax', 'chainLoopMode', 'chainLoopNumLoops',
              'useDialogFadeOut', 'dialogFadeOutTime', 'soundPlayedEventActive', 'soundPlayedCustomEvent', 'limitMode',
              'limitPerXFrames', 'minimumTimeBetween', 'limitPolyphony', 'voiceLimitCount', 'alwaysHighestPriority',
              'targetDespawnedBehavior', 'despawnFadeTime', 'isUsingOcclusion', 'childSoundGroups', 'endLinkedGroups',
              'linkedStartGroupSelectionType', 'linkedStopGroupSelectionType')
VARIATION_KEEP = ('probabilityToPlay', 'weight', 'useRandomPitch', 'randomPitchMode', 'randomPitchMin', 'randomPitchMax',
                  'useRandomVolume', 'randomVolumeMode', 'randomVolumeMin', 'randomVolumeMax', 'audLocation',
                  'resourceFileName', 'useFades', 'fadeInTime', 'fadeOutTime', 'useCustomLooping', 'minCustomLoops',
                  'maxCustomLoops', 'useIntroSilence', 'introSilenceMin', 'introSilenceMax', 'useRandomStartTime',
                  'randomStartMinPercent', 'randomStartMaxPercent', 'randomEndPercent')
CONTROLLER_KEEP = ('startPlaylistName', 'isShuffle', 'isAutoAdvance', 'loopPlaylist', 'syncGroupNum')
PLAYLIST_KEEP = ('songTransitionType', 'crossfadeMode', 'crossFadeTime', 'fadeInFirstSong', 'fadeOutLastSong')


def plain(value):
    """A type tree's value as JSON (0/1 bools stay numbers: the game's ints and bools look alike)."""
    if isinstance(value, float):
        return value
    if isinstance(value, list):
        return [plain(v) for v in value]
    if isinstance(value, dict):
        return {k: plain(v) for k, v in value.items()}
    return value


def safe(name):
    return re.sub(r'[^A-Za-z0-9_.+-]', '_', name)


class Files:
    """The game's serialized files, loaded once each with the type tree generator set."""

    def __init__(self, game):
        self.game = Path(game)
        self.data = self.game / 'Moonlighter_Data'
        self.envs = {}
        self.generator = None

    def load(self, name):
        if name not in self.envs:
            env = UnityPy.load(str(self.data / name))
            if self.generator is None:
                sf = next(iter(env.files.values()))
                self.generator = TypeTreeGenerator(str(sf.unity_version))
                self.generator.load_local_game(str(self.game))
            env.typetree_generator = self.generator
            self.envs[name] = env
        return self.envs[name]

    def names(self):
        """The serialized files with objects: the scenes, then resources.assets and sharedassets."""
        out = sorted((p.name for p in self.data.iterdir() if re.fullmatch(r'level\d+', p.name)), key=lambda n: int(n[5:]))
        out.append('resources.assets')
        out += sorted((p.name for p in self.data.iterdir() if re.fullmatch(r'sharedassets\d+\.assets', p.name)),
                      key=lambda n: int(n[12:-7]))
        return out

    def serialized(self, name):
        return next(iter(self.load(name).files.values()))

    def external(self, name, file_id):
        """The file name a PPtr's m_FileID names, from `name`."""
        if file_id == 0:
            return name
        ext = self.serialized(name).externals[file_id - 1]
        return ext.path.replace('\\', '/').split('/')[-1]


class Scan:
    """One file's objects: GameObjects, their components and children."""

    def __init__(self, files, name):
        self.files = files
        self.name = name
        self.sf = files.serialized(name)
        self.objects = self.sf.objects
        self.script_names = {}
        self.trees = {}

    def tree(self, path_id):
        if path_id not in self.trees:
            self.trees[path_id] = self.objects[path_id].read_typetree()
        return self.trees[path_id]

    def script(self, obj):
        head = obj.parse_monobehaviour_head()
        key = (head.m_Script.m_FileID, head.m_Script.m_PathID)
        if key not in self.script_names:
            try:
                self.script_names[key] = head.m_Script.deref_parse_as_object().m_ClassName
            except Exception:  # noqa: BLE001 - a missing script
                self.script_names[key] = None
        return self.script_names[key]

    def components(self, go_id):
        go = self.tree(go_id)
        out = []
        for c in go['m_Component']:
            ref = c['component'] if 'component' in c else c
            obj = self.objects.get(ref['m_PathID']) if ref['m_FileID'] == 0 else None
            if obj is not None:
                out.append(obj)
        return out

    def behaviour(self, go_id, cls):
        for obj in self.components(go_id):
            if obj.type.name == 'MonoBehaviour' and self.script(obj) == cls:
                return obj
        return None

    def native(self, go_id, type_name):
        for obj in self.components(go_id):
            if obj.type.name == type_name:
                return obj
        return None

    def transform(self, go_id):
        return self.native(go_id, 'Transform') or self.native(go_id, 'RectTransform')

    def children(self, go_id):
        """The child GameObjects' path ids, in order."""
        tr = self.tree(self.transform(go_id).path_id)
        out = []
        for child in tr['m_Children']:
            if child['m_FileID'] == 0 and child['m_PathID'] in self.objects:
                out.append(self.tree(child['m_PathID'])['m_GameObject']['m_PathID'])
        return out

    def parent(self, go_id):
        tr = self.tree(self.transform(go_id).path_id)
        father = tr['m_Father']
        if father['m_FileID'] != 0 or father['m_PathID'] == 0:
            return None
        return self.tree(father['m_PathID'])['m_GameObject']['m_PathID']

    def path(self, go_id):
        names = []
        while go_id is not None:
            names.append(self.tree(go_id)['m_Name'])
            go_id = self.parent(go_id)
        return '/'.join(reversed(names))

    def clip_key(self, ref, keys):
        if ref['m_PathID'] == 0:
            return None
        return keys.get((self.files.external(self.name, ref['m_FileID']), ref['m_PathID']))


def scan_clips(files):
    clips = []
    for name in files.names():
        for obj in files.serialized(name).objects.values():
            if obj.type.name != 'AudioClip':
                continue
            c = obj.read()
            clips.append({'name': c.m_Name, 'file': name, 'pathId': obj.path_id,
                          'format': FORMATS.get(int(c.m_CompressionFormat), str(c.m_CompressionFormat)),
                          'loadType': LOAD_TYPES.get(int(c.m_LoadType), str(c.m_LoadType)),
                          'rate': c.m_Frequency, 'channels': c.m_Channels, 'length': float(c.m_Length),
                          'bytes': c.m_Resource.m_Size, 'frames': None, 'uses': [], '_obj': obj})
    counts = {}
    for c in clips:
        counts[c['name']] = counts.get(c['name'], 0) + 1
    out = {}
    for c in clips:
        key = c['name'] if counts[c['name']] == 1 else f"{c['name']}~{c['pathId']}"
        c['pcm'] = f'pcm/{safe(key)}.wav'
        out[key] = c
    return out


# The AudioMixer's groups by their objects ((file, path id) -> index in the mixer's groups), and
# the buses the creators make (name -> its mixer group index or None: the first to make it, as
# MasterAudio.CreateBus refuses a second; scenes first, as the game loads them).
MIXER_GROUPS = {}
BUSES = {}
CREATOR_HARD_CODED_BUSES = 3  # DynamicSoundGroupCreator.HardCodedBusOptions


def mixer_group(scan, ref):
    """The mixer group index a PPtr to an AudioMixerGroup names, or None."""
    if not ref or ref['m_PathID'] == 0:
        return None
    return MIXER_GROUPS.get((scan.files.external(scan.name, ref['m_FileID']), ref['m_PathID']))


def creator_buses(scan, go):
    """A DynamicSoundGroupCreator's buses: [(name, mixer group index or None, isExisting)]."""
    b = scan.behaviour(go, 'DynamicSoundGroupCreator')
    if b is None:
        return []
    return [(bus['busName'], mixer_group(scan, bus.get('mixerChannel')), bool(bus.get('isExisting')))
            for bus in scan.tree(b.path_id).get('groupBuses', [])]


def scan_mixer(files):
    """The AudioMixer (the game has one, MIXER): its groups (name, parent, volume in dB in its
    start snapshot) and exposed parameters (by the CRC32 of their names: the group whose volume
    each sets)."""
    for name in files.names():
        sf = files.serialized(name)
        guid_name = {}
        for obj in sf.objects.values():
            if obj.type.name == 'AudioMixerGroupController':
                t = obj.read_typetree()
                g = t['m_GroupID']
                guid_name[(g['data[0]'], g['data[1]'], g['data[2]'], g['data[3]'])] = (t['m_Name'], obj.path_id)
        for obj in sf.objects.values():
            if obj.type.name != 'AudioMixerController':
                continue
            t = obj.read_typetree()
            c = t['m_MixerConstant']
            start = [s['m_PathID'] for s in t['m_Snapshots']].index(t['m_StartSnapshot']['m_PathID'])
            values = c['snapshots'][start]['values']
            groups = []
            for i, (g, guid) in enumerate(zip(c['groups'], c['groupGUIDs'])):
                key = (guid['data[0]'], guid['data[1]'], guid['data[2]'], guid['data[3]'])
                gname, path_id = guid_name.get(key, (None, 0))
                groups.append({'name': gname, 'parent': g['parentConstantIndex'], 'volume': values[g['volumeIndex']]})
                MIXER_GROUPS[(name, path_id)] = i
            volume_of = {g['volumeIndex']: i for i, g in enumerate(c['groups'])}
            exposed = {str(h): volume_of.get(index) for h, index in zip(c['exposedParameterNames'], c['exposedParameterIndices'])}
            return {'name': t['m_Name'], 'file': name, 'groups': groups, 'exposed': exposed}
    return None


def scan_buses(files):
    """The buses the creators make (not those they name as existing), the first of a name."""
    for name in files.names():
        scan = Scan(files, name)
        for obj in scan.objects.values():
            if obj.type.name == 'MonoBehaviour' and scan.script(obj) == 'DynamicSoundGroupCreator':
                go = scan.tree(obj.path_id)['m_GameObject']['m_PathID']
                for bus, group, existing in creator_buses(scan, go):
                    if not existing:
                        BUSES.setdefault(bus, group)


def scan_sound_groups(files, scan, keys, resources_paths, scene_names, clips, creators):
    """The file's sound group creators, as sound_groups.json names them: a "Sounds - ..." object
    (the prefab DungeonGenerator.GenerateSounds loads, a scene's container) with the
    DynamicSoundGroupCreators on it and below it (each makes the groups below it); its groups
    (anywhere below it, depth first, as GetComponentsInChildren finds them, each with the
    creator that makes it) and their variations."""
    containers = []
    for obj in scan.objects.values():
        if obj.type.name != 'MonoBehaviour' or scan.script(obj) != 'DynamicSoundGroupCreator':
            continue
        go = scan.tree(obj.path_id)['m_GameObject']['m_PathID']
        # The outermost "Sounds - ..." object it's in (or itself).
        top, at = go, go
        while at is not None:
            if scan.tree(at)['m_Name'].startswith('Sounds - '):
                top = at
            at = scan.parent(at)
        if top not in containers:
            containers.append(top)
    for root in containers:
        name = scan.tree(root)['m_Name']
        path = scan.path(root)
        if name in creators:
            name = f'{scan.name}:{path}'  # (a copy: the tutorial's prefab of the town's)
        groups = {}
        stack = scan.children(root)
        makers = {}
        while stack:
            go = stack.pop(0)
            gb = scan.behaviour(go, 'DynamicSoundGroup')
            if gb is None:
                stack[0:0] = scan.children(go)
                continue
            # The creator that makes it: the nearest one above it.
            at = scan.parent(go)
            while at is not None and scan.behaviour(at, 'DynamicSoundGroupCreator') is None:
                at = scan.parent(at)
            if at not in makers:
                makers[at] = scan.path(at) if at is not None else None
            gname = scan.tree(go)['m_Name']
            gt = scan.tree(gb.path_id)
            group = {'creator': makers[at]}
            group.update({k: plain(gt[k]) for k in GROUP_KEEP if k in gt})
            # Its bus (DynamicSoundGroupCreator.CreateGroups: its creator's groupBuses[busIndex -
            # HardCodedBusOptions]) and the bus's AudioMixerGroup, which MasterAudio routes the
            # variations' AudioSources to: the first creator to make a bus of that name sets it.
            buses = creator_buses(scan, at) if at is not None else []
            index = gt.get('busIndex', -1) - CREATOR_HARD_CODED_BUSES
            bus = buses[index] if 0 <= index < len(buses) else None
            group['busName'] = bus[0] if bus else ''
            group['mixerGroup'] = (BUSES[bus[0]] if bus[0] in BUSES else bus[1]) if bus else None
            group['variations'] = []
            for vgo in scan.children(go):
                vb = scan.behaviour(vgo, 'DynamicGroupVariation')
                if vb is None:
                    continue
                vt = scan.tree(vb.path_id)
                vname = scan.tree(vgo)['m_Name']
                v = {'name': vname, 'clip': None, 'volume': 1.0, 'pitch': 1.0, 'loop': False}
                src = scan.native(vgo, 'AudioSource')
                if src is not None:
                    st = scan.tree(src.path_id)
                    v['clip'] = scan.clip_key(st['m_audioClip'], keys)
                    v['volume'] = st['m_Volume']
                    v['pitch'] = st['m_Pitch']
                    v['loop'] = bool(st['Loop'])
                    v['mixerGroup'] = mixer_group(scan, st.get('OutputAudioMixerGroup'))
                v.update({k: plain(vt[k]) for k in VARIATION_KEEP if k in vt})
                group['variations'].append(v)
                if v['clip'] is not None:
                    clips[v['clip']]['uses'].append({'kind': 'variation', 'creator': name, 'group': gname, 'variation': vname})
            if gname not in groups:  # (the first of a name: as MasterAudio makes them)
                groups[gname] = group
        if not groups:
            continue
        creators[name] = {'file': scan.name, 'path': path, 'scene': scene_names.get(scan.name),
                          'resources': resources_paths.get((scan.name, root)), 'groups': groups}
        print(f'   {name}: {len(groups)} groups, {sum(len(g["variations"]) for g in groups.values())} variations', flush=True)


def scan_master_audio(scan, keys, clips, out):
    for obj in scan.objects.values():
        if obj.type.name != 'MonoBehaviour':
            continue
        cls = scan.script(obj)
        if cls == 'MasterAudio':
            t = scan.tree(obj.path_id)
            out['master'] = {'masterVolume': t['_masterAudioVolume'], 'playlistMasterVolume': t['_masterPlaylistVolume'],
                             'crossFadeTime': t['crossFadeTime'],
                             'buses': [{'name': b['busName'], 'volume': b['volume'], 'voiceLimit': b.get('voiceLimit'),
                                        'stopOldest': b.get('stopOldestSound')} for b in t['groupBuses']],
                             'enableMusicDucking': bool(t['enableMusicDucking']),
                             'musicDucking': [{'soundType': d['soundType'], 'riseVolStart': d['riseVolStart'],
                                               'duckedVolumeCut': d.get('duckedVolumeCut'), 'unduckTime': d.get('unduckTime')}
                                              for d in t['musicDuckingSounds']]}
            for p in t['musicPlaylists']:
                songs = []
                for m in p['MusicSettings']:
                    key = scan.clip_key(m['clip'], keys)
                    songs.append({'name': m['songName'], 'clip': key, 'volume': m['volume'], 'pitch': m['pitch'],
                                  'loop': bool(m['isLoop']), 'songStartTimeMode': m['songStartTimeMode'],
                                  'customStartTime': m['customStartTime'], 'customStartTimeMax': m['customStartTimeMax']})
                    if key is not None:
                        clips[key]['uses'].append({'kind': 'playlist', 'playlist': p['playlistName'], 'song': m['songName']})
                out['playlists'][p['playlistName']] = dict({k: plain(p[k]) for k in PLAYLIST_KEEP}, songs=songs)
        elif cls == 'PlaylistController':
            t = scan.tree(obj.path_id)
            name = scan.tree(t['m_GameObject']['m_PathID'])['m_Name']
            out['controllers'][name] = dict({k: plain(t[k]) for k in CONTROLLER_KEEP}, playlistVolume=t['_playlistVolume'],
                                            mixerGroup=mixer_group(scan, t.get('mixerChannel')))
        elif cls == 'PlaylistManager':
            # Its controllers (golemDungeonPlaylistController...): their objects' names.
            t = scan.tree(obj.path_id)
            for field, ref in t.items():
                if field.endswith('PlaylistController') and isinstance(ref, dict) and ref.get('m_PathID'):
                    target = scan.objects.get(ref['m_PathID']) if ref['m_FileID'] == 0 else None
                    if target is not None:
                        go = scan.tree(target.path_id)['m_GameObject']['m_PathID']
                        out['playlistManager'][field] = scan.tree(go)['m_Name']
        elif cls == 'GlobalAudioManager':
            # The options' volumes: an exposed mixer parameter goes from min to max (dB) as the
            # option goes from 0 to 1 (GlobalAudioManager.SetVolume).
            t = scan.tree(obj.path_id)
            out['options'] = {k: t[k] for k in t if k.endswith('Volume') and isinstance(t[k], float)}


def scan_sources(scan, keys, clips, variation_sources):
    """AudioSources that aren't a variation's."""
    for obj in scan.objects.values():
        if obj.type.name != 'AudioSource' or (scan.name, obj.path_id) in variation_sources:
            continue
        t = scan.tree(obj.path_id)
        key = scan.clip_key(t['m_audioClip'], keys)
        if key is None:
            continue
        go = t['m_GameObject']['m_PathID']
        if scan.behaviour(go, 'DynamicGroupVariation') is not None or scan.behaviour(go, 'SoundGroupVariation') is not None:
            continue
        if scan.behaviour(go, 'PlaylistController') is not None:
            continue  # (its AudioSource plays the playlists' songs)
        clips[key]['uses'].append({'kind': 'source', 'file': scan.name, 'object': scan.path(go)})


def decode(files, clips, out_dir, previous):
    """Each clip to pcm/<key>.wav (16-bit), unless an earlier run left it there."""
    pcm = out_dir / 'pcm'
    pcm.mkdir(parents=True, exist_ok=True)
    total = len(clips)
    for i, (key, c) in enumerate(clips.items()):
        target = out_dir / c['pcm']
        old = previous.get(key)
        if old and old.get('bytes') == c['bytes'] and old.get('frames') and target.exists():
            c['frames'] = old['frames']
            continue
        samples = c['_obj'].read().samples
        if len(samples) != 1:
            raise SystemExit(f"{key}: {len(samples)} sounds in its bank, not 1")
        data = next(iter(samples.values()))
        with wave.open(io.BytesIO(data)) as w:
            if w.getsampwidth() != 2:
                raise SystemExit(f'{key}: decoded to {8 * w.getsampwidth()}-bit samples, not 16')
            if w.getframerate() != c['rate'] or w.getnchannels() != c['channels']:
                raise SystemExit(f"{key}: decoded at {w.getframerate()} Hz x {w.getnchannels()}, the clip says "
                                 f"{c['rate']} x {c['channels']}")
            c['frames'] = w.getnframes()
        target.write_bytes(data)
        if (i + 1) % 50 == 0 or i + 1 == total:
            print(f'   decoded {i + 1} of {total}', flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', required=True)
    parser.add_argument('--out', required=True)
    parser.add_argument('--no-pcm', action='store_true', help='the index only: no clip decoded')
    args = parser.parse_args()
    files = Files(args.game)
    out_dir = Path(args.out) / 'audio'
    out_dir.mkdir(parents=True, exist_ok=True)

    clips = scan_clips(files)
    keys = {(c['file'], c['pathId']): key for key, c in clips.items()}
    print(f"   {len(clips)} clips, {sum(c['bytes'] for c in clips.values()) / 1e6:.1f} MB, "
          f"{sum(c['length'] for c in clips.values()) / 60:.1f} minutes", flush=True)

    # The scenes' names (BuildSettings: level<n> is its n-th scene) and the Resources paths.
    scene_names, resources_paths = {}, {}
    gg = UnityPy.load(str(files.data / 'globalgamemanagers'))
    for obj in gg.objects:
        if obj.type.name == 'BuildSettings':
            for i, s in enumerate(obj.read_typetree()['scenes']):
                scene_names[f'level{i}'] = s.split('/')[-1].rsplit('.', 1)[0]
        elif obj.type.name == 'ResourceManager':
            externals = next(iter(gg.files.values())).externals
            for path, ref in obj.read_typetree()['m_Container']:
                if ref['m_FileID'] > 0:
                    name = externals[ref['m_FileID'] - 1].path.replace('\\', '/').split('/')[-1]
                    resources_paths[(name, ref['m_PathID'])] = path

    mixer = scan_mixer(files)
    scan_buses(files)
    result = {'clips': clips, 'creators': {}, 'playlists': {}, 'controllers': {}, 'playlistManager': {}, 'options': {},
              'master': None, 'mixer': mixer}
    variation_sources = set()
    for name in files.names():
        scan = Scan(files, name)
        scan_sound_groups(files, scan, keys, resources_paths, scene_names, clips, result['creators'])
        scan_master_audio(scan, keys, clips, result)
        for obj in scan.objects.values():
            if obj.type.name == 'MonoBehaviour' and scan.script(obj) == 'DynamicGroupVariation':
                go = scan.tree(obj.path_id)['m_GameObject']['m_PathID']
                src = scan.native(go, 'AudioSource')
                if src is not None:
                    variation_sources.add((name, src.path_id))
        scan_sources(scan, keys, clips, variation_sources)
    if result['master'] is None:
        raise SystemExit('no MasterAudio in the scenes')

    previous = {}
    index = out_dir / 'audio.json'
    if index.exists():
        try:
            previous = json.loads(index.read_text(encoding='utf-8'))['clips']
        except (OSError, ValueError, KeyError):
            previous = {}
    if not args.no_pcm:
        decode(files, clips, out_dir, previous)
    for c in clips.values():
        del c['_obj']
    index.write_text(json.dumps(result, indent=1), encoding='utf-8')
    used = sum(1 for c in clips.values() if c['uses'])
    print(f"   {len(result['creators'])} sound group creators, {len(result['playlists'])} playlists; "
          f"{used} of {len(clips)} clips played by them or an AudioSource", flush=True)


if __name__ == '__main__':
    sys.exit(main())

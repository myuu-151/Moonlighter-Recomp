"""The prefabs in the dungeon and enemy bundles, flattened: for every root object, each
object below it (itself included) as the scene holds it after Instantiate: its path below
the root, whether it's active, its local position and scale, and its SpriteRenderer
(sprite, sorting layer, order, flips, colour, enabled). The same rows the oracle's floor
snapshot has, so what the port builds from these can be checked against the game.

Also the chests PrefabRegister holds (sharedassets0.assets: WoodChest ... MagicChest and
the other dungeon chests), the weapons of the items bundle (the prefabs below
CONTAINER_ONLY's folder: ItemDatabase.GetPrefab loads them by name), and the sorting layers'
names in their drawing order (the TagManager's).

Needs UnityPy.

    python tools/extract/prefabs.py --game <the game's folder> --out <folder>
    -> <folder>/prefabs.json  {"sortingLayers": [...], "bundles": {bundle: {prefab: [rows]}}}
       row: [path, active, x, y, scale x, scale y, sprite, layer, order, flipX, flipY, rgba, enabled,
            rotation z (degrees), behaviours: [[class, {fields}], ...],
            [game object layer, tag, colliders: [[type, enabled, trigger, offset x, offset y, ...]]],
            rigidbody: {bodyType (0 dynamic, 1 kinematic, 2 static), simulated, mass, linearDrag,
            angularDrag, gravityScale, constraints (bits: 1 freeze x, 2 y, 4 rotation),
            collisionDetection (0 discrete, 1 continuous), interpolation (0 none, 1 interpolate,
            2 extrapolate), sleepingMode (0 never, 1 start awake, 2 start asleep)} or null,
            [local rotation quaternion z, w]]
       (fields only for the classes in FIELDS; a LayerMask as its bits, a reference as whether
       it's set, a list as its length, but for the STRUCTURED fields; a collider ends with
       size x, size y for a box, the radius for a circle, the paths for a polygon)
"""
import argparse
import json
import math
from pathlib import Path

import UnityPy

BUNDLE_PREFIXES = ('dungeon_', 'enemies_', 'hero_merchant')
# PrefabRegister's prefabs the dungeon spawns by name (SpawnChest), from the player's files.
PLAYER_FILE = 'sharedassets0.assets'
# Bundles of which only the prefabs below a folder are wanted (by their asset paths).
CONTAINER_ONLY = {'items': 'assets/prefabs/items/equipment/weapons/'}
# MonoBehaviours whose Start() draws from Random when a room comes to life, and the fields
# that decide how many draws and what they pick; and the settings of what the port runs
# (the hero, the enemies).
ENEMY_BEHAVIOUR = ('canMove', 'fallInHoles', 'attackPushBackwardsForce', 'soundWhenHit')
FIELDS = {
    'BreakableDetail': ('destroyAtStartProb',),
    'GolemKingRockBehaviour': ('RandomRockSprites',),
    'BreakableObjectBehaviour': ('skins', 'currentSkin'),
    'BreakableBoxBehaviour': ('skins', 'currentSkin'),  # a BreakableObjectBehaviour (inherits its Start)
    'CarnivorousMutaeBossRoots': (),
    # Not a draw: sets its renderer's sortingOrder from the world y in Start (what the scene draws in front).
    'SpriteDepthOrderRender': ('offset', 'forceOrderValue', 'forcedOrderValue', 'over', 'overOffset'),
    # The hero's movement settings (layer masks as their bits).
    'HeroMerchantController': ('collisionDetectionLayers', 'slideCollisionDetectionLayers', 'godModeCollisionDetectionLayers',
                               'holesMask', 'items', 'speedFactor', 'itemAttrackRadius'),
    # The enemies: what they drop (and where), how they move (PolyNav), what their attacks hit.
    'Enemy': ('itemDrops', 'dropRadius', 'dropOffset'),
    'EnemyBehaviour': ENEMY_BEHAVIOUR,
    'BabySlimeBehaviour': ENEMY_BEHAVIOUR,  # an EnemyBehaviour
    'GolemSoldierBehaviour': ENEMY_BEHAVIOUR + ('iDirectionLocked',),  # an EnemyBehaviour
    'TangleBehaviour': ENEMY_BEHAVIOUR + ('tangleCulture',),  # an EnemyBehaviour (tangleCulture: CommonEnemyCulture)
    # an EnemyBehaviour (attackParticles: whether it has its ParticleSystem)
    'HermitSlimeBehaviourNEW': ENEMY_BEHAVIOUR + ('iDirectionLocked', 'attackParticles'),
    # A child hurtbox (the HermitSlime's "Collidere"): the behaviour it passes the weapon's hits to.
    'EnemyDamageReceiver': ('gameObjectToAdvise',),
    'PolyNavAgent': ('maxSpeed', 'mass', 'stoppingDistance', 'slowingDistance', 'accelerationRate', 'decelerationRate',
                     'avoidRadius', 'lookAheadDistance', 'repath', 'restrict', 'closerPointOnInvalid', 'roundCorners',
                     'rotateTransform'),
    # shapeType 0 polygon, 1 box; m_Enabled: the behaviour's enabled flag (only enabled ones join the map)
    'PolyNavObstacle': ('invertPolygon', 'extraOffset', 'shapeType', 'm_Enabled'),
    'EnemyAttackCollisionDetection': ('dealDamageToBreakables', 'gameObjectToAdvise'),
    # The Broken turret (an EnemyBehaviour) and its shot: the projectile prefab (its name), the
    # Invoke's delay, where shots start (EnemyBehaviour.spawnProyectilePositions), the look it
    # starts with (HeroMerchantController.Direction); the crash's wait, the art the shot turns.
    'BrokenGolemTurretBehaviour': ENEMY_BEHAVIOUR + ('projectilePrefab', 'shootDelay', 'spawnProyectilePositions', 'enemyDirection'),
    'BrokenGolemTurretProjectile': ('timeToDestroyAfterCrash', 'projectileArt'),
    # The FlyingKamikazeGolem (an EnemyBehaviour): the look it starts with
    # (HeroMerchantController.Direction), the attack object it switches on and off (its path),
    # its trails (how many: particles, presentation).
    'FlyingKamikazeGolemBehaviour': ENEMY_BEHAVIOUR + ('enemyDirection', 'Attack', 'Trails'),
    # MasterAudio's EventSounds (the FlyingKamikazeGolem's "SoundsFlyingKamikaze" child): its
    # OnEnable plays enableSound's sounds (PlaySound: Random draws) when useEnableSound is on.
    'EventSounds': ('m_Enabled', 'useEnableSound', 'enableSound'),
    # A weapon's attack colliders: which attack they hit with (0 main, 1 secondary).
    'WeaponCollisionDetection': ('weaponAttackType',),
    # The GolemHead (an EnemyBehaviour): its roll's settings (the prefab's runOverForce 50, not the
    # class's 9000), the collider it switches (its object's path: the AttackCollider's box), its
    # break particles (their object's path; presentation), its PolyNav2D (none: FindObjectOfType),
    # and EnemyBehaviour.effectOnAttack (whether set: its is the ConfusedEffect prefab's).
    'GolemHeadBehaviour': ENEMY_BEHAVIOUR + ('runOverRepeatTime', 'runOverForce', 'runOverCollider', 'breakParticles',
                                             'polyNavInstance', 'effectOnAttack'),
    # A MoonlighterEffect an enemy's attack puts on the hero (the GolemHead's effectOnAttack):
    # target (0 None), unique (0 None, 1 ResetOther, 2 EraseThis), its sound, how long.
    'ConfusedEffect': ('target', 'soundName', '_yOffset', 'unique', 'effectName', 'confusedTime'),
}


def item_drops(value, go_path, sprite):
    return [{'item': d['item'], 'dropProbability': d['dropProbability'], 'min': d['minDropRate'], 'max': d['maxDropRate']}
            for d in value]


def object_path(value, go_path, sprite):
    """A GameObject (or component) reference as its object's path below the prefab's root
    ('' the root), null if unset or outside the prefab."""
    return go_path.get(value['m_PathID']) if value['m_FileID'] == 0 else None


def object_name(value, go_path, sprite):
    """A reference to another object of the bundle (a prefab's root) as its name, null if unset."""
    return sprite(value)


def vector2_list(value, go_path, sprite):
    return [[v['x'], v['y']] for v in value]


def play_sound_types(value, go_path, sprite):
    """An EventSounds AudioEventGroup: the sound types of its PlaySound events
    (EventSoundFunctionType.PlaySound, 0), in order."""
    return [e['soundType'] for e in value.get('SoundEvents', []) if e.get('currentSoundFunctionType', 0) == 0]


def breakable_skins(value, go_path, sprite):
    """BreakableObjectBehaviour.skins: each skin's whole and broken sprites."""
    return [{'full': sprite(k['fullHealthSprite']), 'broken': sprite(k['breakSprite'])} for k in value]


# Fields kept whole rather than as field() has them: (class, field) -> f(value, {game object: path},
# sprite name of a reference).
STRUCTURED = {
    ('BreakableObjectBehaviour', 'skins'): breakable_skins,
    ('BreakableBoxBehaviour', 'skins'): breakable_skins,
    ('Enemy', 'itemDrops'): item_drops,
    ('EnemyAttackCollisionDetection', 'gameObjectToAdvise'): object_path,
    ('EnemyDamageReceiver', 'gameObjectToAdvise'): object_path,  # (a component: its object's path)
    ('BrokenGolemTurretBehaviour', 'projectilePrefab'): object_name,
    ('BrokenGolemTurretBehaviour', 'spawnProyectilePositions'): vector2_list,
    ('BrokenGolemTurretProjectile', 'projectileArt'): object_path,
    ('FlyingKamikazeGolemBehaviour', 'Attack'): object_path,
    ('EventSounds', 'enableSound'): play_sound_types,
    ('GolemHeadBehaviour', 'runOverCollider'): object_path,  # (a component: its object's path)
    ('GolemHeadBehaviour', 'breakParticles'): object_path,
}
PLAYER_PREFABS = ('WoodChest', 'IronChest', 'GoldChest', 'MagicChest', 'BossChest', 'DungeonToShopChest', 'TimedChestSlime')


def tag_manager(game):
    env = UnityPy.load(str(Path(game) / 'Moonlighter_Data' / 'globalgamemanagers'))
    for obj in env.objects:
        if obj.type.name == 'TagManager':
            return obj.read_typetree()
    return {}


def sorting_layers(manager):
    return [(layer['name'], layer['uniqueID']) for layer in manager.get('m_SortingLayers', [])]


# GameObject.m_Tag: Unity's built-in tags by number, the project's from 20000 on.
BUILTIN_TAGS = {0: 'Untagged', 1: 'Respawn', 2: 'Finish', 3: 'EditorOnly', 5: 'MainCamera', 6: 'Player', 7: 'GameController'}


def tag_name(manager, tag):
    if tag >= 20000:
        tags = manager.get('tags', [])
        return tags[tag - 20000] if tag - 20000 < len(tags) else str(tag)
    return BUILTIN_TAGS.get(tag, str(tag))


def collider_row(kind, c):
    off = c.get('m_Offset', {'x': 0.0, 'y': 0.0})
    row = [kind, bool(c.get('m_Enabled', 1)), bool(c.get('m_IsTrigger', 0)), off['x'], off['y']]
    if kind == 'BoxCollider2D':
        row += [c['m_Size']['x'], c['m_Size']['y']]
    elif kind == 'CircleCollider2D':
        row += [c['m_Radius']]
    elif kind == 'PolygonCollider2D':
        row += [[[[p['x'], p['y']] for p in path] for path in c['m_Points']['m_Paths']]]
    return row


def rigidbody_row(rb):
    return {'bodyType': rb['m_BodyType'], 'simulated': bool(rb['m_Simulated']), 'mass': rb['m_Mass'],
            'linearDrag': rb['m_LinearDrag'], 'angularDrag': rb['m_AngularDrag'], 'gravityScale': rb['m_GravityScale'],
            'constraints': rb['m_Constraints'], 'collisionDetection': rb['m_CollisionDetection'],
            'interpolation': rb['m_Interpolate'], 'sleepingMode': rb['m_SleepingMode']}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--game', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    manager = tag_manager(args.game)
    layers = sorting_layers(manager)
    layer_name = {uid & 0xFFFFFFFF: name for name, uid in layers}
    bundles_dir = Path(args.game) / 'Moonlighter_Data' / 'StreamingAssets' / 'AssetBundles'
    out = {'sortingLayers': [name for name, _ in layers], 'bundles': {}}
    total = 0
    files = [b for b in sorted(bundles_dir.iterdir()) if b.name.startswith(BUNDLE_PREFIXES) and b.suffix != '.manifest']
    files.append(Path(args.game) / 'Moonlighter_Data' / PLAYER_FILE)
    files += [bundles_dir / name for name in CONTAINER_ONLY]
    for bundle in files:
        only = PLAYER_PREFABS if bundle.name == PLAYER_FILE else None
        env = UnityPy.load(str(bundle))
        by_id = {obj.path_id: obj for obj in env.objects}
        # The root game objects of the prefabs below the folder (the bundle's asset paths).
        only_roots = None
        if bundle.name in CONTAINER_ONLY:
            manifest = next(obj for obj in env.objects if obj.type.name == 'AssetBundle').read_typetree()
            only_roots = {info['asset']['m_PathID'] for path, info in manifest['m_Container']
                          if path.startswith(CONTAINER_ONLY[bundle.name]) and path.endswith('.prefab')}
        trees = {}

        def tree(path_id):
            if path_id not in trees:
                trees[path_id] = by_id[path_id].read_typetree() if path_id in by_id else None
            return trees[path_id]

        # Prefabs the game references directly win over others of their name (dungeon_common
        # has two DungeonRoom prefabs; the room database's dungeonRoomPrefab is the one used).
        referenced = set()
        for obj in env.objects:
            if obj.type.name == 'MonoBehaviour':
                try:
                    mb = obj.read_typetree()
                except Exception:  # noqa: BLE001 - no type tree
                    continue
                ref = mb.get('dungeonRoomPrefab') if isinstance(mb, dict) else None
                if isinstance(ref, dict) and ref.get('m_FileID') == 0:
                    referenced.add(ref['m_PathID'])

        def components(go):
            for c in go['m_Component']:
                ref = c['component']
                if ref['m_FileID'] == 0 and ref['m_PathID'] in by_id:
                    yield by_id[ref['m_PathID']]

        sprite_names = {}

        def sprite_name(ref):
            if ref['m_PathID'] == 0:
                return None
            if ref['m_FileID'] != 0:
                return '?external'
            if ref['m_PathID'] not in sprite_names:
                obj = by_id.get(ref['m_PathID'])
                sprite_names[ref['m_PathID']] = obj.read().m_Name if obj is not None else None
            return sprite_names[ref['m_PathID']]

        class_names = {}

        def class_name(ref):
            key = (ref['m_FileID'], ref['m_PathID'])
            if key not in class_names:
                obj = by_id.get(ref['m_PathID']) if ref['m_FileID'] == 0 else None
                class_names[key] = obj.read().m_ClassName if obj is not None else None
            return class_names[key]

        def field(value):
            if isinstance(value, dict) and 'm_Bits' in value:  # LayerMask
                return value['m_Bits']
            if isinstance(value, dict) and 'm_PathID' in value:
                return value['m_PathID'] != 0
            if isinstance(value, list):
                return len(value)
            return value

        prefabs = {}
        for obj in env.objects:
            if obj.type.name not in ('Transform', 'RectTransform'):
                continue
            t = tree(obj.path_id)
            if t['m_Father']['m_PathID'] != 0:
                continue  # not a root
            root_name = tree(t['m_GameObject']['m_PathID'])['m_Name']
            if only is not None and root_name not in only:
                continue
            if only_roots is not None and t['m_GameObject']['m_PathID'] not in only_roots:
                continue
            rows = []
            go_path = {}

            def paths(transform_id, path):
                tr = tree(transform_id)
                go_path[tr['m_GameObject']['m_PathID']] = path
                # Its components too (a reference to one names its object: path ids are unique).
                for c in tree(tr['m_GameObject']['m_PathID'])['m_Component']:
                    if c['component']['m_FileID'] == 0:
                        go_path.setdefault(c['component']['m_PathID'], path)
                for child in tr['m_Children']:
                    ctr = tree(child['m_PathID'])
                    if ctr is not None:
                        cname = tree(ctr['m_GameObject']['m_PathID'])['m_Name']
                        paths(child['m_PathID'], cname if path == '' else f'{path}/{cname}')

            paths(obj.path_id, '')

            # The root's own row has path '', the rest paths below it.
            def walk_node(transform_id, path):
                tr = tree(transform_id)
                go = tree(tr['m_GameObject']['m_PathID'])
                pos, scale = tr['m_LocalPosition'], tr['m_LocalScale']
                row = [path, bool(go['m_IsActive']), pos['x'], pos['y'], scale['x'], scale['y'], None, None, 0, False, False, None, False]
                for comp in components(go):
                    if comp.type.name == 'SpriteRenderer':
                        sr = tree(comp.path_id)
                        col = sr.get('m_Color', {'r': 1, 'g': 1, 'b': 1, 'a': 1})
                        # (the layer's ID is signed in the renderer, unsigned in the TagManager)
                        row[6:] = [sprite_name(sr['m_Sprite']), layer_name.get(sr.get('m_SortingLayerID', 0) & 0xFFFFFFFF, 'Default'),
                                   sr.get('m_SortingOrder', 0), bool(sr.get('m_FlipX', 0)), bool(sr.get('m_FlipY', 0)),
                                   [col['r'], col['g'], col['b'], col['a']], bool(sr.get('m_Enabled', 1))]
                q = tr['m_LocalRotation']
                row.append(math.degrees(2.0 * math.atan2(q['z'], q['w'])))  # 2D: the rotation about z
                behaviours = []
                for comp in components(go):
                    if comp.type.name != 'MonoBehaviour':
                        continue
                    try:
                        mb = tree(comp.path_id)
                    except Exception:  # noqa: BLE001 - no type tree: the class name alone
                        mb = None
                    if mb is None:
                        continue
                    cls = class_name(mb['m_Script'])
                    behaviours.append([cls, {f: STRUCTURED[cls, f](mb[f], go_path, sprite_name) if (cls, f) in STRUCTURED and f in mb
                                             else field(mb.get(f)) for f in FIELDS.get(cls, ())}])
                row.append(behaviours)
                colliders = [collider_row(comp.type.name, tree(comp.path_id)) for comp in components(go)
                             if comp.type.name in ('BoxCollider2D', 'CircleCollider2D', 'PolygonCollider2D')]
                row.append([go['m_Layer'], tag_name(manager, go['m_Tag']), colliders])
                bodies = [rigidbody_row(tree(comp.path_id)) for comp in components(go) if comp.type.name == 'Rigidbody2D']
                row.append(bodies[0] if bodies else None)
                # The local rotation's quaternion z and w as stored (Transform.TransformPoint rotates
                # by it; the degrees above don't give its exact bits back).
                row.append([q['z'], q['w']])
                rows.append(row)
                for child in tr['m_Children']:
                    ctr = tree(child['m_PathID'])
                    if ctr is None:
                        continue
                    cname = tree(ctr['m_GameObject']['m_PathID'])['m_Name']
                    walk_node(child['m_PathID'], cname if path == '' else f'{path}/{cname}')

            walk_node(obj.path_id, '')
            if t['m_GameObject']['m_PathID'] in referenced:
                prefabs[root_name] = rows
            else:
                prefabs.setdefault(root_name, rows)  # first of a name wins (the game's lookups take the first)
        out['bundles'][bundle.name] = prefabs
        total += len(prefabs)
        print(f'   {bundle.name}: {len(prefabs)} prefabs', flush=True)
    target = Path(args.out) / 'prefabs.json'
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(out), encoding='utf-8')
    print(f'   {total} prefabs, {len(layers)} sorting layers', flush=True)


if __name__ == '__main__':
    main()

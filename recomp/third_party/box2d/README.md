# Box2D 2.3.1

Unity 2019.2's 2D physics is a fork of Box2D. This is stock Box2D 2.3.1, which
`unity::PhysicsWorld` (`recomp/src/unity/PhysicsWorld.*`) wraps as Unity's Physics2D world.

- Source: https://github.com/erincatto/box2d, tag `v2.3.1`, commit
  `7e633c4fb86a68bf072fb8ae67ea2c060114750e` (2014-04-06, "Version 2.3.1 Bug fixes plus new GUI.").
- Taken from `Box2D/Box2D/`: `Box2D.h`, `Common/`, `Collision/`, `Dynamics/` (with
  `Dynamics/Joints/`, which `b2World`, `b2Island` and `b2Body` need to compile). `Rope/`, the
  testbed, the build files and the GUI libraries are left out.
- Licence: zlib (`License.txt`, from the same commit).

## Changes (marked "ML port" in the source)

- `Common/b2Settings.h`: `b2_maxTranslation` is 100.0 instead of 2.0, and can be defined from
  outside: Unity's `Physics2DSettings.maxTranslationSpeed` (100 in Moonlighter's project) taken
  as Box2D's own per-step limit. UnityPlayer.dll's `b2_maxTranslation` global holds 100.0 in its
  initialized data (see "Reading Unity's fork" below); whether the player sets it from the
  settings at run time, and how, isn't read yet.
- `Dynamics/b2World.h`, `b2World.cpp`: `SetContinuousBulletsOnly` (default off, as stock).
  On, `SolveTOI` skips contacts between two non-bullet bodies: a Discrete Rigidbody2D (a
  non-bullet dynamic body) isn't swept against static or kinematic colliders either, only
  Continuous ones (bullets: `unity::PhysicsWorld` makes a Continuous Rigidbody2D a bullet) are;
  Unity's Discrete mode only generates contacts at the new position. Stock Box2D gives every
  dynamic body a time of impact against static and kinematic ones. `PhysicsWorld` turns it on
  (`PhysicsWorld::Options::continuousBulletsOnly`). The recordings that show it:
  `tangle.jsonl` (frame 205: Tangle 5 pushed into a hole's inside corner, against a static
  collider, ends where the island's position iterations left it, y 342.989410; stock Box2D's TOI
  pushes it on to the slop, 342.985016) and `enemy_FlyingKamikazeGolem.jsonl` (frame 119: the
  Discrete golem flies 0.18 into a Kinematic wall in one step and is pushed out over the next
  ones).

- `Dynamics/b2ContactManager.h`, `b2ContactManager.cpp`, `b2World.h`: `SetKinematicTriggers`
  (default off, as stock). On, two fixtures whose bodies `b2Body::ShouldCollide` turns away
  (neither dynamic) still pair when one of them is a sensor and one of the bodies is kinematic
  (`b2ContactManager::BodiesPair`, in `AddPair` and in `Collide`'s refiltering): Unity reports a
  trigger on or against a Kinematic Rigidbody2D with static colliders and Kinematic bodies.
  `PhysicsWorld` turns it on (`PhysicsWorld::Options::kinematicTriggers`). The recording that
  shows it: `enemy_KamikazeGolem.jsonl` of recq kamifix-2 (frames 191-194: the KamikazeGolem's
  growing explosion box, a trigger on its AttackCollider's own Kinematic body, enters the room's
  LowerBoundary, DoorBoundaryRight and a puddle's Collider_3).

- `Dynamics/b2Body.h`: `SetLinearVelocity` with a non-zero velocity wakes the body and sets its
  time at rest back to zero whether it slept or not (stock calls `SetAwake(true)`, which zeroes
  that time only for a sleeping body). Unity's `b2Body::SetLinearVelocity` and
  `Rigidbody2D::SetVelocity` do it so. The recording that shows it:
  `enemy_FlyingKamikazeGolem.jsonl` of recq enemyfix-flykami-2 (frame 189: the golem, its
  velocity tweened down to zero by its return's DOTween, which writes `Rigidbody2D.velocity`
  each frame, is awake; stock Box2D has it asleep, its time at rest counted through those
  writes).

Unity's own code around Box2D that `unity::PhysicsWorld` reproduces rather than Box2D: a
collider on a turned Rigidbody2D of its own (the DungeonRoom prefab's door boundaries, turned
by a half or a quarter turn) gets the body angle `Rigidbody2D::Create` gives it (twice
`atan2f(z, w)` of the transform's quaternion: (float)pi for a half turn, whose sine isn't 0) and
its corners through the matrix `Collider2D::CalculateColliderTransformation` makes (the inverse
of that turn times the transform's matrix), so its faces lean by a few 1e-8 as Unity's do
(`PhysicsWorld.cpp`, `TurnedBox`; `enemy_FlyingKamikazeGolem.jsonl` of recq enemyfix-flykami-2,
frames 111-188: the golem pressed into DoorBoundaryDown's top face keeps velocities of 1e-6
down to 1e-13 that only that lean gives).

Every other tuning constant already matches the project's Physics2DSettings
(`data/extracted/physics2d.json`): linear slop 0.005 (polygon radius 0.01 = the default contact
offset), Baumgarte 0.2, TOI Baumgarte 0.75, max linear correction 0.2, max angular correction 8
degrees, velocity threshold 1, time to sleep 0.5, linear sleep tolerance 0.01, angular sleep
tolerance 2 degrees.

## Reading Unity's fork

Moonlighter's UnityPlayer.dll (Unity 2019.2, x64, `UnityPlayer_Win64_mono_x64.pdb` GUID
`3F233A7CAA6B491580A681E2105B6966`, age 1) has public symbols on Unity's symbol server
(`https://symbolserver.unity3d.com/UnityPlayer_Win64_mono_x64.pdb/3F233A7CAA6B491580A681E2105B69661/UnityPlayer_Win64_mono_x64.pd_`,
a cabinet: `expand` it). `tools/unity_dll/pdb_publics.py` lists them (RVA and name) and
`tools/unity_dll/disasm.py` disassembles a function by RVA (Python 3 with `pefile` and
`capstone`). Its Box2D is a 2.3.x with Unity's tasks around the island solve
(`b2IslandDiscreteSolver`: `b2SolveDiscreteIslandVelocitiesTask`,
`b2InitializeContactSolverTask`, `b2InitializeVelocityConstraintsTask`,
`b2WarmStartContactSolverTask`, `b2SolveVelocityConstraintsTask`,
`b2SolveDiscreteIslandIntegratePosesTask`, `b2SolvePositionConstraintsTask`,
`b2SolveDiscreteIslandBodySleepTask`). Read against this source, the velocity integration and
damping, the contact constraints' masses, velocity bias, block solver and its condition number,
the friction and normal impulses, the warm start and the pose integration with its maximum
translation do the same float operations in the same order; they add per-axis factors for
`RigidbodyConstraints2D.FreezePositionX/Y` (1 for a free axis, so nothing changes there). The
position solver uses the stock constants (slop 0.005, `b2_baumgarte`, `b2_maxLinearCorrection`,
the -3 slop it stops at); it isn't read through.

## Building

Built into `mlcore` (`recomp/CMakeLists.txt`) with the game code's float flags: no FMA
contraction (MSVC `/fp:precise`; GCC `-ffp-contract=off`, as for the GameCube build). The code
is plain C++ without exceptions or platform calls other than `b2Timer` (which has a stub for
targets that are neither Windows, Linux nor Apple).

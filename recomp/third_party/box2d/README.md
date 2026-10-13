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

- `Common/b2Settings.h`: `b2_maxTranslation` is 1.0 instead of 2.0, and can be defined from
  outside. Unity's `Physics2DSettings.maxTranslationSpeed` is 100 in Moonlighter's project;
  this takes it as per second over the 0.01 s fixed step. Whether Unity's fork applies it per
  second or per step is not known.
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

Every other tuning constant already matches the project's Physics2DSettings
(`data/extracted/physics2d.json`): linear slop 0.005 (polygon radius 0.01 = the default contact
offset), Baumgarte 0.2, TOI Baumgarte 0.75, max linear correction 0.2, max angular correction 8
degrees, velocity threshold 1, time to sleep 0.5, linear sleep tolerance 0.01, angular sleep
tolerance 2 degrees.

## Building

Built into `mlcore` (`recomp/CMakeLists.txt`) with the game code's float flags: no FMA
contraction (MSVC `/fp:precise`; GCC `-ffp-contract=off`, as for the GameCube build). The code
is plain C++ without exceptions or platform calls other than `b2Timer` (which has a stub for
targets that are neither Windows, Linux nor Apple).

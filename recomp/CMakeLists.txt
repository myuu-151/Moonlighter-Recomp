cmake_minimum_required(VERSION 3.20)
project(MoonlighterRecomp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Game logic must round floats exactly like the original (no fused multiply-add).
# MSVC doesn't contract under the default /fp:precise; GCC/Clang need it switched off.
if(MSVC)
    add_compile_options(/W4 /fp:precise /utf-8 /wd4100 /wd4458)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS) # getenv in the tests
else()
    add_compile_options(-Wall -Wextra -ffp-contract=off)
endif()

# Box2D 2.3.1 (third_party/box2d/README.md): Unity's 2D physics, for the bodies' contacts.
file(GLOB_RECURSE BOX2D_SOURCES CONFIGURE_DEPENDS third_party/box2d/Box2D/*.cpp)
if(MSVC)
    set_source_files_properties(${BOX2D_SOURCES} PROPERTIES COMPILE_OPTIONS /wd4456) # its shadowed locals
endif()

# libtess2 (third_party/libtess2/README.md): how Unity cuts a PolygonCollider2D into Box2D's
# convex polygons (unity/PolygonCollider2D.h). Plain C.
enable_language(C)
file(GLOB LIBTESS2_SOURCES CONFIGURE_DEPENDS third_party/libtess2/Source/*.c)
if(MSVC)
    set_source_files_properties(${LIBTESS2_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0")
else()
    set_source_files_properties(${LIBTESS2_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
endif()

add_library(mlcore STATIC
    ${BOX2D_SOURCES}
    ${LIBTESS2_SOURCES}
    src/cs/Object.cpp
    src/cs/String.cpp
    src/cs/Text.cpp
    src/unity/Random.cpp
    src/util/Json.cpp
    src/game/Constants.cpp
    src/game/Cave.cpp
    src/game/DungeonLayout.cpp
    src/game/RoomDatabase.cpp
    src/game/RoomSelection.cpp
    src/game/RoomFill.cpp
    src/game/RoomGenerator.cpp
    src/game/Chest.cpp
    src/game/ChestGeneration.cpp
    src/game/RoomDetails.cpp
    src/game/SpawnDraws.cpp
    src/game/RoomPlacement.cpp
    src/game/RoomScene.cpp
    src/game/PrefabCatalog.cpp
    src/game/Prefabs.cpp
    src/game/BossRoomStart.cpp
    src/game/Session.cpp
    src/game/Composite.cpp
    src/render/SpriteBank.cpp
    src/render/DrawList.cpp
    src/render/RoomDraw.cpp
    src/game/DungeonFloor.cpp
    src/unity/Animator.cpp
    src/unity/Physics2D.cpp
    src/unity/Tween.cpp
    src/game/HeroMerchantController.cpp
    src/game/HeroMerchantStats.cpp
    src/game/Weapon.cpp
    src/game/SceneTransform.cpp
    src/game/RoomPhysics.cpp
    src/game/HoleColliders.cpp
    src/game/Items.cpp
    src/game/RoomDoors.cpp
    src/unity/PhysicsWorld.cpp
    src/game/PolyNav.cpp
    src/game/PolyNavAgent.cpp
    src/game/HoleOutlines.cpp
    src/game/RoomPolyNav.cpp
    src/unity/DelayedCalls.cpp
    src/game/EnemyStats.cpp
    src/game/Enemy.cpp
    src/game/BabySlime.cpp
    src/game/GolemSoldier.cpp
    src/game/MasterAudio.cpp
    src/game/EnemyFactory.cpp
    src/game/Tangle.cpp
    # The HermitSlime, child hurtboxes and a root box the clips drive, the hero's damage types.
    src/game/HermitSlime.cpp
    src/game/EnemyColliders.cpp
    src/game/HeroDamage.cpp
    src/game/PoolManager.cpp
    src/game/EnemyProjectile.cpp
    src/game/BrokenGolemTurret.cpp
    # The FlyingKamikazeGolem, and a root box a script switches on and off.
    src/game/FlyingKamikazeGolem.cpp
    src/game/EnemyRootCollider.cpp
    # MasterAudio's EventSounds on an enemy's objects: OnEnable's sounds and their draws.
    src/game/EnemyEventSounds.cpp
    # An Animator's root motion on an enemy's body (the BrokenGolemTurret's: it never sleeps).
    src/game/EnemyRootMotion.cpp
    # DungeonRoom.DisableEnemies (ExitRoom: the hero leaving, or falling into a secret room).
    src/game/EnemyRoom.cpp
    # The hero's amulet effects on the enemies' hits (the Wanderer Amulet's draw).
    src/game/AmuletEffects.cpp
    # The KamikazeGolem, Physics2D.OverlapCircleAll over the physics world.
    src/game/KamikazeGolem.cpp
    src/unity/Physics2DOverlap.cpp
    # EnemyBehaviour.MakeJump (shared with the chest mimics).
    src/game/EnemyJump.cpp
    # A PolygonCollider2D's convex pieces in the physics world (libtess2).
    src/unity/PolygonCollider2D.cpp
)
target_include_directories(mlcore PUBLIC src third_party/box2d third_party/libtess2/Include)

add_executable(cave_oracle_test tests/cave_oracle_test.cpp)
target_link_libraries(cave_oracle_test PRIVATE mlcore)

enable_testing()
add_test(NAME cave_oracle
         COMMAND cave_oracle_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(room_oracle_test tests/room_oracle_test.cpp)
target_link_libraries(room_oracle_test PRIVATE mlcore)
add_test(NAME room_oracle
         COMMAND room_oracle_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle ${CMAKE_CURRENT_SOURCE_DIR}/../data/extracted/rooms.json
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(fill_oracle_test tests/fill_oracle_test.cpp)
target_link_libraries(fill_oracle_test PRIVATE mlcore)
add_test(NAME fill_oracle
         COMMAND fill_oracle_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle ${CMAKE_CURRENT_SOURCE_DIR}/../data/extracted/rooms.json
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(room_sync_test tests/room_sync_test.cpp)
target_link_libraries(room_sync_test PRIVATE mlcore)
add_test(NAME room_sync
         COMMAND room_sync_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle ${CMAKE_CURRENT_SOURCE_DIR}/../data/extracted/rooms.json ${CMAKE_CURRENT_SOURCE_DIR}/../data/extracted/generation_data_counts.json
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(floor_sync_test tests/floor_sync_test.cpp)
target_link_libraries(floor_sync_test PRIVATE mlcore)
add_test(NAME floor_sync
         COMMAND floor_sync_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle ${CMAKE_CURRENT_SOURCE_DIR}/../data/extracted/generation_data_counts.json
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(room_doors_test tests/room_doors_test.cpp)
target_link_libraries(room_doors_test PRIVATE mlcore)
add_test(NAME room_doors COMMAND room_doors_test)

add_executable(physics_test tests/physics_test.cpp)
target_link_libraries(physics_test PRIVATE mlcore)
add_test(NAME physics COMMAND physics_test WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(animator_curves_test tests/animator_curves_test.cpp)
target_link_libraries(animator_curves_test PRIVATE mlcore)
add_test(NAME animator_curves COMMAND animator_curves_test WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(player_walk_test tests/player_walk_test.cpp)
target_link_libraries(player_walk_test PRIVATE mlcore)
add_test(NAME player_walk
         COMMAND player_walk_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle/walk.jsonl
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_test(NAME player_roll
         COMMAND player_walk_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle/walk_roll.jsonl
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_test(NAME player_diagonals
         COMMAND player_walk_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle/walk_diag.jsonl
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)
# Falling into a hole and back at the door (the oracle's -oracle-walk=fall).
add_test(NAME player_fall
         COMMAND player_walk_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle/walk_fall.jsonl
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)
# The main attack: the recording's save slot equips Forest Spirit Gloves (recordings since name it).
add_test(NAME player_attack
         COMMAND player_walk_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle/walk_attack.jsonl
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_tests_properties(player_attack PROPERTIES ENVIRONMENT "ML_WALK_WEAPON=Forest Spirit Gloves")
# Walking and rolling into a rock (the oracle's -oracle-walk=rocks): his Rigidbody2D in Box2D,
# pushed out of its corners, and his body after each frame's physics steps.
add_test(NAME player_rocks
         COMMAND player_walk_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle/walk_rocks.jsonl
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_tests_properties(player_rocks PROPERTIES ENVIRONMENT "ML_WALK_BOX2D=1")

add_executable(polynav_test tests/polynav_test.cpp)
target_link_libraries(polynav_test PRIVATE mlcore)
add_test(NAME polynav
         COMMAND polynav_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle/polynav.jsonl
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)

# The BabySlime against the game (data/oracle/slime.jsonl, `-oracle-slime`; skipped if there's
# none), and its self-checks (Random, the delayed calls, the stats, a hit and a death).
add_executable(slime_test tests/slime_test.cpp)
target_link_libraries(slime_test PRIVATE mlcore)
add_test(NAME slime
         COMMAND slime_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle/slime.jsonl
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)
# A second room (Golem_Enemies_15_Bis): the targeting's Invoke falling due in a fixed step.
add_test(NAME slime_bis
         COMMAND slime_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle/slime_bis.jsonl
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)

# The Golem enemies past the BabySlime against the game (data/oracle/enemy_<prefab>.jsonl,
# `-oracle-enemy=<prefab>`; skipped if there's none), and their self-checks (the screen shake's
# draws, a GolemSoldier in a box: its chase, its swing's events and AttackCollider).
add_executable(enemy_test tests/enemy_test.cpp)
target_link_libraries(enemy_test PRIVATE mlcore)
add_test(NAME enemy_golem_soldier
         COMMAND enemy_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle/enemy_GolemSoldier.jsonl GolemSoldier
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)
# The HermitSlime (its patrol, its slam, its trigger root the clips drive, its hurtbox, ItemDrain).
add_test(NAME enemy_hermit_slime
         COMMAND enemy_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle/enemy_HermitSlime.jsonl HermitSlime
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)
# The BrokenGolemTurret and its shots (game/BrokenGolemTurret.h, game/EnemyProjectile.h): turrets in a
# box (the look's raycasts, the shots' pool, flight, crash and return), and against the game
# (`-oracle-enemy=BrokenGolemTurret`; skipped if there's none).
add_test(NAME enemy_broken_turret
         COMMAND enemy_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle/enemy_BrokenGolemTurret.jsonl BrokenGolemTurret
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)

# The FlyingKamikazeGolem (game/FlyingKamikazeGolem.h): golems in a walled box (the charge's draw,
# aim and bug, the root box and attack child switched, the OutQuint return, EnableCharge's
# draw), and against the game (`-oracle-enemy=FlyingKamikazeGolem`; skipped if there's none).
add_test(NAME enemy_flyingkamikazegolem
         COMMAND enemy_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle/enemy_FlyingKamikazeGolem.jsonl FlyingKamikazeGolem
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)

# The KamikazeGolem (game/KamikazeGolem.h): kamikazes in a walled box (the wander, the jump, the
# explosion and its OverlapCircleAll, SelfDestroy), and against the game
# (`-oracle-enemy=KamikazeGolem`; skipped if there's none).
add_test(NAME enemy_kamikazegolem
         COMMAND enemy_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle/enemy_KamikazeGolem.jsonl KamikazeGolem
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)

# The Tangle against the game (data/oracle/tangle.jsonl, `-oracle-tangle`; skipped if there's
# none), and its self-checks (Physics2D.Raycast, the weave).
add_executable(tangle_test tests/tangle_test.cpp)
target_link_libraries(tangle_test PRIVATE mlcore)
add_test(NAME tangle
         COMMAND tangle_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle/tangle.jsonl
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)

# The PC preview of generated floors (PNGs), from the same code the GameCube build draws with.
add_executable(preview tools/preview.cpp)
target_link_libraries(preview PRIVATE mlcore)

# Will walking a generated floor on the PC (what the GameCube build runs), for checking moves.
add_executable(walk_sim tools/walk_sim.cpp)
target_link_libraries(walk_sim PRIVATE mlcore)

# The game's own code translated by tools/translate, and the oracle tests against it
# (docs/translator_plan.md): opt-in, it needs the .NET SDK and the game copy.
option(ML_TRANSLATED "Translate the game's code (tools/translate) and build the oracle tests against it" OFF)
if(ML_TRANSLATED)
    include(translated.cmake)
endif()

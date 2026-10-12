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

add_library(mlcore STATIC
    ${BOX2D_SOURCES}
    src/cs/Object.cpp
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
)
target_include_directories(mlcore PUBLIC src third_party/box2d)

add_executable(cave_oracle_test tests/cave_oracle_test.cpp)
target_link_libraries(cave_oracle_test PRIVATE mlcore)

enable_testing()
add_test(NAME cave_oracle
         COMMAND cave_oracle_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle)

add_executable(room_oracle_test tests/room_oracle_test.cpp)
target_link_libraries(room_oracle_test PRIVATE mlcore)
add_test(NAME room_oracle
         COMMAND room_oracle_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle ${CMAKE_CURRENT_SOURCE_DIR}/../data/extracted/rooms.json)

add_executable(fill_oracle_test tests/fill_oracle_test.cpp)
target_link_libraries(fill_oracle_test PRIVATE mlcore)
add_test(NAME fill_oracle
         COMMAND fill_oracle_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle ${CMAKE_CURRENT_SOURCE_DIR}/../data/extracted/rooms.json)

add_executable(room_sync_test tests/room_sync_test.cpp)
target_link_libraries(room_sync_test PRIVATE mlcore)
add_test(NAME room_sync
         COMMAND room_sync_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle ${CMAKE_CURRENT_SOURCE_DIR}/../data/extracted/rooms.json ${CMAKE_CURRENT_SOURCE_DIR}/../data/extracted/generation_data_counts.json)

add_executable(floor_sync_test tests/floor_sync_test.cpp)
target_link_libraries(floor_sync_test PRIVATE mlcore)
add_test(NAME floor_sync
         COMMAND floor_sync_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle ${CMAKE_CURRENT_SOURCE_DIR}/../data/extracted/generation_data_counts.json)

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

add_executable(polynav_test tests/polynav_test.cpp)
target_link_libraries(polynav_test PRIVATE mlcore)
add_test(NAME polynav
         COMMAND polynav_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle/polynav.jsonl
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)

# The PC preview of generated floors (PNGs), from the same code the GameCube build draws with.
add_executable(preview tools/preview.cpp)
target_link_libraries(preview PRIVATE mlcore)

# Will walking a generated floor on the PC (what the GameCube build runs), for checking moves.
add_executable(walk_sim tools/walk_sim.cpp)
target_link_libraries(walk_sim PRIVATE mlcore)

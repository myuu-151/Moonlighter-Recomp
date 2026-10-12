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

add_library(mlcore STATIC
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
    src/game/DungeonFloor.cpp
)
target_include_directories(mlcore PUBLIC src)

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

# The PC preview of generated floors (PNGs), from the same code the GameCube build draws with.
add_executable(preview tools/preview.cpp)
target_link_libraries(preview PRIVATE mlcore)

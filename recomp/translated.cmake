# The game's own code, translated into C++ by tools/translate (docs/translator_plan.md), and the
# oracle tests built against it instead of the hand port. Included by CMakeLists.txt when
# ML_TRANSLATED is ON.
#
# The translation runs at configure time from the user's copy of the game (never committed):
#   tools/translate decompiles Moonlighter_Data/Managed/Assembly-CSharp.dll in-process, compiles
#   it with Roslyn against the game's other DLLs and writes recomp/gen/cpp (game.h, one .cpp per
#   C# file, sources.cmake, report.tsv). Changing units.txt, bindings.tsv or the tool's sources
#   re-runs it on the next build.

find_program(ML_DOTNET dotnet REQUIRED)
set(ML_TRANSLATE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../tools/translate)
set(ML_GEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/gen/cpp)
set(ML_GAME "" CACHE PATH "The game copy (the folder holding Moonlighter_Data); default: the translator's own search (ML_GAME, then the main checkout's Moonlighter/)")

set(_ml_translate_args translate --out ${ML_GEN_DIR})
if(ML_GAME)
    list(APPEND _ml_translate_args --managed ${ML_GAME}/Moonlighter_Data/Managed)
endif()
message(STATUS "tools/translate: translating the game's code into ${ML_GEN_DIR}")
execute_process(COMMAND ${ML_DOTNET} run --project ${ML_TRANSLATE_DIR} -c Release -- ${_ml_translate_args}
                RESULT_VARIABLE _ml_translate_result OUTPUT_VARIABLE _ml_translate_out ERROR_VARIABLE _ml_translate_err)
if(NOT _ml_translate_result EQUAL 0)
    message(FATAL_ERROR "tools/translate failed:\n${_ml_translate_out}${_ml_translate_err}")
endif()
message(STATUS "${_ml_translate_out}")
file(GLOB _ml_translate_sources CONFIGURE_DEPENDS ${ML_TRANSLATE_DIR}/*.cs)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${ML_TRANSLATE_DIR}/units.txt ${ML_TRANSLATE_DIR}/bindings.tsv ${_ml_translate_sources})

include(${ML_GEN_DIR}/sources.cmake)
add_library(mlgen STATIC ${ML_GEN_SOURCES}
            src/bind/Core.cpp src/bind/Scene.cpp src/bind/Physics.cpp src/bind/Components.cpp src/bind/DOTween.cpp
            src/bind/Engine.cpp src/bind/Plugins.cpp src/bind/Prefabs.cpp src/bind/Serial.cpp)
target_include_directories(mlgen PUBLIC ${ML_GEN_DIR} src)
target_link_libraries(mlgen PUBLIC mlcore)
if(MSVC)
    # generated code: unused parameters and locals of untranslated stubs, unreachable code
    target_compile_options(mlgen PRIVATE /W3 /wd4100 /wd4189 /wd4702 /wd4101 /bigobj)
else()
    target_compile_options(mlgen PRIVATE -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable)
endif()

# ML_TRANSLATED_DEBUG: PDBs for the translated code and its tests, and ML_CS_STACK=1 printing the C++
# stack at each C# exception thrown (finding where translated code dereferenced null).
option(ML_TRANSLATED_DEBUG "PDBs for the translated code; ML_CS_STACK=1 prints the stack at each C# throw" OFF)
if(ML_TRANSLATED_DEBUG AND MSVC)
    set_source_files_properties(src/cs/Object.cpp PROPERTIES COMPILE_DEFINITIONS ML_CS_STACK_DUMP)
    target_compile_options(mlgen PRIVATE /Zi)
endif()
function(ml_translated_debug target)
    if(ML_TRANSLATED_DEBUG AND MSVC)
        target_compile_options(${target} PRIVATE /Zi)
        target_link_options(${target} PRIVATE /DEBUG)
    endif()
endfunction()

# The oracle tests against the translated classes: tests/translated holds headers named like
# the hand port's (game/Cave.h...) that name the translated classes instead, and the glue that
# drives them as the game does.
add_executable(cave_oracle_translated tests/cave_oracle_test.cpp tests/translated/Adapters.cpp)
target_include_directories(cave_oracle_translated BEFORE PRIVATE tests/translated)
target_link_libraries(cave_oracle_translated PRIVATE mlgen)
ml_translated_debug(cave_oracle_translated)
add_test(NAME cave_oracle_translated
         COMMAND cave_oracle_translated ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)

# The Golem enemies against the game with their own code translated (tests/translated/enemy_translated_test:
# enemy_replay.h's replay and rows, slime_test's and enemy_test's recordings) in place of the hand
# port: BabySlimeBehaviour, GolemSoldierBehaviour, EnemyBehaviour, Enemy, EnemyStats, PolyNav...,
# on the scene facade (src/bind) and the stand-ins of tests/translated/scene.
add_executable(enemy_translated_test tests/translated/enemy_translated_test.cpp tests/translated/scene/Standins.cpp)
target_include_directories(enemy_translated_test PRIVATE tests/translated)
target_link_libraries(enemy_translated_test PRIVATE mlgen)
ml_translated_debug(enemy_translated_test)
add_test(NAME slime_translated
         COMMAND enemy_translated_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle/slime.jsonl BabySlime
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_test(NAME slime_bis_translated
         COMMAND enemy_translated_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle/slime_bis.jsonl BabySlime
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)
add_test(NAME enemy_golem_soldier_translated
         COMMAND enemy_translated_test ${CMAKE_CURRENT_SOURCE_DIR}/../data/oracle/enemy_GolemSoldier.jsonl GolemSoldier
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)

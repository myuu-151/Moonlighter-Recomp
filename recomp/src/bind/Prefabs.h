#pragma once
// A prefab of the game's (data/extracted/prefabs.json, game/Prefabs.h) made in the scene the way
// Instantiate makes it: its objects (names, active flags, layers, tags, local transforms), the
// engine's components the extraction has (SpriteRenderer, the Collider2Ds, Rigidbody2D; an
// Animator on the root when a controller is given) and the translated scripts by class name
// (gen::cs_AddComponent) with the serialized fields the extraction has (numbers, strings,
// vectors, colours, references to the prefab's own objects; a field it doesn't have keeps its
// C# initializer), then Awake and OnEnable once the whole tree is there.
//
// The extraction doesn't say the order of an object's components: the renderer, the colliders,
// the body, the Animator, then the scripts in the extraction's order. Scripts no translation has
// (presentation: sorting, the Chinese builds' sprites) are left out and listed.

#include <string>
#include <vector>

#include "bind/UnityEngine.h"
#include "game/Prefabs.h"

namespace bind {

struct PrefabOptions {
    const unity::AnimatorController* controller = nullptr; // an Animator on the root with it
    bool active = true;                                     // activate it (Awake, OnEnable) once made
};

// What the translation adds a script with (generated: game.h's gen::cs_AddComponent).
using AddScript = UnityEngine::Component* (*)(UnityEngine::GameObject*, const char*);

// Makes `prefab` under `parent` (none: at the scene's root); its root is named "<name>(Clone)".
// `skipped`: the scripts left out (class names), if given.
UnityEngine::GameObject* InstantiatePrefab(const game::Prefab& prefab, UnityEngine::Transform* parent, const PrefabOptions& options,
                                           AddScript addScript, std::vector<std::string>* skipped = nullptr);

// The object at `path` below `root` ("" the root itself; "A/B"), nullptr if none.
UnityEngine::GameObject* FindBelow(UnityEngine::GameObject* root, const std::string& path);

} // namespace bind

#pragma once
// The engine and plugin API translated game code calls (tools/translate/bindings.tsv names this
// header): the facade over the hand-written Unity layer (recomp/src/unity).
//   bind/Core.h        values, UnityEngine.Object, Resources, Debug, Time, Mathf
//   bind/Values.h      Vector2/Vector3/Quaternion/LayerMask/Color arithmetic as Unity's managed code has it
//   bind/Scene.h       GameObject, Component, Transform, MonoBehaviour (messages, Invoke, coroutines), the scene
//   bind/Physics.h     Rigidbody2D, the Collider2Ds, Physics2D's queries, the fixed step and its triggers
//   bind/Components.h  Animator, renderers, materials, sprites, particle systems
//   bind/DOTween.h     DOTween
//   bind/Engine.h      Instantiate, FindObjectOfType, Application, Gizmos, layers
//   bind/Plugins.h     MasterAudio, AntiCheat's obscured values
//   bind/Serial.h      serialized values (prefabs, JSON files) into fields; bind/Prefabs.h makes prefabs

#include "bind/Components.h"
#include "bind/Core.h"
#include "bind/DOTween.h"
#include "bind/Engine.h"
#include "bind/Physics.h"
#include "bind/Plugins.h"
#include "bind/Scene.h"
#include "bind/Serial.h"
#include "bind/Values.h"

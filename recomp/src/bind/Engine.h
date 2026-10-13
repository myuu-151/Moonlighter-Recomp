#pragma once
// The rest of the engine API translated scripts call: Object.Instantiate and FindObjectOfType,
// Application, Gizmos and Debug's drawing (editor-only: nothing), LayerMask's names.
//
// Instantiate copies an object the way the engine does for what the game's scripts read: the
// GameObject tree (names, layers, tags, active flags, local transforms) and each component's
// fields (a translated script's whole object, a facade component's settings), then puts it
// under the parent and runs its Awake / OnEnable (a Rigidbody2D's body made after the
// colliders). Unity copies only the serialized fields and remaps references into the copied
// tree; here every field is copied as it is (a script's reference to another object of the
// same prefab still points into the original): the prefabs the translated code instantiates so
// far (an enemy's health bar, a death's particles) don't hold such references.

#include <type_traits>

#include "bind/Components.h"
#include "bind/Physics.h"
#include "bind/Scene.h"

namespace UnityEngine {

struct Application {
    static bool get_isPlaying() { return true; }
    static bool get_isEditor() { return false; }
};

struct Gizmos {
    static void DrawLine(Vector3, Vector3) {}
    static void DrawWireSphere(Vector3, float) {}
    static void DrawSphere(Vector3, float) {}
    static void DrawWireCube(Vector3, Vector3) {}
    static void DrawCube(Vector3, Vector3) {}
    static Color get_color() { return color; }
    static void set_color(Color c) { color = c; }
    static Color color;
};

// The engine side of Instantiate (the templates below cast what it returns).
Object* InstantiateObject(Object* original, Transform* parent, bool worldPositionStays);
Object* InstantiateObjectAt(Object* original, Vector3 position, Quaternion rotation, Transform* parent);

template <typename T>
T Instantiate(T original)
{
    return dynamic_cast<T>(InstantiateObject(original, nullptr, true));
}
template <typename T>
T Instantiate(T original, Transform* parent)
{
    return dynamic_cast<T>(InstantiateObject(original, parent, false));
}
template <typename T>
T Instantiate(T original, Transform* parent, bool instantiateInWorldSpace)
{
    return dynamic_cast<T>(InstantiateObject(original, parent, instantiateInWorldSpace));
}
template <typename T>
T Instantiate(T original, Vector3 position, Quaternion rotation)
{
    return dynamic_cast<T>(InstantiateObjectAt(original, position, rotation, nullptr));
}
template <typename T>
T Instantiate(T original, Vector3 position, Quaternion rotation, Transform* parent)
{
    return dynamic_cast<T>(InstantiateObjectAt(original, position, rotation, parent));
}

// Object.FindObjectOfType<T>(): the first live component of the type on an active object, in
// the order the objects were made.
template <typename T>
T FindObjectOfType()
{
    for (Object* o : Object::Live()) {
        if (o->destroyed_) continue;
        auto t = dynamic_cast<T>(o);
        if (!t) continue;
        auto* c = dynamic_cast<Component*>(o);
        if (c && (!c->gameObject_ || !c->gameObject_->get_activeInHierarchy())) continue;
        return t;
    }
    return nullptr;
}
template <typename T>
cs::Array<T>* FindObjectsOfType()
{
    std::vector<T> out;
    for (Object* o : Object::Live()) {
        if (o->destroyed_) continue;
        auto t = dynamic_cast<T>(o);
        if (!t) continue;
        auto* c = dynamic_cast<Component*>(o);
        if (c && (!c->gameObject_ || !c->gameObject_->get_activeInHierarchy())) continue;
        out.push_back(t);
    }
    return cs::Array<T>::FromVector(out);
}

// The project's layers (its TagManager, as data/extracted/physics2d.json lists them).
const char* LayerName(int32_t layer);

} // namespace UnityEngine

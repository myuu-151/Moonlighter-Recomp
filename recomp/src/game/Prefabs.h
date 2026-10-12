#pragma once
// The game's prefabs as data (data/extracted/prefabs.json, tools/extract/prefabs.py): each
// one's objects in hierarchy order, with their local transforms and SpriteRenderers. What
// Instantiate copies; the scene builder (RoomScene) assembles rooms from them.

#include <map>
#include <string>
#include <vector>

#include "unity/PhysicsWorld.h"
#include "util/Json.h"

namespace game {

// A MonoBehaviour on a prefab object: its class, and the fields extracted for it (numbers;
// a bool or an object reference as 0/1, a list as its length).
struct Behaviour {
    std::string cls;
    std::map<std::string, double> fields;
    double Field(const std::string& name, double fallback = 0.0) const
    {
        auto it = fields.find(name);
        return it == fields.end() ? fallback : it->second;
    }
};

// A Collider2D on a prefab object (local to it): BoxCollider2D (size), CircleCollider2D
// (radius) or PolygonCollider2D (paths).
struct ColliderDef {
    std::string type;
    bool enabled = true, trigger = false;
    float ox = 0.0f, oy = 0.0f;   // offset
    float sx = 0.0f, sy = 0.0f;   // box size
    float radius = 0.0f;
    std::vector<std::vector<std::pair<float, float>>> paths;
};

struct PrefabNode {
    std::string path;  // below the prefab's root ("" for the root itself)
    bool active = true;
    float x = 0.0f, y = 0.0f;   // local position
    float sx = 1.0f, sy = 1.0f; // local scale
    float rotation = 0.0f;      // about z, degrees
    // SpriteRenderer (sprite empty: none, or no renderer when `layer` is empty too).
    std::string sprite, layer;
    int order = 0;
    bool flipX = false, flipY = false, rendererEnabled = false;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    std::vector<Behaviour> behaviours;
    int objectLayer = 0; // the GameObject's (physics) layer
    std::string tag;
    std::vector<ColliderDef> colliders;
    bool hasRigidbody = false;
    unity::Rigidbody2DDef rigidbody;
    bool HasRenderer() const { return !layer.empty(); }
};

struct Prefab {
    std::string name, bundle;
    std::vector<PrefabNode> nodes; // nodes[0] is the root
};

class PrefabLibrary {
public:
    static void Load(const json::Value& extracted);
    static bool Loaded();
    // A prefab by name: from `bundle` if it has one, else the first bundle (in name order)
    // that does. nullptr if none.
    static const Prefab* Find(const std::string& name, const std::string& bundle = "");
    // The sorting layers in drawing order.
    static const std::vector<std::string>& SortingLayers();
};

} // namespace game

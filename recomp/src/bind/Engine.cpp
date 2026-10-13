#include "bind/Engine.h"

#include <algorithm>
#include <cstring>

namespace UnityEngine {

Color Gizmos::color{1.0f, 1.0f, 1.0f, 1.0f};

// ---- layers ------------------------------------------------------------------------------------------

namespace {
// The project's TagManager layers (data/extracted/physics2d.json "layers").
const char* const kLayers[32] = {
    "Default",  "TransparentFX",  "Ignore Raycast",  "",         "Water",         "UI",          "",           "",
    "RisenObstacle", "Flying",    "Item",            "LevelBoundary", "GroundEnemy", "FlyingEnemy", "SunkenObstacle", "EnemyProjectile",
    "PlayerProjectile", "PlayerSpecificShopCollider", "Villager", "WeaponCollider", "Familiar", "EnemyBoundary", "ItemNoCollision", "EnemyAttack",
    "IntangibleHero", "Door",     "Citizen",         "Roomba",   "RoombaCollider", "",          "WandererChest", "Lights",
};
} // namespace

const char* LayerName(int32_t layer) { return layer >= 0 && layer < 32 ? kLayers[layer] : ""; }

int32_t LayerMask::NameToLayer(cs::String* name)
{
    if (!name) return -1;
    const std::string n = name->ToUtf8();
    if (n.empty()) return -1;
    for (int32_t i = 0; i < 32; i++)
        if (n == kLayers[i]) return i;
    return -1;
}

cs::String* LayerMask::LayerToName(int32_t layer) { return cs::String::FromAscii(LayerName(layer)); }

// ---- the facade components' copies -----------------------------------------------------------------

namespace {
template <typename T>
T* Copy(const T* c)
{
    auto* n = cs::gcnew<T>(*c);
    n->gameObject_ = nullptr;
    return n;
}
} // namespace

Object* Collider2D::cs_Clone()
{
    auto* n = Copy(this);
    n->changed_ = true;
    return n;
}
Object* BoxCollider2D::cs_Clone()
{
    auto* n = Copy(this);
    n->changed_ = true;
    return n;
}
Object* CircleCollider2D::cs_Clone()
{
    auto* n = Copy(this);
    n->changed_ = true;
    return n;
}
Object* PolygonCollider2D::cs_Clone()
{
    auto* n = Copy(this);
    n->changed_ = true;
    return n;
}
Object* Rigidbody2D::cs_Clone()
{
    auto* n = Copy(this);
    n->body_ = nullptr;
    return n;
}
Object* Renderer::cs_Clone() { return Copy(this); }
Object* SpriteRenderer::cs_Clone() { return Copy(this); }
Object* MeshRenderer::cs_Clone() { return Copy(this); }
Object* ParticleSystem::cs_Clone() { return Copy(this); }
Object* Animator::cs_Clone()
{
    auto* n = cs::gcnew<Animator>();
    n->name_ = name_;
    n->enabled_ = enabled_;
    n->speed_ = speed_;
    n->runtime_ = runtime_;
    n->SetController(controller_);
    return n;
}

// ---- Instantiate ---------------------------------------------------------------------------------------

namespace {

void Reset(Component* c)
{
    c->gameObject_ = nullptr;
    c->destroyed_ = false;
    if (auto* m = dynamic_cast<MonoBehaviour*>(c)) {
        m->awoken_ = false;
        m->started_ = false;
    }
}

// The tree below `src`, not yet attached: (object, its components in order) per object.
using Made = std::vector<std::pair<GameObject*, std::vector<Component*>>>;
using Objects = std::vector<std::pair<GameObject*, GameObject*>>; // (original, copy)
using Pairs = std::vector<std::pair<Component*, Component*>>; // (original, copy)

GameObject* CloneTree(GameObject* src, Transform* parent, Made& made, Pairs& pairs, Objects& objects)
{
    auto* go = cs::gcnew<GameObject>();
    go->name_ = src->name_;
    go->layer_ = src->layer_;
    go->tag_ = src->tag_;
    go->activeSelf_ = src->activeSelf_;
    auto* t = cs::gcnew<Transform>();
    t->gameObject_ = go;
    t->localPosition_ = src->transform_->localPosition_;
    t->localScale_ = src->transform_->localScale_;
    t->rotationZ_ = src->transform_->rotationZ_;
    go->transform_ = t;
    go->components_.push_back(t);
    if (parent) {
        t->parent_ = parent;
        parent->children_.push_back(t);
    }
    std::vector<Component*> comps;
    for (Component* c : src->components_) {
        if (c == src->transform_ || c->destroyed_) continue;
        auto* n = dynamic_cast<Component*>(c->cs_Clone());
        if (!n) cs::NotTranslated("Instantiate: a component's copy isn't a component");
        Reset(n);
        comps.push_back(n);
        pairs.emplace_back(c, n);
    }
    made.emplace_back(go, comps);
    objects.emplace_back(src, go);
    pairs.emplace_back(src->transform_, t);
    for (Transform* k : src->transform_->children_) CloneTree(k->gameObject_, t, made, pairs, objects);
    return go;
}

GameObject* InstantiateTree(GameObject* src, Transform* parent, bool worldPositionStays, const Vector3* position, Pairs& pairs)
{
    Made made;
    Objects objects;
    const bool wasActive = src->activeSelf_;
    GameObject* root = CloneTree(src, nullptr, made, pairs, objects);
    // The copies' references into the tree, to the copies (Unity's Instantiate: a script's
    // reference to an object of its prefab is its copy's).
    Remap remap;
    for (const auto& [o, n] : objects) remap.map.emplace_back(o, n);
    for (const auto& [o, n] : pairs) remap.map.emplace_back(o, n);
    for (const auto& [o, n] : pairs) n->cs_Remap(remap);
    root->name_ = cs::String::Concat(src->get_name(), CS_LIT(u"(Clone)"));
    root->activeSelf_ = false; // (Awake and OnEnable once it's all there)
    bind::Scene* s = bind::Scene::current;
    if (s) s->roots.push_back(root);
    if (position) root->transform_->localPosition_ = *position;
    if (parent) {
        const Vector3 world = root->transform_->get_position();
        root->transform_->SetParent(parent, false);
        if (worldPositionStays || position) root->transform_->set_position(position ? *position : world);
    }
    // The components onto their objects: the colliders before a body that needs them.
    for (auto& [go, comps] : made) {
        std::stable_sort(comps.begin(), comps.end(), [](Component* a, Component* b) {
            return dynamic_cast<Rigidbody2D*>(a) == nullptr && dynamic_cast<Rigidbody2D*>(b) != nullptr;
        });
        for (Component* c : comps) go->Attach(c);
    }
    if (wasActive) root->SetActive(true);
    return root;
}

} // namespace

Object* InstantiateObject(Object* original, Transform* parent, bool worldPositionStays)
{
    if (!Object::op_Implicit(original)) cs::Throw(cs::gcnew<cs::ArgumentException>(CS_LIT(u"The Object you want to instantiate is null.")));
    Pairs pairs;
    if (auto* g = dynamic_cast<GameObject*>(original)) return InstantiateTree(g, parent, worldPositionStays, nullptr, pairs);
    if (auto* c = dynamic_cast<Component*>(original)) {
        GameObject* root = InstantiateTree(cs::nc(c->gameObject_), parent, worldPositionStays, nullptr, pairs);
        if (c == c->gameObject_->transform_) return root->transform_;
        for (const auto& [o, n] : pairs)
            if (o == c) return n;
        return nullptr;
    }
    Object* n = original->cs_Clone();
    n->name_ = cs::String::Concat(original->get_name(), CS_LIT(u"(Clone)"));
    return n;
}

Object* InstantiateObjectAt(Object* original, Vector3 position, Quaternion rotation, Transform* parent)
{
    if (!QIsIdentity(rotation)) cs::NotTranslated("Instantiate with a rotation");
    if (!Object::op_Implicit(original)) cs::Throw(cs::gcnew<cs::ArgumentException>(CS_LIT(u"The Object you want to instantiate is null.")));
    GameObject* g = dynamic_cast<GameObject*>(original);
    Component* c = dynamic_cast<Component*>(original);
    if (c) g = c->gameObject_;
    if (!g) cs::NotTranslated("Instantiate at a position of a non-GameObject");
    Pairs pairs;
    GameObject* root = InstantiateTree(g, parent, true, &position, pairs);
    if (!c) return root;
    if (c == g->transform_) return root->transform_;
    for (const auto& [o, n] : pairs)
        if (o == c) return n;
    return nullptr;
}

} // namespace UnityEngine

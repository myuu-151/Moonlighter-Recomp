#include "bind/Prefabs.h"

#include <map>

namespace bind {

using namespace UnityEngine;

namespace {

// A prefab field's value as the generated cs_SetField reads it: a number from the extraction's
// numbers, anything else from its raw JSON (bind/Serial.h).
class PrefabValue : public SerialValue {
public:
    PrefabValue(const game::Behaviour& b, const std::string& field, GameObject* root)
        : b_(b), field_(field), raw_(Raw(b, field)), json_(raw_ ? *raw_ : Null(), root)
    {
    }
    bool AsBool() const override { return b_.Field(field_) != 0.0; }
    int32_t AsInt() const override { return static_cast<int32_t>(b_.Field(field_)); }
    float AsFloat() const override { return static_cast<float>(b_.Field(field_)); }
    cs::String* AsString() const override { return json_.AsString(); }
    unity::Vector2 AsVector2() const override { return json_.AsVector2(); }
    Vector3 AsVector3() const override { return json_.AsVector3(); }
    Color AsColor() const override { return json_.AsColor(); }
    GameObject* AsGameObject() const override { return json_.AsGameObject(); }
    int32_t Length() const override { return json_.Length(); }
    void Element(int32_t i, const std::function<void(const SerialValue&)>& f) const override { json_.Element(i, f); }
    void Fields(const std::function<void(const char*, const SerialValue&)>& f) const override { json_.Fields(f); }

private:
    static const json::Value* Raw(const game::Behaviour& b, const std::string& field)
    {
        auto it = b.raw.find(field);
        return it == b.raw.end() ? nullptr : &it->second;
    }
    static const json::Value& Null()
    {
        static const json::Value v;
        return v;
    }
    const game::Behaviour& b_;
    const std::string& field_;
    const json::Value* raw_;
    JsonSerialValue json_;
};

std::string Leaf(const std::string& path)
{
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string ParentPath(const std::string& path)
{
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

} // namespace

GameObject* FindBelow(GameObject* root, const std::string& path)
{
    if (!root) return nullptr;
    if (path.empty()) return root;
    Transform* t = root->transform_;
    size_t at = 0;
    while (t) {
        const size_t slash = path.find('/', at);
        const std::string head = path.substr(at, slash == std::string::npos ? std::string::npos : slash - at);
        Transform* next = nullptr;
        for (Transform* c : t->children_)
            if (c->gameObject_->get_name()->ToUtf8() == head) {
                next = c;
                break;
            }
        t = next;
        if (slash == std::string::npos) break;
        at = slash + 1;
    }
    return t ? t->gameObject_ : nullptr;
}

GameObject* InstantiatePrefab(const game::Prefab& prefab, Transform* parent, const PrefabOptions& options, AddScript addScript,
                              std::vector<std::string>* skipped)
{
    std::map<std::string, GameObject*> byPath;
    GameObject* root = nullptr;
    // The objects, inactive at the root until the tree is made.
    for (const game::PrefabNode& n : prefab.nodes) {
        const bool isRoot = n.path.empty();
        GameObject* go = GameObject::Create(isRoot ? prefab.name + "(Clone)" : Leaf(n.path));
        go->activeSelf_ = isRoot ? false : n.active;
        go->layer_ = n.objectLayer;
        if (!n.tag.empty()) go->tag_ = cs::String::Intern(cs::String::FromUtf8(n.tag));
        Transform* t = go->transform_;
        t->localPosition_ = Vector3{n.x, n.y, 0.0f};
        t->localScale_ = Vector3{n.sx, n.sy, 1.0f};
        t->rotationZ_ = n.rotation;
        if (isRoot) {
            root = go;
            if (parent) t->SetParent(parent, false);
        } else {
            GameObject* p = byPath.count(ParentPath(n.path)) ? byPath[ParentPath(n.path)] : root;
            t->SetParent(p->transform_, false);
        }
        t->physicsDirty_ = false;
        byPath[n.path] = go;
    }
    // Their components.
    for (const game::PrefabNode& n : prefab.nodes) {
        GameObject* go = byPath[n.path];
        if (n.HasRenderer()) {
            auto* r = cs::gcnew<SpriteRenderer>();
            r->sprite_ = n.sprite.empty() ? nullptr : Sprite::Named(n.sprite);
            r->sortingOrder_ = n.order;
            r->sortingLayer_ = cs::String::FromUtf8(n.layer);
            r->flipX_ = n.flipX;
            r->flipY_ = n.flipY;
            r->color_ = Color{n.color[0], n.color[1], n.color[2], n.color[3]};
            r->enabled_ = n.rendererEnabled;
            go->Attach(r);
        }
        for (const game::ColliderDef& c : n.colliders) {
            Collider2D* col = nullptr;
            if (c.type == "BoxCollider2D") {
                auto* b = cs::gcnew<BoxCollider2D>();
                b->size_ = Vector2{c.sx, c.sy};
                col = b;
            } else if (c.type == "CircleCollider2D") {
                auto* b = cs::gcnew<CircleCollider2D>();
                b->radius_ = c.radius;
                col = b;
            } else if (c.type == "PolygonCollider2D") {
                auto* b = cs::gcnew<PolygonCollider2D>();
                for (const auto& path : c.paths) {
                    b->paths_.emplace_back();
                    for (const auto& [x, y] : path) b->paths_.back().push_back(Vector2{x, y});
                }
                col = b;
            } else {
                continue;
            }
            col->offset_ = Vector2{c.ox, c.oy};
            col->isTrigger_ = c.trigger;
            col->enabled_ = c.enabled;
            col->layerSlot_ = n.objectLayer;
            go->Attach(col);
        }
        if (n.hasRigidbody) {
            auto* rb = cs::gcnew<Rigidbody2D>();
            rb->def_ = n.rigidbody;
            go->Attach(rb);
        }
        if (n.path.empty() && options.controller) {
            auto* a = cs::gcnew<Animator>();
            a->SetController(options.controller);
            go->Attach(a);
        }
        for (const game::Behaviour& b : n.behaviours) {
            Component* c = addScript ? addScript(go, b.cls.c_str()) : nullptr;
            if (!c) {
                if (skipped) skipped->push_back(b.cls);
                continue;
            }
            auto* o = dynamic_cast<Object*>(c);
            o->cs_SerialInit();
            for (const auto& [field, v] : b.fields) {
                (void)v;
                PrefabValue pv(b, field, root);
                o->cs_SetField(field.c_str(), pv);
            }
            for (const auto& [field, v] : b.raw) {
                (void)v;
                if (b.fields.count(field)) continue;
                PrefabValue pv(b, field, root);
                o->cs_SetField(field.c_str(), pv);
            }
        }
    }
    if (options.active && !prefab.nodes.empty() && prefab.nodes[0].active) root->SetActive(true);
    return root;
}

} // namespace bind

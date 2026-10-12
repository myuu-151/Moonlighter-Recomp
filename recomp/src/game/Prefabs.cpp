#include "game/Prefabs.h"

namespace game {

namespace {

struct Library {
    std::map<std::string, std::map<std::string, Prefab>> byBundle; // bundle -> name -> prefab
    std::vector<std::string> layers;
};

Library& Lib()
{
    static Library lib;
    return lib;
}

float F(const json::Value& v) { return v.IsInt() || v.IsDouble() ? static_cast<float>(v.AsDouble()) : 0.0f; }

} // namespace

void PrefabLibrary::Load(const json::Value& extracted)
{
    Library& lib = Lib();
    lib.byBundle.clear();
    lib.layers.clear();
    for (const json::Value& l : extracted["sortingLayers"].arr) lib.layers.push_back(l.s);
    for (const auto& [bundle, prefabs] : extracted["bundles"].obj) {
        auto& out = lib.byBundle[bundle];
        for (const auto& [name, rows] : prefabs.obj) {
            Prefab& p = out[name];
            p.name = name;
            p.bundle = bundle;
            for (const json::Value& r : rows.arr) {
                PrefabNode n;
                n.path = r[0].s;
                n.active = r[1].b;
                n.x = F(r[2]);
                n.y = F(r[3]);
                n.sx = F(r[4]);
                n.sy = F(r[5]);
                if (r[6].IsString()) n.sprite = r[6].s;
                if (r[7].IsString()) n.layer = r[7].s;
                n.order = static_cast<int>(r[8].i);
                n.flipX = r[9].b;
                n.flipY = r[10].b;
                if (r[11].IsArray())
                    for (int c = 0; c < 4; c++) n.color[c] = F(r[11][static_cast<size_t>(c)]);
                n.rendererEnabled = r[12].b;
                if (r.size() > 13) n.rotation = F(r[13]);
                if (r.size() > 17 && r[17].IsArray() && r[17].size() == 2) {
                    n.hasQuaternion = true;
                    n.qz = F(r[17][0]);
                    n.qw = F(r[17][1]);
                }
                if (r.size() > 14)
                    for (const json::Value& b : r[14].arr) {
                        Behaviour beh;
                        if (b[0].IsString()) beh.cls = b[0].s;
                        for (const auto& [field, v] : b[1].obj) {
                            if (v.IsArray()) {
                                beh.fields[field] = static_cast<double>(v.size());
                                beh.raw[field] = v;
                            } else if (v.kind == json::Value::Kind::Object) {
                                beh.raw[field] = v;
                            } else {
                                // (A string or null, e.g. an object reference's path: raw too.)
                                if (v.IsString() || v.IsNull()) beh.raw[field] = v;
                                beh.fields[field] = v.kind == json::Value::Kind::Bool ? (v.b ? 1.0 : 0.0) : static_cast<double>(F(v));
                            }
                        }
                        n.behaviours.push_back(beh);
                    }
                if (r.size() > 15) {
                    const json::Value& phys = r[15];
                    n.objectLayer = static_cast<int>(phys[0].AsInt());
                    n.tag = phys[1].s;
                    for (const json::Value& c : phys[2].arr) {
                        ColliderDef d;
                        d.type = c[0].s;
                        d.enabled = c[1].b;
                        d.trigger = c[2].b;
                        d.ox = F(c[3]);
                        d.oy = F(c[4]);
                        if (d.type == "BoxCollider2D") {
                            d.sx = F(c[5]);
                            d.sy = F(c[6]);
                        } else if (d.type == "CircleCollider2D") {
                            d.radius = F(c[5]);
                        } else if (d.type == "PolygonCollider2D") {
                            for (const json::Value& path : c[5].arr) {
                                d.paths.emplace_back();
                                for (const json::Value& pt : path.arr) d.paths.back().emplace_back(F(pt[0]), F(pt[1]));
                            }
                        }
                        n.colliders.push_back(std::move(d));
                    }
                }
                if (r.size() > 16 && r[16].IsObject()) {
                    const json::Value& rb = r[16];
                    n.hasRigidbody = true;
                    n.rigidbody.bodyType = static_cast<unity::RigidbodyType2D>(rb["bodyType"].AsInt());
                    n.rigidbody.mass = F(rb["mass"]);
                    n.rigidbody.linearDrag = F(rb["linearDrag"]);
                    n.rigidbody.angularDrag = F(rb["angularDrag"]);
                    n.rigidbody.gravityScale = F(rb["gravityScale"]);
                    n.rigidbody.constraints = static_cast<int>(rb["constraints"].AsInt());
                    n.rigidbody.collisionDetection = static_cast<int>(rb["collisionDetection"].AsInt());
                    n.rigidbody.sleepingMode = static_cast<int>(rb["sleepingMode"].AsInt());
                }
                p.nodes.push_back(n);
            }
        }
    }
}

bool PrefabLibrary::Loaded() { return !Lib().byBundle.empty(); }

const Prefab* PrefabLibrary::Find(const std::string& name, const std::string& bundle)
{
    const Library& lib = Lib();
    if (!bundle.empty()) {
        auto b = lib.byBundle.find(bundle);
        if (b != lib.byBundle.end()) {
            auto p = b->second.find(name);
            if (p != b->second.end()) return &p->second;
        }
    }
    for (const auto& [b, prefabs] : lib.byBundle) {
        auto p = prefabs.find(name);
        if (p != prefabs.end()) return &p->second;
    }
    return nullptr;
}

const std::vector<std::string>& PrefabLibrary::SortingLayers() { return Lib().layers; }

} // namespace game

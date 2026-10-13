// The collector on the Unity facade's objects (docs/translator_plan.md, "Memory"): a UnityEngine.Object
// is a root while its native side holds it (not destroyed); destroyed, it lives on (== null) while
// something references it, and is collected with what only it references once nothing does.
//   - a GameObject tree (a child, a collider, a body, a renderer and its own material, a script)
//     destroyed and referenced from a live list: kept, == null, its tree with it;
//   - the reference dropped: the whole tree freed (the destroyed objects counted), the scene's and
//     the physics' tables holding none of it;
//   - an object never destroyed and referenced by nothing: kept (the native side holds it);
//   - a renderer's own material instance: collected with its renderer.
// Run with ML_GC_POISON=1 (ctest does): what's freed is overwritten, so a table that kept a
// pointer to it crashes the next step.

#include <cstdio>
#include <string>

#include <Box2D/Box2D.h>

#include "bind/UnityEngine.h"
#include "unity/PhysicsWorld.h"

namespace {

int gFailed = 0, gPassed = 0;

void Check(bool ok, const std::string& what)
{
    if (ok) gPassed++;
    else {
        gFailed++;
        std::printf("FAIL: %s\n", what.c_str());
    }
}

bool Live(const UnityEngine::Object* o, int32_t hash)
{
    for (UnityEngine::Object* l : UnityEngine::Object::Live())
        if (l == o && l->GetHashCode() == hash) return true;
    return false;
}

} // namespace

int main()
{
    using namespace UnityEngine;
    bind::Scene scene;
    unity::PhysicsWorld world;
    bind::PhysicsScene physics(world, scene);
    cs::Heap& heap = cs::CurrentHeap();
    heap.Collect();
    const size_t liveBefore = Object::Live().size();
    const size_t freedBefore = Object::DestroyedFreed();

    // A tree: root (a body, its box, a renderer with its own material) and a child (a trigger box).
    GameObject* root = GameObject::Create("Enemy");
    GameObject* child = GameObject::Create("AttackCollider", root->transform_);
    auto* renderer = cs::gcnew<SpriteRenderer>();
    root->Attach(renderer);
    Material* material = renderer->get_material();
    auto* box = cs::gcnew<BoxCollider2D>();
    root->Attach(box);
    auto* rb = cs::gcnew<Rigidbody2D>();
    rb->def_.gravityScale = 0.0f;
    root->Attach(rb);
    auto* trigger = cs::gcnew<BoxCollider2D>();
    trigger->isTrigger_ = true;
    child->Attach(trigger);
    physics.SyncColliders(rb);
    Check(rb->body_ != nullptr && rb->body_->Body()->GetFixtureList() != nullptr, "the tree's body and fixtures made");
    const int32_t rootHash = root->GetHashCode(), childHash = child->GetHashCode(), materialHash = material->GetHashCode(), rbHash = rb->GetHashCode();
    // A step with it in the world.
    physics.Step(0.01f);

    // Not destroyed: the scene holds it.
    heap.Collect();
    Check(Live(root, rootHash) && Live(child, childHash) && Live(material, materialHash), "a live tree: kept (the scene holds it)");

    // Destroyed, and referenced from a live object's field (a list a root holds).
    cs::Root<cs::List<Object*>> holder(cs::New<cs::List<Object*>>());
    holder->Add(root);
    Object::Destroy(root);
    scene.ProcessDestroys();
    Check(root->destroyed_ && child->destroyed_ && rb->destroyed_, "Destroy: the tree destroyed at the frame's end");
    heap.Collect();
    Check(Live(root, rootHash) && Live(child, childHash) && Live(rb, rbHash), "destroyed but referenced: kept, its tree with it");
    Check(Object::op_Equality(holder->get_Item(0), nullptr) && !Object::op_Implicit(holder->get_Item(0)), "a destroyed object kept alive is == null");
    physics.Step(0.01f); // (the physics' tables hold nothing of it)

    // The reference dropped: the whole tree collected, its material instance with its renderer.
    holder->Clear();
    const size_t freed = heap.Collect();
    Check(!Live(root, rootHash) && !Live(child, childHash) && !Live(rb, rbHash), "unreferenced: the destroyed tree collected");
    Check(!Live(material, materialHash), "the renderer's own material instance collected with it");
    Check(Object::DestroyedFreed() - freedBefore >= 7,
          "the destroyed objects freed: " + std::to_string(Object::DestroyedFreed() - freedBefore) + " (" + std::to_string(freed) + " objects in all)");
    Check(Object::Live().size() == liveBefore, "the live objects back to before the tree (" + std::to_string(Object::Live().size()) + " vs " +
                                                   std::to_string(liveBefore) + ")");
    physics.Step(0.01f); // (no dangling table: a poisoned object would crash this)
    scene.UpdateAnimators(0.01f);

    // Never destroyed, referenced by nothing: the native side holds it.
    auto* loose = cs::gcnew<GameObject>();
    const int32_t looseHash = loose->GetHashCode();
    heap.Collect();
    Check(Live(loose, looseHash), "an object never destroyed, referenced by nothing: kept");

    std::printf("bind_gc: %d passed, %d failed\n", gPassed, gFailed);
    return gFailed ? 1 : 0;
}

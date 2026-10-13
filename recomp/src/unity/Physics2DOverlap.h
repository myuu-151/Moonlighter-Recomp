#pragma once
// Physics2D's overlap queries that see every collider of the physics world, the moving bodies'
// included (unity/Physics2D's queries see only the room's fixed colliders):
// Physics2D.OverlapCircleAll, as the KamikazeGolem's explosion uses it.
//
// As Unity runs it on its Box2D: the world's broad-phase query for the circle's box
// (b2World::QueryAABB: the proxies' fat boxes, in the dynamic tree's order), then for each
// fixture the layer mask, the trigger setting (queriesHitTriggers) and the exact test
// b2TestOverlap(circle, the fixture's shape) (b2Distance with both radii: the polygons' skin
// counts); each collider once (a compound collider's fixtures are one collider). The bodies are
// where the last fixed step or sync left them: autoSyncTransforms is off, so a transform a
// script moved since isn't seen there yet.

#include <vector>

#include "unity/Vector2.h"

class b2Fixture;

namespace unity {

class PhysicsWorld;

// Physics2D.DefaultRaycastLayers: every layer but Ignore Raycast (2).
constexpr int kDefaultRaycastLayers = ~(1 << 2);

// Physics2D.OverlapCircleAll(point, radius, layerMask): the fixtures of the colliders the circle
// overlaps (one per collider), in the broad phase's order.
std::vector<const b2Fixture*> OverlapCircleAll(PhysicsWorld& world, Vector2 point, float radius, int layerMask = kDefaultRaycastLayers,
                                               bool queriesHitTriggers = true);

} // namespace unity

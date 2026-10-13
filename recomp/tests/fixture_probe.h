#pragma once
// The fixtures Unity made between two fixed steps for objects a replay's world doesn't hold
// (oracle/MoonlighterOracle/FixtureProbe.cs: each recorded step's "fx", [made, reshaped,
// staticMoved, childMoved, trigger, body]). Box2D's e_newFixture: the step pairs every proxy that
// moved (FindNewContacts) before it collides, not after it solves, so a contact whose proxy was
// touched since (the hero's, refiltered when his layer changes) is solved a step earlier
// (tangle.jsonl, frame 406; unity::PhysicsWorld::FixtureMadeElsewhere).
//
// The replays call FixtureMadeElsewhere before a step whose "fx" has a collider made (switched on,
// new, its object activated), reshaped, or moved where Unity must remake its shapes (one without
// a Rigidbody2D, in world space; one below its body's object), or switched to or from a trigger
// (Unity remakes its shapes: enemy_BrokenGolemTurret.jsonl of enemyfix-turret-2, frame 206, a
// shot's isTrigger set as it crashed, in "fxOwn"; game/EnemyProjectile.cpp). A body's type or
// simulated flag isn't counted: whether Unity remakes fixtures for those isn't known, and the
// recordings will show it. "fx" leaves out the recorded enemies' own colliders (they are
// in "fxOwn"; so are the BrokenGolemTurret's shots, pooled outside its hierarchy, which the turret
// replay makes itself): the port remakes those itself (Box2D's CreateFixture sets e_newFixture),
// and taking them from the recording would hide a remake the port misses. The room's critters
// stay in "fx": a replay places them (tests/enemy_replay_critters.h) but doesn't switch them on. Recordings from before the probe
// have no "fx": the replays then guess from the weapon's hits (the hero's hitbox switched on in
// the frame).

#include "util/Json.h"

namespace {

// Whether this recording has the probe (its first frame with a step has "fx").
inline bool RecordsFixtures(const json::Value& frame)
{
    const json::Value& steps = frame["steps"];
    return !steps.arr.empty() && steps[0].Find("fx") != nullptr;
}

inline bool StepMadeFixtures(const json::Value& step)
{
    const json::Value* fx = step.Find("fx");
    if (!fx || fx->size() < 4) return false;
    for (size_t k = 0; k < 5 && k < fx->size(); k++)
        if ((*fx)[k].AsInt() > 0) return true;
    return false;
}

} // namespace

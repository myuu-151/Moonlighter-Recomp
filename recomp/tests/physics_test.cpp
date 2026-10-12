// unity::PhysicsWorld (Box2D 2.3.1) with the hero's Rigidbody2D:
//  - without contacts, a fixed step's round trip (sync the transform, step, write back) is
//    (p + c) - c bit for bit, also after he falls asleep, and the hero's controller walks and
//    rolls exactly as without the world;
//  - put into a 24x24 Kinematic rock (layer RisenObstacle) he's pushed out along the axis of
//    least penetration, from the second step (stock Box2D finds the contact at the end of the
//    first), each step as Box2D's position solver says (Baumgarte 0.2, slop 0.005, at most
//    0.2 per correction, 16 iterations stopping once the separation is above -3 slop: replayed
//    here from the contact's manifold), settling 0.005..0.015 inside the rock's skin;
//  - the layer matrix: rolling (IntangibleHero) still hits rocks, an ignored pair doesn't;
//  - walking into the rock through the controller: straight on, his casts stop him and the
//    world changes nothing; with no casts, the world holds him at the rock.
//
// Usage: physics_test   (the controller's checks need ML_DATA's animators/hero_merchant.json)

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include <Box2D/Box2D.h>

#include "game/HeroMerchantController.h"
#include "unity/Animator.h"
#include "unity/Physics2D.h"
#include "unity/PhysicsWorld.h"
#include "util/Json.h"

using unity::Vector2;

namespace {

int checks = 0, failures = 0;

void Check(bool ok, const std::string& what)
{
    checks++;
    if (!ok) {
        failures++;
        std::printf("FAIL %s\n", what.c_str());
    }
}

bool Same(float a, float b) { return std::memcmp(&a, &b, 4) == 0; }
bool Same(Vector2 a, Vector2 b) { return Same(a.x, b.x) && Same(a.y, b.y); }

std::string Str(Vector2 v)
{
    char buf[96];
    std::snprintf(buf, sizeof buf, "(%.9g, %.9g)", v.x, v.y);
    return buf;
}

const Vector2 kOffset{1.0f, -3.5f}, kSize{12.0f, 12.0f}; // his BoxCollider2D
constexpr float kStep = 0.01f;

Vector2 RoundTrip(Vector2 p) { return {(p.x + kOffset.x) - kOffset.x, (p.y + kOffset.y) - kOffset.y}; }

unity::Rigidbody2DDef HeroBody()
{
    unity::Rigidbody2DDef def = game::HeroMerchantController::Settings::HeroBody();
    def.hasCenterOfMass = true;
    def.centerOfMass = kOffset;
    return def;
}

uint32_t rng = 12345u;
float Uniform(float lo, float hi)
{
    rng = rng * 1664525u + 1013904223u;
    return lo + (hi - lo) * static_cast<float>(rng >> 8) / 16777216.0f;
}

// A 24x24 rock at `at` (its Kinematic body's position, the box at offset 0).
unity::Collider2D Rock(Vector2 at)
{
    unity::Collider2D c;
    c.path = "GolemRock";
    c.layer = unity::kLayerRisenObstacle;
    c.position = at;
    c.size = {24.0f, 24.0f};
    return c;
}

// The hero's transform with his collider's centre at `centre`.
Vector2 HeroAt(Vector2 centre) { return {centre.x - kOffset.x, centre.y - kOffset.y}; }

// Without contacts: Simulate is (p + c) - c, through moves, standing still (he sleeps after
// 0.5 s) and moves again, over magnitudes where the sum changes exponent.
void RoundTripChecks()
{
    unity::PhysicsWorld world;
    unity::Physics2D far;
    far.Add(Rock({-5000.0f, -5000.0f}));
    world.AddColliders(far, 1 << unity::kLayerRisenObstacle);
    Vector2 p{1920.0f, 365.19128f};
    unity::Rigidbody2D& hero = world.AddBody(HeroBody(), p, unity::kLayerDefault, kOffset, kSize);
    Check(Same(hero.Body()->GetLocalCenter().x, 1.0f) && Same(hero.Body()->GetLocalCenter().y, -3.5f) && hero.Body()->GetMass() == 1.0f,
          "his body: mass 1, centre of mass (1, -3.5)");
    int bad = 0, steps = 0, slept = 0;
    for (int frame = 0; frame < 6000; frame++) {
        // Phases: walking, standing (long enough to sleep), jumps across magnitudes.
        const int phase = (frame / 150) % 4;
        if (phase == 0) p = p + Vector2{Uniform(-2.4f, 2.4f), Uniform(-2.4f, 2.4f)};
        else if (phase == 2) p = Vector2{Uniform(-40.0f, 40.0f), Uniform(-40.0f, 40.0f)} * (frame % 3 == 0 ? 0.01f : 1.0f);
        else if (phase == 3) p = p + Vector2{Uniform(-0.01f, 0.01f), 0.0f};
        const int fixedSteps = 1 + (frame & 1);
        for (int s = 0; s < fixedSteps; s++) {
            const Vector2 want = RoundTrip(p);
            p = world.Simulate(hero, p, kStep);
            steps++;
            slept += !hero.IsAwake();
            if (!Same(p, want) && bad++ < 5) std::printf("  round trip step %d: %s, want %s\n", steps, Str(p).c_str(), Str(want).c_str());
            if (!Same(hero.velocity(), Vector2{})) bad++;
        }
    }
    Check(bad == 0, "no contacts: every step is (p + c) - c (" + std::to_string(steps) + " steps)");
    Check(slept > 0, "he falls asleep standing still (and the round trip holds)");
}

// The position solver as Box2D 2.3.1 runs it on one contact between the static/kinematic `A`
// (centre cA, no mass) and the hero B (centre cB, inverse mass 1, no rotation): returns his
// centre after the step's position iterations.
b2Vec2 SolvePositions(const b2Manifold& m, b2Vec2 cA, b2Vec2 cB, b2Vec2 localCenterB, int iterations)
{
    const float radius = b2_polygonRadius;
    for (int it = 0; it < iterations; it++) {
        float minSeparation = 0.0f;
        for (int j = 0; j < m.pointCount; j++) {
            b2Transform xfA, xfB;
            xfA.q.Set(0.0f);
            xfB.q.Set(0.0f);
            xfA.p = cA - b2Mul(xfA.q, b2Vec2(0.0f, 0.0f));
            xfB.p = cB - b2Mul(xfB.q, localCenterB);
            b2Vec2 normal, point;
            float separation;
            if (m.type == b2Manifold::e_faceA) {
                normal = b2Mul(xfA.q, m.localNormal);
                b2Vec2 planePoint = b2Mul(xfA, m.localPoint);
                b2Vec2 clipPoint = b2Mul(xfB, m.points[j].localPoint);
                separation = b2Dot(clipPoint - planePoint, normal) - radius - radius;
                point = clipPoint;
            } else {
                normal = b2Mul(xfB.q, m.localNormal);
                b2Vec2 planePoint = b2Mul(xfB, m.localPoint);
                b2Vec2 clipPoint = b2Mul(xfA, m.points[j].localPoint);
                separation = b2Dot(clipPoint - planePoint, normal) - radius - radius;
                point = clipPoint;
                normal = -normal;
            }
            (void)point;
            minSeparation = b2Min(minSeparation, separation);
            const float C = b2Clamp(b2_baumgarte * (separation + b2_linearSlop), -b2_maxLinearCorrection, 0.0f);
            const float K = 0.0f + 1.0f; // mA + mB (no rotation)
            const float impulse = K > 0.0f ? -C / K : 0.0f;
            const b2Vec2 P = impulse * normal;
            cB += 1.0f * P;
        }
        if (minSeparation >= -3.0f * b2_linearSlop) break;
    }
    return cB;
}

// The hero's collider core against the rock's: the gap on each side (negative: overlap).
Vector2 Gaps(Vector2 heroPos, Vector2 rock)
{
    const float hx0 = heroPos.x + kOffset.x - 6.0f, hx1 = heroPos.x + kOffset.x + 6.0f;
    const float hy0 = heroPos.y + kOffset.y - 6.0f, hy1 = heroPos.y + kOffset.y + 6.0f;
    return {std::fmax(rock.x - 12.0f - hx1, hx0 - (rock.x + 12.0f)), std::fmax(rock.y - 12.0f - hy1, hy0 - (rock.y + 12.0f))};
}

// Placed overlapping the rock by `overlap` (x, y) at its top-left corner: pushed out along the
// axis it overlaps least, every step as the position solver says.
void PushChecks(Vector2 overlap, const char* name, bool contactsOnSync = false)
{
    unity::PhysicsWorld::Options options;
    options.contactsOnSync = contactsOnSync;
    unity::PhysicsWorld world(unity::Physics2DSettings(), options);
    unity::Physics2D room;
    const Vector2 rock{1800.0f, 300.0f};
    room.Add(Rock(rock));
    world.AddColliders(room, 1 << unity::kLayerRisenObstacle);
    // His collider's right edge `overlap.x` into the rock's left, its bottom `overlap.y` into its top.
    Vector2 p = HeroAt({rock.x - 12.0f - 6.0f + overlap.x, rock.y + 12.0f + 6.0f - overlap.y});
    unity::Rigidbody2D& hero = world.AddBody(HeroBody(), Vector2{p.x - 100.0f, p.y}, unity::kLayerDefault, kOffset, kSize);
    world.Simulate(hero, Vector2{p.x - 100.0f, p.y}, kStep); // apart first, as when walking up
    const int axis = overlap.x < overlap.y ? 0 : 1;          // least penetration

    const Vector2 start = p;
    p = world.Simulate(hero, p, kStep);
    if (!contactsOnSync)
        Check(Same(p, RoundTrip(start)), std::string(name) + ": the step that starts overlapping corrects nothing (contact found at its end)");
    else
        Check(!Same(p, RoundTrip(start)), std::string(name) + ": with contactsOnSync the first step corrects");

    int bad = 0, sideways = 0;
    b2Body* rockBody = nullptr;
    for (b2ContactEdge* e = hero.Body()->GetContactList(); e; e = e->next) rockBody = e->other;
    for (int s = 0; s < 120; s++) {
        const b2Vec2 c0 = hero.Body()->GetWorldCenter();
        const Vector2 before = p;
        p = world.Simulate(hero, p, kStep);
        const b2Contact* contact = hero.Body()->GetContactList() ? hero.Body()->GetContactList()->contact : nullptr;
        if (!contact || !rockBody) {
            bad++;
            break;
        }
        const bool heroIsB = contact->GetFixtureB()->GetBody() == hero.Body();
        b2Vec2 want;
        if (heroIsB) {
            want = SolvePositions(*contact->GetManifold(), rockBody->GetWorldCenter(), c0, hero.Body()->GetLocalCenter(),
                                  world.Settings().positionIterations);
        } else {
            bad++; // (the rock's proxy is older: it's always A)
            break;
        }
        const Vector2 wantPos{want.x - 1.0f, want.y - (-3.5f)};
        if (!Same(p, wantPos) && bad++ < 5)
            std::printf("  %s step %d: %s, the solver says %s\n", name, s, Str(p).c_str(), Str(wantPos).c_str());
        // Only along the axis of least penetration; the other keeps its round trip.
        if (axis == 0 ? !Same(p.y, RoundTrip(before).y) : !Same(p.x, RoundTrip(before).x)) sideways++;
        if (!Same(hero.velocity(), Vector2{})) bad++;
    }
    Check(bad == 0, std::string(name) + ": each step as Box2D's position solver says");
    Check(sideways == 0, std::string(name) + ": pushed out along " + (axis == 0 ? "x" : "y") + " only");
    const Vector2 g = Gaps(p, rock);
    const float gap = axis == 0 ? g.x : g.y;
    // At rest the separation (core gap - both skins, 0.02) is above -3 slop and below -slop.
    Check(gap >= 0.005f && gap < 0.015f, std::string(name) + ": settles inside the skins, core gap " + std::to_string(gap));
    Check(hero.TouchingCount() == 1, std::string(name) + ": still touching the rock");
}

// The layer matrix: Default and IntangibleHero both hit RisenObstacle; an ignored pair doesn't.
void LayerChecks()
{
    unity::Physics2DSettings settings;
    Check(settings.Collide(unity::kLayerDefault, unity::kLayerRisenObstacle) && settings.Collide(unity::kLayerIntangibleHero, unity::kLayerRisenObstacle),
          "the hero hits rocks, walking and rolling");
    Check(!settings.Collide(unity::kLayerIntangibleHero, 12) && settings.Collide(unity::kLayerDefault, 12),
          "rolling, he passes ground enemies (layer 12)");
    if (const char* data = std::getenv("ML_DATA")) {
        std::ifstream f(std::string(data) + "/physics2d.json", std::ios::binary);
        if (f) {
            std::ostringstream ss;
            ss << f.rdbuf();
            unity::Physics2DSettings loaded;
            Check(loaded.Load(json::Parse(ss.str())), "physics2d.json loads");
            for (int l : {unity::kLayerDefault, unity::kLayerRisenObstacle, unity::kLayerIntangibleHero})
                Check(loaded.layerCollisionMatrix[l] == settings.layerCollisionMatrix[l], "physics2d.json's row " + std::to_string(l) + " is the built-in one");
            Check(loaded.velocityIterations == 24 && loaded.positionIterations == 16 && Same(loaded.gravity.y, -9.81f), "physics2d.json's iterations and gravity");
        }
    }

    for (int ignore = 0; ignore < 2; ignore++) {
        unity::PhysicsWorld world;
        if (ignore) world.IgnoreLayerCollision(unity::kLayerIntangibleHero, unity::kLayerRisenObstacle);
        unity::Physics2D room;
        const Vector2 rock{0.0f, 0.0f};
        room.Add(Rock(rock));
        world.AddColliders(room, 1 << unity::kLayerRisenObstacle);
        const Vector2 p = HeroAt({-12.0f - 6.0f + 2.0f, 0.0f});
        unity::Rigidbody2D& hero = world.AddBody(HeroBody(), p, unity::kLayerIntangibleHero, kOffset, kSize);
        Vector2 q = p;
        for (int s = 0; s < 20; s++) q = world.Simulate(hero, q, kStep);
        Check(ignore ? Same(q, RoundTrip(p)) : q.x < p.x, ignore ? "an ignored layer pair doesn't push" : "rolling (IntangibleHero), a rock pushes him");
    }
}

std::string ReadFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The controller with and without a world: the same frames.
void ControllerChecks(const unity::AnimatorController& controller)
{
    game::HeroMerchantController::Settings settings;
    settings.colliderSize = kSize;
    settings.colliderOffset = kOffset;
    settings.lowerDeadZone = 0.4f;
    settings.collisionLayers = (1 << unity::kLayerRisenObstacle) | (1 << unity::kLayerLevelBoundary);
    settings.slideLayers = settings.collisionLayers;
    settings.godModeLayers = settings.collisionLayers;
    settings.abilityCooldown = 0.6f;

    // Free walking and rolling, no colliders: bit for bit the same.
    {
        unity::Physics2D empty;
        unity::PhysicsWorld world;
        game::HeroMerchantController a(controller, settings, empty), b(controller, settings, empty);
        b.physicsWorld = &world;
        a.position = b.position = {1920.0f, 365.19128f};
        a.animator.Play("IdleDown", 0.0f);
        b.animator.Play("IdleDown", 0.0f);
        int bad = 0, rolls = 0;
        rng = 777u;
        for (int f = 0; f < 3000; f++) {
            game::HeroMerchantController::Input in;
            const int seg = (f / 40) % 8;
            const Vector2 sticks[8] = {{1, 0}, {0, 0}, {0, 1}, {-0.7071068f, -0.7071068f}, {0, 0}, {-1, 0}, {0.2f, 0}, {1, 0.4f}};
            in.stick = sticks[seg];
            in.roll = f % 97 == 5;
            const float dt = Uniform(0.0160f, 0.0175f);
            a.Update(dt, in);
            b.Update(dt, in);
            rolls += a.isIntangible;
            if ((!Same(a.position, b.position) || a.isIntangible != b.isIntangible) && bad++ < 5)
                std::printf("  frame %d: world %s, without %s\n", f, Str(b.position).c_str(), Str(a.position).c_str());
        }
        Check(bad == 0, "the controller with an empty world walks and rolls bit for bit as without");
        Check(rolls > 0 && b.Body() && b.Body()->bodyType() == unity::RigidbodyType2D::Dynamic, "his body made in the world; he rolled");
    }

    // Walking right into a rock, straight on: his casts stop him short; the world adds nothing.
    // With no casts (layers 0), the world alone holds him at it.
    for (int casts = 1; casts >= 0; casts--) {
        game::HeroMerchantController::Settings s = settings;
        if (!casts) s.collisionLayers = s.slideLayers = 0;
        unity::Physics2D room;
        const Vector2 rock{2000.0f, 361.69128f};
        room.Add(Rock(rock));
        unity::PhysicsWorld world;
        world.AddColliders(room, 1 << unity::kLayerRisenObstacle);
        game::HeroMerchantController a(controller, s, room), b(controller, s, room);
        b.physicsWorld = &world;
        a.position = b.position = {1920.0f, 365.19128f};
        a.animator.Play("IdleDown", 0.0f);
        b.animator.Play("IdleDown", 0.0f);
        int differ = 0;
        float worst = 0.0f;
        for (int f = 0; f < 150; f++) {
            game::HeroMerchantController::Input in;
            in.stick = f < 120 ? Vector2{1.0f, 0.0f} : Vector2{};
            a.Update(1.0f / 60.0f, in);
            b.Update(1.0f / 60.0f, in);
            differ += !Same(a.position, b.position);
            if (f >= 30) worst = std::fmin(worst, Gaps(b.position, rock).x);
        }
        const float gap = Gaps(b.position, rock).x;
        if (casts) {
            Check(differ == 0 && gap > 0.0f, "straight into a rock: his casts stop him, the world adds nothing (gap " + std::to_string(gap) + ")");
        } else {
            Check(a.position.x + kOffset.x - 6.0f > rock.x + 12.0f, "with no casts and no world he walks through the rock");
            Check(gap >= 0.005f && gap < 0.015f && worst > -3.0f,
                  "with no casts the world holds him at the rock (rest gap " + std::to_string(gap) + ", deepest " + std::to_string(worst) + ")");
            Check(Same(b.position.y, a.position.y), "pushed back along x only");
        }
    }
}

} // namespace

int main()
{
    RoundTripChecks();
    PushChecks({1.0f, 3.0f}, "x overlap 1, y overlap 3");
    PushChecks({3.0f, 1.0f}, "x overlap 3, y overlap 1");
    PushChecks({4.5f, 12.0f}, "into the left face");
    PushChecks({1.0f, 3.0f}, "contactsOnSync", true);
    LayerChecks();

    const char* data = std::getenv("ML_DATA");
    const std::string animPath = std::string(data ? data : "data/extracted") + "/animators/hero_merchant.json";
    const std::string anim = ReadFile(animPath);
    if (anim.empty()) {
        std::printf("no %s: the controller's checks skipped\n", animPath.c_str());
    } else {
        unity::AnimatorController controller = unity::AnimatorController::FromJson(json::Parse(anim));
        ControllerChecks(controller);
    }

    std::printf("\n%d checks, %d failed\n%s\n", checks, failures, failures ? "FAILURES" : "ALL PASS");
    return failures ? 1 : 0;
}

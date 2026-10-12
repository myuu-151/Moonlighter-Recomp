// The Golem dungeon's enemies past the BabySlime (slime_test has that one): GolemSoldier first.
//  1. self-checks that need no recording: the screen shake's Random draws (insideUnitSphere's
//     three, DOTween.Shake's), and with the game's data (ML_DATA, default data/extracted) the
//     GolemSoldier from its prefab (the factory's behaviour, its box off in the prefab) and a
//     soldier in a box with the hero standing: it waits its second, chases him, stops within 2
//     tiles + 1 (diamond) of his feet and swings, its clip events in order (ForbidEnemyMovement,
//     LockLookDirection, DoScreenShakeFromEnemyAnimation with its 15 draws, PlayHitOnGroundSound,
//     AllowEnemyMovement, UnLockLookDirection), its AttackCollider on only in the swing's
//     window, grown to 48 x 24 and moved by the curves, with a fixture only while it's on, and
//     each swing hitting him once (the box turned on again enters again; remade as it grows, it
//     doesn't), MasterAudio's draw for the sword's crash (sound_groups.json: its loops'
//     Range(1, 6));
//     The HermitSlime's (prefab HermitSlime): tests/enemy_hermit_checks.h (its patrol, its slam,
//     its root box, its hurtbox, ItemDrain's draws);
//  2. with the oracle's recording (data/oracle/enemy_<prefab>.jsonl, `-oracle-enemy=<prefab>`;
//     skipped if there's none): tests/enemy_replay.h's replay, with the attack trigger per fixed
//     step and frame, the look lock, and the clip events and their Random draws (and
//     tests/enemy_replay_colliders.h's rows: the root box, the hurtboxes, the patrol, ItemDrain).
//
// Usage: enemy_test [enemy_GolemSoldier.jsonl] [prefab (GolemSoldier, HermitSlime)]
//        (ML_HERMIT_BOX=1: the hermit's root box in its attack frames)
//        (ML_ENEMY_DEBUG=1: every mismatch; ML_ENEMY_CONTACTS=<frame>: the enemies' Box2D
//        contacts after each of that frame's steps; ML_ENEMY_BOX=1: the soldier in a box's
//        frames with its attack box on)

#include "enemy_replay.h"
#include "enemy_hermit_checks.h" // (the HermitSlime's self-checks)

namespace {

struct Data {
    std::string dir;
    Value enemyStats, physics2d, soundGroups;
    std::unique_ptr<unity::AnimatorController> controller;
    const game::Prefab* prefab = nullptr;
    bool ok = false;
};

Data LoadData(const std::string& name)
{
    Data d;
    d.dir = std::getenv("ML_DATA") ? std::getenv("ML_DATA") : "data/extracted";
    const std::string constants = ReadFile(d.dir + "/constants.json"), stats = ReadFile(d.dir + "/enemy_stats.json"),
                      prefabs = ReadFile(d.dir + "/prefabs.json"), anim = ReadFile(d.dir + "/animators/" + name + ".json"),
                      physics = ReadFile(d.dir + "/physics2d.json");
    if (constants.empty() || stats.empty() || prefabs.empty() || anim.empty()) return d;
    game::Constants::LoadFromText(constants);
    d.enemyStats = json::Parse(stats);
    if (!physics.empty()) d.physics2d = json::Parse(physics);
    const std::string sounds = ReadFile(d.dir + "/sound_groups.json");
    if (!sounds.empty()) d.soundGroups = json::Parse(sounds);
    game::PrefabLibrary::Load(json::Parse(prefabs));
    d.prefab = game::PrefabLibrary::Find(name, "enemies_golem");
    d.controller = std::make_unique<unity::AnimatorController>(unity::AnimatorController::FromJson(json::Parse(anim)));
    d.ok = d.prefab != nullptr;
    return d;
}

// ---- 1. Self-checks ---------------------------------------------------------------------

void CheckShakeDraws()
{
    unity::Random::InitState(4242);
    unity::Random::State before = unity::Random::GetState();
    const unity::Random::Vector3 v = unity::Random::insideUnitSphere();
    Check(unity::Random::DrawsBetween(before, unity::Random::GetState(), 8) == 3 && v.x * v.x + v.y * v.y + v.z * v.z <= 1.0001f,
          "Random.insideUnitSphere: three draws, inside the unit sphere");
    // The enemies' rumble: 0.15 s at vibration 50: (int)(50 * 0.15) = 7 waypoints: the first
    // angle, 5 turns, 6 rotations about y.
    before = unity::Random::GetState();
    const int n = unity::ShakeDraws(0.15f, 50, 0.0f, true, false);
    Check(n == 12 && unity::Random::DrawsBetween(before, unity::Random::GetState(), 32) == 12, "DOTween.Shake (vectorBased, 7 waypoints): 12 draws");
    Check(unity::ShakeDraws(0.01f, 10, 90.0f, false, true) == 1, "DOTween.Shake: at least 2 waypoints (the first angle only, z ignored)");
}

// A GolemSoldier in an empty box with the hero standing at the origin.
void CheckSoldierInABox(const Data& data)
{
    game::PolyNav2D nav;
    nav.inflateRadius = 8.0f;
    nav.map = game::PolyNav2D::PolyMap({{{{-240.0f, 130.0f}, {-240.0f, -130.0f}, {240.0f, -130.0f}, {240.0f, 130.0f}}}}, {});
    nav.GenerateNodes();
    game::PolyNav2D::current = &nav;

    unity::Physics2DSettings settings;
    if (!data.physics2d.IsNull()) settings.Load(data.physics2d);
    unity::PhysicsWorld world(settings);
    game::EnemyPhysics physics(world);
    unity::Rigidbody2DDef heroDef;
    heroDef.mass = 1.0f;
    heroDef.linearDrag = 5.0f;
    heroDef.gravityScale = 0.0f;
    heroDef.constraints = unity::Rigidbody2DDef::FreezeRotation;
    heroDef.collisionDetection = unity::Rigidbody2DDef::Continuous;
    unity::Rigidbody2D& heroBody = world.AddBody(heroDef, {0.0f, 0.0f}, 0, {1.0f, -3.5f}, {13.0f, 13.0f});
    physics.SetHero(&heroBody);

    unity::DelayedCalls calls;
    unity::Tweens tweens;
    game::EnemyHero hero;
    int frameNow = 0;
    std::vector<int> hitFrames;
    hero.dealDamage = [&](float strength, const std::string& source, game::Enemy&) {
        hitFrames.push_back(frameNow);
        Check(source == "GolemSoldier" && strength == 15.0f, "the hit: DealDamageToPlayer(15, \"GolemSoldier\")");
    };
    int vibrations = 0;
    hero.vibrate = [&](float intensity, float duration) { vibrations += intensity == 0.2f && duration == 0.1f; };
    game::EnemyScene scene;
    scene.calls = &calls;
    scene.tweens = &tweens;
    scene.hero = &hero;
    int sounds = 0, rumbles = 0;
    game::MasterAudio audio;
    const bool haveAudio = !data.soundGroups.IsNull() && audio.Load(data.soundGroups, {"Sounds - GolemDungeon", "Sounds - GeneralDungeons"});
    if (haveAudio) scene.audio = &audio;
    else std::printf("(no sound_groups.json (tools/extract/sound_groups.py): the sound's draw not checked)\n");
    scene.playSound = [&](game::Enemy&, const std::string& sound) { sounds += sound == "golem_dungeon_soldier_sword_crash"; };
    scene.rumble = [&](float duration, float strength, Vector2, int vibration) { rumbles += duration == 0.15f && strength == 2.0f && vibration == 50; };
    // The clip events as they come, with the Random draws each made.
    std::vector<std::pair<int, std::string>> events;
    unity::Random::State eventStart{};
    bool shakeDraws = true;
    scene.onAnimationEvent = [&](game::Enemy&, const unity::AnimationEvent& e, bool after) {
        if (!after) {
            events.emplace_back(frameNow, e.function);
            eventStart = unity::Random::GetState();
            return;
        }
        const int draws = unity::Random::DrawsBetween(eventStart, unity::Random::GetState(), 64);
        if (e.function == "DoScreenShakeFromEnemyAnimation") shakeDraws = shakeDraws && draws == 15;
        else if (e.function == "PlayHitOnGroundSound") shakeDraws = shakeDraws && draws == (haveAudio ? 1 : 0);
        else shakeDraws = shakeDraws && draws == 0;
    };

    auto soldier = game::MakeEnemy(*data.prefab, scene, data.controller.get(), {{120.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}});
    auto* behaviour = dynamic_cast<game::GolemSoldierBehaviour*>(soldier->enemyBehaviour.get());
    Check(behaviour != nullptr, "the GolemSoldier prefab makes a GolemSoldierBehaviour");
    if (!behaviour) return;
    Check(soldier->hasAttackCollider && !soldier->attackEnabled && soldier->attackActive && Same(soldier->attackSize, {10.0f, 10.0f}) &&
              Same(soldier->attackOffset, {0.0f, 10.0f}) && soldier->attackLayer == game::kLayerEnemyAttack,
          "its AttackCollider: a 10 x 10 box at (0, 10) on EnemyAttack, off in the prefab");
    Check(soldier->colliderSize.x == 15.0f && soldier->tag == "HardEnemy" && soldier->layer == game::kLayerGroundEnemy,
          "its body: a 15 x 15 box, HardEnemy on GroundEnemy");
    bool hasFields = false;
    for (const game::Behaviour& b : data.prefab->nodes[0].behaviours)
        if (b.cls == "GolemSoldierBehaviour") hasFields = b.Field("attackPushBackwardsForce", -1.0) >= 0.0;
    if (hasFields) Check(behaviour->attackPushBackwardsForce == 100.0f && !behaviour->iDirectionLocked, "its fields (prefabs.py FIELDS): push 100, not locked");
    else std::printf("(prefabs.json without GolemSoldierBehaviour's fields: rerun tools/extract/prefabs.py)\n");
    game::EnemyStatModificator m;
    game::EnemyStatModificator::FromJson(data.enemyStats, "GolemSoldier", m);
    soldier->Init("GolemSoldier", m, game::EnemyStatsContext{});
    Check(soldier->navAgent.maxSpeed == 30.0f && soldier->rigidbody.mass == 12.0f && soldier->enemyStats.strength() == 15.0f,
          "Init: speed 30, mass 12, strength 15");
    physics.Add(*soldier);
    Check(soldier->attackFixture == nullptr, "no attack fixture while the box is off");
    soldier->enemyBehaviour->StartTargetingPlayerAfterTime(1.0f);

    const float dt = 1.0f / 60.0f;
    double time = 0.0, fixedTime = 0.0;
    int targetFrame = -1, attackFrame = -1;
    float attackDistance = 0.0f;
    bool fixtureOnlyWhenOn = true, grew = false, wasOn = false, lockedSeen = false;
    int swingsOn = 0; // times the box came on
    std::vector<int> onFrames;
    auto frame = [&](int f) {
        frameNow = f;
        time += static_cast<double>(dt);
        nav.BeginFrame();
        while (fixedTime + 0.01 <= time) {
            fixedTime += 0.01;
            calls.fixedNow = fixedTime;
            calls.inFixedStep = true;
            world.SyncTransform(heroBody, {0.0f, 0.0f});
            heroBody.SetVelocity({0.0f, 0.0f});
            physics.Step(0.01f);
            // The fixture follows the box (made at the step's sync).
            fixtureOnlyWhenOn = fixtureOnlyWhenOn && ((soldier->attackFixture != nullptr) == soldier->attackEnabled);
            calls.UpdateFixed(fixedTime);
            calls.inFixedStep = false;
        }
        calls.now = time;
        audio.now = time;
        audio.frameCount = f;
        tweens.Update(dt);
        soldier->enemyBehaviour->Update(dt);
        calls.Update(time);
        nav.ResumeCoroutines();
        soldier->UpdateAnimator(dt);
        nav.LateUpdate();
        soldier->navAgent.LateUpdate(dt);
        if (targetFrame < 0 && soldier->enemyBehaviour->targetPlayer) targetFrame = f;
        if (attackFrame < 0 && soldier->enemyBehaviour->enemyState == game::EnemyBehaviour::EnemyState::Attacking) {
            attackFrame = f;
            const Vector2 p = soldier->position();
            attackDistance = std::fabs(p.x - 0.0f) + std::fabs(p.y - -3.5f);
        }
        if (soldier->attackEnabled && !wasOn) {
            swingsOn++;
            onFrames.push_back(f);
        }
        wasOn = soldier->attackEnabled;
        if (std::getenv("ML_ENEMY_BOX") && soldier->attackEnabled)
            std::printf("frame %d: soldier %s state %d box %s fixture %d hits %zu\n", f, Str(soldier->position()).c_str(),
                        soldier->enemyAnimator->CurrentState(), AttackStr(*soldier).c_str(), soldier->attackFixture != nullptr, hitFrames.size());
        grew = grew || (soldier->attackSize.x == 48.0f && soldier->attackSize.y == 24.0f);
        lockedSeen = lockedSeen || behaviour->iDirectionLocked;
    };
    int f = 0;
    for (; f < 600; f++) frame(f);
    Check(targetFrame >= 59 && targetFrame <= 61, "targets the hero a second after (frame " + std::to_string(targetFrame) + ")");
    Check(attackFrame > targetFrame && attackDistance <= 49.0f,
          "chases him and stops to swing within 2 tiles + 1 of his feet (frame " + std::to_string(attackFrame) + ", diamond " +
              std::to_string(attackDistance) + ")");
    Check(soldier->position().x > 30.0f, "it stopped short of him (x " + std::to_string(soldier->position().x) + ")");

    // The swings after the attack began: their events in the clip's order.
    std::vector<std::string> swing;
    for (const auto& e : events)
        if (e.first >= attackFrame) swing.push_back(e.second);
    const char* const kOrder[6] = {"ForbidEnemyMovement", "LockLookDirection", "DoScreenShakeFromEnemyAnimation", "PlayHitOnGroundSound",
                                   "AllowEnemyMovement", "UnLockLookDirection"};
    bool order = swing.size() >= 12;
    for (size_t k = 0; order && k < 12; k++) order = swing[k] == kOrder[k % 6];
    std::string got;
    for (size_t k = 0; k < swing.size() && k < 12; k++) got += " " + swing[k];
    Check(order, "two swings' clip events in order:" + got);
    Check(shakeDraws, "DoScreenShakeFromEnemyAnimation draws 15 (insideUnitSphere 3, the shake 12), PlayHitOnGroundSound 1 (the "
                      "sound's loops), the other events none");
    Check(sounds >= 2 && rumbles >= 2 && vibrations >= 2, "the sword's crash: the sound, the camera's rumble, the pad's");
    Check(fixtureOnlyWhenOn, "the attack fixture exists exactly while the box is on");
    Check(grew, "the box grows to 48 x 24 (AttackLeft's curves)");
    Check(swingsOn >= 3, "the box comes on once per swing (" + std::to_string(swingsOn) + " times)");
    // Each swing's box hits him anew (OnTriggerEnter2D again): a hit within each window after
    // the attack began.
    int swingsHit = 0, swingsAfter = 0, hitsInSwings = 0;
    for (int on : onFrames) {
        if (on < attackFrame) continue;
        swingsAfter++;
        int hits = 0;
        for (int h : hitFrames) hits += h >= on - 1 && h <= on + 16;
        swingsHit += hits > 0;
        hitsInSwings += hits;
    }
    Check(swingsAfter >= 2 && swingsHit == swingsAfter,
          "every swing at him hits him (" + std::to_string(swingsHit) + " of " + std::to_string(swingsAfter) + ", " +
              std::to_string(hitFrames.size()) + " hits in all)");
    // The box is remade as it grows: it enters him once a swing.
    Check(hitsInSwings == swingsAfter, "one hit a swing: " + std::to_string(hitsInSwings) + " in " + std::to_string(swingsAfter));

    // The look is locked from 0.5 s into a swing to 1.0 s: a hero who moves then isn't followed.
    Check(lockedSeen, "LockLookDirection locks the look in a swing");
    game::PolyNav2D::current = nullptr;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string file = argc > 1 ? argv[1] : "data/oracle/enemy_GolemSoldier.jsonl";
    const std::string name = argc > 2 ? argv[2] : "GolemSoldier";
    CheckShakeDraws();
    Data data = LoadData(name);
    if (data.ok) {
        if (name == "GolemSoldier") {
            CheckSoldierInABox(data);
        }
        if (name == "HermitSlime") CheckHermitSlime(*data.prefab, *data.controller, data.enemyStats, data.physics2d, data.soundGroups);
    } else {
        std::printf("(no %s data in %s (ML_DATA; animators/%s.json from tools/extract/animators.py): skipping the soldier in a box)\n",
                    name.c_str(), data.dir.c_str(), name.c_str());
    }
    std::printf("self-checks: %d passed, %d failed\n", gPassed, gFailed);
    ReplayData replay;
    replay.enemyStats = data.enemyStats;
    replay.physics2d = data.physics2d;
    replay.prefab = data.prefab;
    replay.controller = data.controller.get();
    replay.soundGroups = data.soundGroups;
    replay.recordWith = "-oracle-enemy=" + name;
    replay.debugEnv = "ML_ENEMY_DEBUG";
    replay.contactsEnv = "ML_ENEMY_CONTACTS";
    const int replayed = ReplayRecording(file, replay);
    return gFailed || replayed ? 1 : 0;
}

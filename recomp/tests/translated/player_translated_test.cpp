// Will, the hero, on the game's own code (translated), against the walk recordings
// (data/oracle/walk*.jsonl, the oracle's -oracle-walk): player_walk_test's replay and rows
// (tests/player_walk_replay.h: the recording, the room's colliders built and checked as the
// game's, the rows and the pass criterion) with HeroMerchant, HeroMerchantController,
// HeroMerchantStats, the SMAnimatorHandler his Animator's states advise, the Dash ability and its
// SpeedEffect, his weapon (Weapon, Equipment), his action areas and the roll's ghost trail the
// game's own, on the scene facade (recomp/src/bind) in place of the hand port's
// game::HeroMerchantController.
//
// The scene: the room's colliders (one stand-in object each, by name, tag and layer: what
// Physics2D's queries answer with) in the facade's Box2D world, as the hand port's ML_WALK_BOX2D
// steps them; the hero made from his prefab (hero_merchant's HeroMerchant: its objects, collider,
// body, action areas), his Animator on hero_merchant.json with an SMStateAdvisor on every state
// (the controller's state machine behaviours aren't extracted: SMAnimatorHandler's registrations
// name the states), his serialized references the extraction doesn't have put back by path
// (the action areas, the trails, the ability's effect, the ghost's renderer); the equipped weapon
// (the item walkStart names: its art prefab under his Weapon / ItemStack / Logic, its Animator on
// unscaled time, running before his) and the Dash. HeroMerchantStats.Init and
// HeroMerchantController.Init are the game's (the item database, the items' objects and the HUD
// stood in for: recomp/tests/translated/scene/Standins.cpp); his stats, position, Animator, the
// weapon's Animator and the combo are then put back as the first frame has them.
//
// Every later frame as the game ran it: the pad through InControl (the recording's stick into
// LeftStick.UpdateWithAxes, each button's press CommitWithValue'd: where the oracle's hooks feed
// them), the frame's fixed steps (the recording's count), DOTween, Start, the Updates, the delayed
// calls and coroutines, the objects destroyed in the Updates (SpeedEffect's OnDestroy), the
// Animators (the weapon's, then his), the frame's end. Physics2D.OverlapBox's ties (unity/Physics2D.h:
// the broad phase's order isn't modelled) are settled by the recording as the hand port settles
// them: a frame whose velocity differs after one is run again (the whole replay, a new process:
// the translated statics start over) with the other candidates until it matches; counted on the
// velocity row as the hand port counts them.
//
// Usage: player_translated_test walk.jsonl [trace.jsonl]   (ML_DATA: data/extracted; ML_WALK_DEBUG=1
//        every frame; ML_GC_EVERY_FRAME=1 the collector at every frame's end; ML_WALK_TIES: the
//        ties' choices so far, set by the test itself when it runs again)

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <Box2D/Box2D.h>

#include "bind/Prefabs.h"
#include "bind/UnityEngine.h"
#include "game.h"
#include "game/Items.h"
#include "game/Prefabs.h"
#include "scene/Standins.h"
#include "unity/Animator.h"
#include "unity/Physics2D.h"
#include "unity/PhysicsWorld.h"
#include "util/Json.h"

#include "../player_walk_replay.h"

namespace {

using json::Value;
using unity::Vector2;
using UnityEngine::GameObject;
using walkreplay::Bits;
using walkreplay::ReadFile;
using walkreplay::V2;

cs::String* S(const std::string& s) { return cs::String::FromUtf8(s); }
std::string Leaf(const std::string& path)
{
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// ---- the ties' choices (Physics2D.OverlapBox): "frame.call=choice,..." --------------------------------

struct Tie {
    int frame = 0, call = 0, choice = 0;
};
std::vector<Tie> ParseTies(const char* s)
{
    std::vector<Tie> out;
    if (!s) return out;
    std::string text = s;
    size_t at = 0;
    while (at < text.size()) {
        size_t comma = text.find(',', at);
        std::string item = text.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        Tie t;
        if (std::sscanf(item.c_str(), "%d.%d=%d", &t.frame, &t.call, &t.choice) == 3) out.push_back(t);
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    return out;
}
std::string FormatTies(const std::vector<Tie>& ties)
{
    std::string s;
    for (const Tie& t : ties) s += (s.empty() ? "" : ",") + std::to_string(t.frame) + "." + std::to_string(t.call) + "=" + std::to_string(t.choice);
    return s;
}

// This test again, with ML_WALK_TIES set: its exit code (its output its own).
int RunAgain(char** argv, const std::string& ties)
{
    std::fflush(stdout);
#ifdef _WIN32
    _putenv_s("ML_WALK_TIES", ties.c_str());
    std::vector<std::string> quoted;
    for (char** a = argv; *a; a++) quoted.push_back(std::string("\"") + *a + "\"");
    std::vector<const char*> args;
    for (const std::string& q : quoted) args.push_back(q.c_str());
    args.push_back(nullptr);
    return static_cast<int>(_spawnv(_P_WAIT, argv[0], args.data()));
#else
    setenv("ML_WALK_TIES", ties.c_str(), 1);
    const pid_t pid = fork();
    if (pid == 0) {
        execv(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
}

// ---- the pad (InControl) -------------------------------------------------------------------------

// A PlayerAction as InputActionsManager makes one (PlayerAction's constructor: Raw; its owner's
// device and bindings are the pad's business, outside the replay).
gen::InControl::PlayerAction* NewAction()
{
    auto* a = cs::gcnew<gen::InControl::PlayerAction>();
    a->gen::InControl::OneAxisInputControl::__ctor();
    a->Raw = true;
    return a;
}

struct Pad {
    gen::HeroMerchantActions* actions = nullptr;
    gen::GUIActions* gui = nullptr;
    uint64_t tick = 0;
    std::vector<gen::InControl::PlayerAction*> all; // every button, committed each frame

    void Make()
    {
        actions = cs::gcnew<gen::HeroMerchantActions>();
        gen::InControl::PlayerAction** buttons[] = {
            &actions->LeftStickUp, &actions->LeftStickDown, &actions->LeftStickRight, &actions->LeftStickLeft, &actions->RightStickUp,
            &actions->RightStickDown, &actions->RightStickRight, &actions->RightStickLeft, &actions->DPadUp, &actions->DPadDown,
            &actions->DPadRight, &actions->DPadLeft, &actions->Action1, &actions->Action2, &actions->Action3, &actions->Action4,
            &actions->ButtonLeftStick, &actions->ButtonRightStick, &actions->LeftBumper, &actions->RightBumper, &actions->LeftTrigger,
            &actions->RightTrigger, &actions->ButtonSelect, &actions->ButtonStart};
        for (auto** b : buttons) {
            *b = NewAction();
            all.push_back(*b);
        }
        actions->LeftStick = cs::New<gen::InControl::PlayerTwoAxisAction>(actions->LeftStickLeft, actions->LeftStickRight, actions->LeftStickDown,
                                                                           actions->LeftStickUp);
        // The GUI's actions (GUIManager.input): the oracle's hooks fed the hero's only, nothing here is pressed.
        gui = cs::gcnew<gen::GUIActions>();
        gui->Action1 = NewAction();
        gui->ButtonStart = NewAction();
        all.push_back(gui->Action1);
        all.push_back(gui->ButtonStart);
    }

    // A frame's input, as the oracle fed it (Walk.cs: its StickHook gives LeftStick.UpdateWithAxes the
    // stick, its ButtonHook commits each scripted button: pressed on a segment's first frame).
    void Frame(Vector2 stick, int buttons, float deltaTime)
    {
        tick++;
        for (gen::InControl::PlayerAction* a : all) {
            int bit = a == actions->Action1 ? 1 : a == actions->Action2 ? 2 : a == actions->Action3 ? 4 : a == actions->Action4 ? 8
                      : a == actions->LeftTrigger                                                                  ? 16
                                                                                                                   : 0;
            a->CommitWithValue(bit && (buttons & bit) ? 1.0f : 0.0f, tick, deltaTime);
        }
        actions->LeftStick->UpdateWithAxes(stick.x, stick.y, tick, deltaTime);
    }
};

// ---- the scene's MonoBehaviours, as the frame's passes reach them ----------------------------------

void Collect(UnityEngine::Transform* t, std::vector<UnityEngine::MonoBehaviour*>& out)
{
    if (!t->gameObject_->get_activeInHierarchy()) return;
    for (UnityEngine::Component* c : t->gameObject_->components_)
        if (auto* m = dynamic_cast<UnityEngine::MonoBehaviour*>(c)) out.push_back(m);
    for (UnityEngine::Transform* child : t->children_) Collect(child, out);
}

std::vector<UnityEngine::MonoBehaviour*> Behaviours(bind::Scene& scene)
{
    std::vector<UnityEngine::MonoBehaviour*> out;
    const auto roots = scene.roots;
    for (GameObject* g : roots)
        if (!g->destroyed_) Collect(g->transform_, out);
    return out;
}

int Replay(char** argv, const std::string& walkPath, const std::string& tracePath, bool& ranAgain)
{
    const std::string data = std::getenv("ML_DATA") ? std::getenv("ML_DATA") : "data/extracted";
    const bool debug = std::getenv("ML_WALK_DEBUG") != nullptr;
    const bool gcEveryFrame = std::getenv("ML_GC_EVERY_FRAME") != nullptr;
    std::vector<Tie> ties = ParseTies(std::getenv("ML_WALK_TIES"));

    // The game's constants (the game's own reader: Constants, FullSerializer's).
    UnityEngine::Resources::SetTextRoot(data);
    {
        cs::StaticScope keep;
        gen::Constants::ReadFile(S("constants"));
    }

    walkreplay::Recording rec;
    if (int code = rec.Load(walkPath)) return code;
    const Value& start = rec.start;
    const std::vector<Value>& frames = rec.frames;
    walkreplay::Room room;
    if (int code = room.Build(rec, data, tracePath)) return code;

    // ---- the scene: the physics and the room's colliders ----
    bind::Scene scene;
    unity::Physics2DSettings physicsSettings;
    {
        std::string text = ReadFile(data + "/physics2d.json");
        if (!text.empty() && !physicsSettings.Load(json::Parse(text))) walkreplay::Print("%s/physics2d.json: no layer matrix\n", data.c_str());
    }
    unity::PhysicsWorld world(physicsSettings);
    room.AddToWorld(rec, world);
    bind::PhysicsScene physics(world, scene);
    physics.queries = &room.physics;
    // Each room collider's object (its name, tag, layer and position: what the queries and messages
    // answer with; the corner slide measures between their transforms).
    std::map<const unity::Collider2D*, UnityEngine::BoxCollider2D*> objectOf;
    for (const unity::Collider2D& c : room.physics.Colliders()) {
        GameObject* g = GameObject::Create(Leaf(c.path));
        g->transform_->localPosition_ = UnityEngine::Vector3{c.position.x, c.position.y, 0.0f};
        g->transform_->physicsDirty_ = false;
        if (!c.tag.empty()) g->tag_ = cs::String::Intern(S(c.tag));
        g->layer_ = c.layer;
        auto* b = cs::gcnew<UnityEngine::BoxCollider2D>();
        b->isTrigger_ = c.trigger;
        b->enabled_ = c.enabled;
        g->Attach(b);
        objectOf[&c] = b;
        physics.RegisterQuery(&c, b);
    }
    for (b2Body* b = world.World().GetBodyList(); b; b = b->GetNext())
        for (b2Fixture* f = b->GetFixtureList(); f; f = f->GetNext())
            if (const unity::Collider2D* c = world.ColliderOf(f))
                if (objectOf.count(c)) physics.Register(f, objectOf[c]);

    // The ties: the recording's choice where one was made, else OverlapBox's own (the first).
    int frameNumber = -1, callInFrame = 0;
    std::vector<std::pair<int, size_t>> tiesThisFrame; // (call, candidates)
    physics.onOverlapBoxTie = [&](const std::vector<const unity::Collider2D*>& candidates) -> size_t {
        const int call = callInFrame++;
        tiesThisFrame.emplace_back(call, candidates.size());
        for (const Tie& t : ties)
            if (t.frame == frameNumber && t.call == call) return static_cast<size_t>(t.choice);
        return 0;
    };

    // ---- the game's services the hero reaches (Standins.cpp) ----
    standins::S().heroStandIns = false;
    auto* gameManager = cs::gcnew<gen::GameManager>();
    gameManager->currentGameSlot = cs::gcnew<gen::GameSlot>();
    standins::S().gameManager = gameManager;
    // GUIManager (no panel open: currentInventory none) with its HUD (HeroMerchantStats.Init reads its HUDManager).
    auto* gui = cs::gcnew<gen::GUIManager>();
    gen::GUIManager::Instance = gui;
    gui->canOpenPanels = true;
    {
        GameObject* hudGo = GameObject::Create("HUDSPanel");
        auto* hud = cs::gcnew<gen::HUDManager>();
        hudGo->components_.push_back(hud);
        hud->gameObject_ = hudGo;
        hud->pendantShortcutHUD = cs::gcnew<gen::PendantShortcutHUD>();
        gen::HUDManager::Instance = hud;
        gui->HUDSPanel = hud;
    }
    // The pad: InputActionsManager's actions (found by Resources.FindObjectsOfTypeAll), the GUI's.
    Pad pad;
    pad.Make();
    auto* inputManager = cs::gcnew<gen::InputActionsManager>();
    inputManager->actions = pad.actions;
    gui->input = pad.gui;
    // The dungeon's manager and room (the fall's: OnPlayerFallsOnHole, the door he came in by).
    auto* dungeonManager = cs::gcnew<gen::DungeonManager>();
    gen::DungeonManager::_instance = dungeonManager;
    GameObject* dungeonGo = GameObject::Create("Dungeon");
    GameObject* roomGo = GameObject::Create(room.room->name, dungeonGo->transform_);
    roomGo->transform_->localPosition_ = UnityEngine::Vector3{room.position.x, room.position.y, 0.0f};
    auto* dungeonRoom = cs::New<gen::DungeonRoom>();
    roomGo->components_.push_back(dungeonRoom);
    dungeonRoom->gameObject_ = roomGo;
    dungeonRoom->isInit = room.room->name == "Init";
    dungeonRoom->roomDoors = cs::New<cs::List<gen::DungeonDoor*>>();
    dungeonManager->currentDungeonRoom = dungeonRoom;
    if (const Value* r = start.Find("respawn")) {
        // DungeonManager.lastDoorUsed: its playerEnterDestinyRoomPoint where he's put back.
        GameObject* doorGo = GameObject::Create("LastDoorUsed");
        auto* door = cs::gcnew<gen::DungeonDoor>();
        doorGo->components_.push_back(door);
        door->gameObject_ = doorGo;
        GameObject* point = GameObject::Create("PlayerEnterDestinyRoomPoint", doorGo->transform_);
        point->transform_->localPosition_ = UnityEngine::Vector3{V2(*r).x, V2(*r).y, 0.0f};
        door->playerEnterDestinyRoomPoint = point->transform_;
        dungeonManager->lastDoorUsed = door;
    }

    // The item database (items.json) as ItemDatabase.GetItemByName gives the items: the [Serializable]
    // masters with their fields.
    const Value items = json::Parse(ReadFile(data + "/items.json"));
    auto masterOf = [&](const std::string& name) -> gen::ItemMaster* {
        const Value* item = game::Items::Find(items, name);
        if (!item) return nullptr;
        const std::string type = item->Find("$type") && (*item)["$type"].IsString() ? (*item)["$type"].s : "ItemMaster";
        gen::ItemMaster* m = type == "GlovesWeaponMaster"       ? cs::New<gen::GlovesWeaponMaster>()
                             : type == "SpearWeaponMaster"      ? cs::New<gen::SpearWeaponMaster>()
                             : type == "ShortSwordWeaponMaster" ? cs::New<gen::ShortSwordWeaponMaster>()
                             : type == "BigSwordWeaponMaster"   ? cs::New<gen::BigSwordWeaponMaster>()
                             : type == "BowWeaponMaster"        ? cs::New<gen::BowWeaponMaster>()
                             : type == "AbilityItemMaster"      ? cs::New<gen::AbilityItemMaster>()
                             : type == "EquipmentItemMaster"    ? cs::New<gen::EquipmentItemMaster>()
                                                                : cs::New<gen::ItemMaster>();
        if (type != "ItemMaster" && type != "GlovesWeaponMaster" && type != "SpearWeaponMaster" && type != "ShortSwordWeaponMaster" &&
            type != "BigSwordWeaponMaster" && type != "BowWeaponMaster" && type != "AbilityItemMaster" && type != "EquipmentItemMaster")
            cs::NotTranslated(("the item database: an item of type " + type).c_str());
        for (const auto& [field, fv] : item->obj) {
            if (fv.IsString() && (field == "weaponType" || field == "equipmentSlot" || field == "tier" || field == "culture")) continue;
            UnityEngine::JsonSerialValue jv(fv, nullptr);
            m->cs_SetField(field.c_str(), jv);
        }
        // (enums by name, as FullSerializer reads them)
        if (auto* w = dynamic_cast<gen::WeaponEquipmentMaster*>(m)) {
            static const char* const kTypes[6] = {"None", "ShortSword", "BigSword", "Spear", "Gloves", "Bow"};
            for (int i = 0; i < 6; i++)
                if ((*item)["weaponType"].s == kTypes[i]) w->weaponType = static_cast<gen::WeaponEquipmentMaster_WeaponType>(i);
        }
        return m;
    };
    standins::S().itemByName = [&](cs::String* name) -> gen::ItemMaster* { return masterOf(name ? name->ToUtf8() : ""); };
    // ItemStack.Create: the item's object (its UI not made).
    standins::S().createItemStack = [&](gen::ItemMaster* master) -> gen::ItemStack* {
        GameObject* g = GameObject::Create("ItemStack(Clone)");
        auto* stack = cs::New<gen::ItemStack>();
        g->components_.push_back(stack);
        stack->gameObject_ = g;
        stack->master = master;
        auto* pick = cs::New<gen::PickOnWalk>();
        g->components_.push_back(pick);
        pick->gameObject_ = g;
        stack->pickOnWalk = pick;
        return stack;
    };

    // ---- the hero ----
    const game::Prefab* heroPrefab = game::PrefabLibrary::Find("HeroMerchant", "hero_merchant");
    unity::AnimatorController controller = unity::AnimatorController::FromJson(json::Parse(ReadFile(data + "/animators/hero_merchant.json")));
    if (!heroPrefab) {
        walkreplay::Print("no HeroMerchant prefab in %s/prefabs.json\n", data.c_str());
        return 1;
    }
    // His scripts the translation has (the inventory, his sounds, the panels' instancer and the
    // sprites' sorting are the driver's or presentation: listed; the pop-up bubble's
    // SMAnimatorHandler is its Animator's, not extracted: presentation).
    std::vector<std::string> skipped;
    bind::PrefabOptions heroOptions;
    heroOptions.controller = &controller;
    heroOptions.active = false;
    GameObject* heroGo = bind::InstantiatePrefab(*heroPrefab, nullptr, heroOptions, [](GameObject* go, const char* cls) -> UnityEngine::Component* {
        static const char* const kScripts[] = {"HeroMerchantStats", "HeroMerchantController", "HeroMerchant", "SMAnimatorHandler", "ActionArea",
                                                "GhostEffect"};
        if (!std::strcmp(cls, "SMAnimatorHandler") && go->transform_->parent_) return nullptr;
        for (const char* s : kScripts)
            if (!std::strcmp(s, cls)) return gen::cs_AddComponent(go, cls);
        return nullptr;
    }, &skipped);
    heroGo->name_ = S("HeroMerchant");
    auto below = [&](const std::string& path) { return bind::FindBelow(heroGo, path); };
    auto* hero = heroGo->GetComponent<gen::HeroMerchant*>();
    auto* ctrl = heroGo->GetComponent<gen::HeroMerchantController*>();
    auto* stats = heroGo->GetComponent<gen::HeroMerchantStats*>();
    auto* handler = heroGo->GetComponent<gen::SMAnimatorHandler*>();
    auto* animator = heroGo->GetComponent<UnityEngine::Animator*>();
    auto* collider = heroGo->GetComponent<UnityEngine::BoxCollider2D*>();
    auto* body = heroGo->GetComponent<UnityEngine::Rigidbody2D*>();
    if (!hero || !ctrl || !stats || !handler || !animator || !collider || !body) {
        walkreplay::Print("the hero's prefab lacks a component\n");
        return 1;
    }
    // Where he is, his collider and body as the recording has them (the scene's: 13 x 13, its
    // centre of mass the box's).
    const Value& f0 = frames[0];
    heroGo->transform_->localPosition_ = UnityEngine::Vector3{V2(f0["pos"]).x, V2(f0["pos"]).y, 0.0f};
    heroGo->layer_ = f0.Find("layer") ? static_cast<int>(f0["layer"].AsInt()) : 0;
    collider->size_ = V2(start["colliderSize"]);
    collider->offset_ = V2(start["colliderOffset"]);
    if (start.Find("rbMass")) {
        body->def_.mass = Bits(start["rbMass"]);
        body->def_.linearDrag = Bits(start["rbDrag"]);
        body->def_.angularDrag = Bits(start["rbAngularDrag"]);
        body->def_.gravityScale = Bits(start["rbGravityScale"]);
        body->def_.constraints = static_cast<int>(start["rbConstraints"].AsInt());
        body->def_.collisionDetection = start["rbDetection"].s == "Continuous" ? unity::Rigidbody2DDef::Continuous : unity::Rigidbody2DDef::Discrete;
        body->def_.hasCenterOfMass = true;
        body->def_.centerOfMass = V2(start["rbCenterOfMass"]);
    } else {
        body->def_.hasCenterOfMass = true;
        body->def_.centerOfMass = collider->offset_;
    }
    // His Animator runs on unscaled time (it plays his death with the game paused), with an
    // SMStateAdvisor on each state SMAnimatorHandler names (all of them).
    animator->unscaledTime_ = true;
    // (the controller's older format has no sprite paths: its sprite is HeroMerchantArt/Will's)
    animator->spriteTarget_ = below("HeroMerchantArt/Will")->GetComponent<UnityEngine::SpriteRenderer*>();
    handler->names = cs::New<cs::List<cs::String*>>();
    handler->nameHashes = cs::New<cs::List<int32_t>>();
    for (const unity::AnimatorState& st : controller.states) {
        handler->names->Add(S(st.name));
        handler->nameHashes->Add(st.hash);
        auto* advisor = cs::New<gen::SMStateAdvisor>();
        animator->AddBehaviour(st.hash, advisor);
    }
    // The references his prefab's scripts hold (not in the extraction): by their objects.
    ctrl->leftActionArea = below("ActionArea/LeftActionArea");
    ctrl->rightActionArea = below("ActionArea/RightLeftActionArea");
    ctrl->frontActionArea = below("ActionArea/FrontActionArea");
    ctrl->backActionArea = below("ActionArea/BackActionArea");
    auto particles = [&](const std::string& path) {
        GameObject* g = below(path);
        auto* p = cs::gcnew<UnityEngine::ParticleSystem>();
        g->Attach(p);
        return p;
    };
    ctrl->MerchantHeroMerchantTrail = particles("Trail/WillStep (spiral short Timing ok )");
    ctrl->foresHeroMerchantTrail = particles("Trail/WillStep ( Forest )");
    ctrl->abilityEffect = particles("AbilityEffects");
    if (auto* ghost = below("AbilityGhostEffect")->GetComponent<gen::GhostEffect*>())
        ghost->renderTarget = below("HeroMerchantArt/Will")->GetComponent<UnityEngine::SpriteRenderer*>();
    hero->currentMode = gen::HeroMerchant_HeroMerchantMode::DungeonMode;

    // His inventory as his code reads it: the equipped weapon (the item walkStart names) and ability.
    auto* inventory = cs::gcnew<gen::HeroMerchantInventory>();
    inventory->weaponParent = below("Weapon");
    inventory->equippedItems = cs::New<cs::List<gen::ItemStack*>>();
    for (int i = 0; i < static_cast<int>(gen::HeroMerchantInventory_EquipmentSlot::Count); i++) inventory->equippedItems->Add(nullptr);
    hero->heroMerchantInventory = inventory;
    const bool hasWeapon = f0.Find("wstate") != nullptr;
    std::unique_ptr<unity::AnimatorController> weaponController;
    gen::Weapon* weapon = nullptr;
    UnityEngine::Animator* weaponAnimator = nullptr;
    GameObject* weaponArt = nullptr;
    if (hasWeapon) {
        const std::string name = start.Find("weapon") ? start["weapon"].s : std::getenv("ML_WALK_WEAPON") ? std::getenv("ML_WALK_WEAPON") : "";
        const Value* item = game::Items::Find(items, name);
        if (!item) {
            walkreplay::Print("the recording's weapon \"%s\" isn't in items.json (walkStart's weapon, or set ML_WALK_WEAPON)\n", name.c_str());
            return 1;
        }
        const std::string art = (*item)["inGameArtPrefabName"].s;
        std::string file;
        for (char ch : art) file += ch == ' ' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        const game::Prefab* artPrefab = game::PrefabLibrary::Find(art, "items");
        const std::string weaponJson = ReadFile(data + "/animators/" + file + ".json");
        if (!artPrefab || weaponJson.empty()) {
            walkreplay::Print("%s: no prefab \"%s\" or animators/%s.json\n", name.c_str(), art.c_str(), file.c_str());
            return 1;
        }
        weaponController = std::make_unique<unity::AnimatorController>(unity::AnimatorController::FromJson(json::Parse(weaponJson)));
        // Its object: the ItemStack under his Weapon, its art (Equipment.InitializeAnimators) under Logic.
        gen::ItemStack* stack = standins::S().createItemStack(masterOf(name));
        GameObject* stackGo = stack->gameObject_;
        stackGo->transform_->SetParent(below("Weapon")->transform_, false);
        GameObject* logic = GameObject::Create("Logic", stackGo->transform_);
        weapon = cs::New<gen::Weapon>();
        stackGo->Attach(weapon);
        auto* equipment = cs::New<gen::Equipment>();
        stackGo->Attach(equipment);
        stack->_weapon = weapon;
        weapon->weaponMaster = dynamic_cast<gen::WeaponEquipmentMaster*>(stack->master);
        equipment->equipmentMaster = weapon->weaponMaster;
        bind::PrefabOptions artOptions;
        artOptions.controller = weaponController.get();
        weaponArt = bind::InstantiatePrefab(*artPrefab, logic->transform_, artOptions, [](GameObject* go, const char* cls) -> UnityEngine::Component* {
            (void)go, (void)cls; // (its sounds and sprites' sorting: presentation)
            return nullptr;
        });
        weaponAnimator = weaponArt->GetComponent<UnityEngine::Animator*>();
        weaponAnimator->unscaledTime_ = true; // (the weapons' update mode)
        equipment->_inGameArt = weaponArt;
        equipment->_animator = weaponAnimator;
        inventory->equippedItems->set_Item(static_cast<int>(gen::HeroMerchantInventory_EquipmentSlot::Weapon1), stack);
    }
    // The Dash (the ability slot's item: Ability.Init adds it), its cooldown the recording's.
    gen::Ability* ability = nullptr;
    {
        gen::ItemStack* stack = standins::S().createItemStack(masterOf("Dash"));
        stack->gameObject_->transform_->SetParent(below("Ability")->transform_, false);
        ability = cs::New<gen::Ability>();
        stack->gameObject_->Attach(ability);
        stack->gameObject_->AddComponent<gen::Dash*>();
        ability->coolDownTime = start.Find("cooldown") ? Bits(start["cooldown"]) : 0.3f;
        inventory->equippedItems->set_Item(static_cast<int>(gen::HeroMerchantInventory_EquipmentSlot::Ability1), stack);
    }

    // Awake and OnEnable, then the game's Inits (HeroMerchant.Init's: the stats', the controller's).
    heroGo->SetActive(true);
    standins::S().townSceneLoaded = f0.Find("town") && f0["town"].b;
    stats->Init();
    using Obscured = CodeStage::AntiCheat::ObscuredTypes::ObscuredFloat;
    auto setStat = [](gen::Stat* s, float value) { s->_value = Obscured::op_Implicit(value); };
    // His stats as the recording has them (his equipment's: the save's).
    setStat(stats->speed, Bits(start.Find("statSpeed") ? start["statSpeed"] : start["moveSpeed"]));
    if (const Value* m = start.Find("maxHealth")) {
        stats->currentHealth->_maxValue = Obscured::op_Implicit(Bits(*m));
        setStat(stats->currentHealth, Bits(start["health"]));
        setStat(stats->defense, Bits(start["defense"]));
        setStat(stats->strength, Bits(start["strength"]));
        if (const Value* a = start.Find("absorption")) {
            stats->absorption->_maxValue = Obscured::op_Implicit(Bits(start["absorptionMax"]));
            setStat(stats->absorption, Bits(*a));
            setStat(stats->shield, Bits(start["shield"]));
        }
    }
    hero->heroMerchantStats = stats;
    hero->heroMerchantController = ctrl;
    ctrl->Init();
    if (weapon) weapon->OnEquipped();
    ability->OnEquipped();
    // The health bar's (WillHealthBar's) subscriptions after the controller's: what it does to him (the
    // damaged effect) as it does it (the HUD itself stood in for).
    standins::HealthBar::Subscribe(stats);
    scene.RunStarts();

    // From the first frame as recorded.
    ctrl->currentPlayerMoveSpeed = Bits(start["moveSpeed"]);
    ctrl->speedFactor = Bits(start["speedFactor"]);
    ctrl->velX = Bits(f0["vel"][0]);
    ctrl->velY = Bits(f0["vel"][1]);
    // His move input as the floor's start left it (the roll out of the door he came in by ended
    // with EnableMoveInput: each axis on), the recording's flags where it has them.
    ctrl->_isMoveInputEnabled = f0.Find("moveInput") ? f0["moveInput"].b : true;
    ctrl->_isHorizontalMoveInputEnabled = true;
    ctrl->_isVerticalMoveInputEnabled = true;
    if (const Value* cm = f0.Find("canMove")) ctrl->_canMove = cm->b;
    ctrl->SetDirection(static_cast<gen::HeroMerchantController_Direction>(f0["dir"].AsInt()));
    const int s0 = controller.StateByHash(static_cast<int32_t>(f0["state"].AsInt()));
    if (s0 < 0) {
        walkreplay::Print("frame 0: state %lld isn't in the controller\n", static_cast<long long>(f0["state"].AsInt()));
        return 1;
    }
    animator->Mecanim()->Play(controller.states[static_cast<size_t>(s0)].name, Bits(f0["t"]));
    animator->SetBool(S("Walk"), controller.states[static_cast<size_t>(s0)].tag == "Walk");
    if (const Value* art = f0.Find("art")) ctrl->heroMerchantArt->SetActive(art->b);
    if (weaponAnimator) {
        const int w0 = weaponController->StateByHash(static_cast<int32_t>(f0["wstate"].AsInt()));
        if (w0 < 0) {
            walkreplay::Print("frame 0: weapon state %lld isn't in the weapon's controller\n", static_cast<long long>(f0["wstate"].AsInt()));
            return 1;
        }
        weaponAnimator->Mecanim()->Play(weaponController->states[static_cast<size_t>(w0)].name, Bits(f0["wt"]));
        ctrl->set_comboCounter(static_cast<int32_t>(f0["combo"].AsInt()));
        // The weapon's Animator runs before his (a child's): what his state callbacks set on it
        // takes effect the next frame.
        auto& list = scene.animators;
        list.erase(std::remove(list.begin(), list.end(), weaponAnimator), list.end());
        list.insert(std::find(list.begin(), list.end(), animator), weaponAnimator);
    }
    physics.Settle();

    // ---- the frames ----
    const float fixedDt = Bits(start["fixedDeltaTime"]);
    UnityEngine::Time::fixedDeltaTime = fixedDt;
    unity::DelayedCalls& calls = scene.calls;
    double time = 0.0, fixedClock = 0.0;
    walkreplay::Rows rows(controller, weaponController.get(), true, debug);
    int tiesSettled = 0;
    std::string tieFrames;
    for (const Tie& t : ties)
        if (t.choice != 0) {
            tiesSettled++;
            if (tiesSettled <= 8) tieFrames += (tieFrames.empty() ? "" : " ") + std::to_string(t.frame);
        }
    for (size_t i = 1; i < frames.size(); i++) {
        const Value& f = frames[i];
        const float dt = Bits(f["dt"]);
        const float udt = f.Find("udt") ? Bits(f["udt"]) : dt;
        frameNumber = static_cast<int>(f["f"].AsInt());
        callInFrame = 0;
        tiesThisFrame.clear();
        scene.BeginFrame();
        if (const Value* town = f.Find("town")) standins::S().townSceneLoaded = town->b;
        // The pad (InControl's update, before the scripts).
        pad.Frame(V2(f["in"]), static_cast<int>(f["in"][2].AsInt()), dt);
        // The fixed steps, as many as the game took.
        const size_t steps = f.Find("fixed") ? f["fixed"].size() : 0;
        for (size_t k = 0; k < steps; k++) {
            fixedClock += static_cast<double>(fixedDt);
            calls.now = time;
            calls.fixedNow = fixedClock;
            calls.inFixedStep = true;
            UnityEngine::Time::deltaTime = fixedDt;
            UnityEngine::Time::time = static_cast<float>(fixedClock);
            scene.RunStarts();
            for (UnityEngine::MonoBehaviour* m : Behaviours(scene))
                if (m->get_isActiveAndEnabled()) m->cs_FixedUpdate();
            physics.Step(fixedDt);
            calls.UpdateFixed(fixedClock);
            scene.ResumeFixed();
            calls.inFixedStep = false;
        }
        time += static_cast<double>(dt);
        calls.now = time;
        UnityEngine::Time::deltaTime = dt;
        UnityEngine::Time::unscaledDeltaTime = udt;
        UnityEngine::Time::time = static_cast<float>(time);
        // DOTween, Start, the Updates, the delayed calls and coroutines.
        scene.tweens.Update(dt, udt);
        scene.RunStarts();
        for (UnityEngine::MonoBehaviour* m : Behaviours(scene))
            if (m->get_isActiveAndEnabled() && !m->destroyed_) m->cs_Update();
        calls.Update(time);
        scene.ResumeYieldNull();
        // What the Updates destroyed goes before the Animators (a SpeedEffect's OnDestroy gives
        // the speed back: the hand port's restore right after the scripts' Update).
        scene.ProcessDestroys();
        scene.UpdateAnimators(dt, udt);
        for (UnityEngine::MonoBehaviour* m : Behaviours(scene))
            if (m->get_isActiveAndEnabled() && !m->destroyed_) m->cs_LateUpdate();
        // (the oracle's recorder read him in its LateUpdate: the frame's end comes after the rows)

        // The tie choices: a frame whose velocity the recording has otherwise, after ties, is run
        // again with the next choice there.
        const float wantVel[2] = {Bits(f["vel"][0]), Bits(f["vel"][1])};
        if ((wantVel[0] != ctrl->velX || wantVel[1] != ctrl->velY) && !tiesThisFrame.empty()) {
            // The next combination of this frame's ties (mixed radix over their candidates).
            std::vector<Tie> next;
            for (const Tie& t : ties)
                if (t.frame != frameNumber) next.push_back(t);
            bool carried = true;
            for (const auto& [call, count] : tiesThisFrame) {
                int choice = 0;
                for (const Tie& t : ties)
                    if (t.frame == frameNumber && t.call == call) choice = t.choice;
                if (carried) {
                    choice++;
                    carried = choice >= static_cast<int>(count);
                    if (carried) choice = 0;
                }
                if (choice != 0) next.push_back({frameNumber, call, choice});
            }
            if (!carried) {
                ranAgain = true;
                return RunAgain(argv, FormatTies(next));
            }
            // (every combination tried: the frame is the rows' to report)
        }

        walkreplay::HeroFrame h;
        const UnityEngine::Vector3 p = heroGo->transform_->get_position();
        h.position = {p.x, p.y};
        h.velX = ctrl->velX;
        h.velY = ctrl->velY;
        h.direction = static_cast<int>(ctrl->heroDirection);
        h.info = animator->Mecanim()->GetCurrentAnimatorStateInfo();
        h.stateName = controller.states[static_cast<size_t>(animator->Mecanim()->CurrentState())].name;
        auto* willRenderer = below("HeroMerchantArt/Will")->GetComponent<UnityEngine::SpriteRenderer*>();
        std::string heroSprite = willRenderer && willRenderer->sprite_ ? willRenderer->sprite_->spriteName : std::string();
        h.sprite = willRenderer && willRenderer->sprite_ ? &heroSprite : nullptr;
        h.isIntangible = ctrl->isIntangible;
        h.animSpeed = animator->get_speed();
        h.abilityInUse = ability->isInUse;
        h.speedFactor = ctrl->speedFactor;
        h.isFalling = ctrl->isFalling;
        h.health = Obscured::op_Implicit(stats->currentHealth->_value);
        h.absorption = Obscured::op_Implicit(stats->absorption->_value);
        h.shield = Obscured::op_Implicit(stats->shield->_value);
        h.healthVariable = CodeStage::AntiCheat::ObscuredTypes::ObscuredBool::op_Implicit(stats->currentHealth->variableStat);
        h.artVisible = ctrl->heroMerchantArt->activeSelf_;
        h.playingDamagedEffect = ctrl->playingDamagedEffect;
        h.isHolesIntangible = ctrl->isHolesIntangible;
        h.layer = heroGo->layer_;
        h.hasBody = body->body_ != nullptr;
        if (h.hasBody) {
            h.touching = body->body_->TouchingCount() > 0;
            h.bodyPosition = body->body_->position();
            h.bodyVelocity = body->body_->velocity();
        }
        std::string weaponSprite;
        if (weaponAnimator) {
            h.combo = ctrl->get_comboCounter();
            h.weaponInfo = weaponAnimator->Mecanim()->GetCurrentAnimatorStateInfo();
            h.weaponStateName = weaponController->states[static_cast<size_t>(weaponAnimator->Mecanim()->CurrentState())].name;
            GameObject* artGo = bind::FindBelow(weaponArt, "MainAttack/Art");
            auto* wr = artGo ? artGo->GetComponent<UnityEngine::SpriteRenderer*>() : nullptr;
            if (wr && wr->sprite_) {
                weaponSprite = wr->sprite_->spriteName;
                h.weaponSprite = &weaponSprite;
            }
            GameObject* hitGo = bind::FindBelow(weaponArt, "MainAttack/Collider");
            auto* hit = hitGo ? hitGo->GetComponent<UnityEngine::BoxCollider2D*>() : nullptr;
            h.hitboxActive = hitGo && hitGo->get_activeInHierarchy() && hit && hit->enabled_;
            if (hit) {
                h.hitboxOffset = hit->offset_;
                h.hitboxSize = hit->size_;
            }
        }
        rows.Frame(f, h);
        // The frame's end: what the Animators' events destroyed (the roll's SpeedEffect: its speed
        // back after the LateUpdates), Destroy's delayed pass.
        scene.ProcessDestroys();
        calls.EndOfFrame();
        if (gcEveryFrame) cs::CurrentHeap().Collect();
    }
    if (debug && !skipped.empty()) {
        walkreplay::Print("\n(his prefab's scripts not made: presentation or the driver's:");
        for (const std::string& s : skipped) walkreplay::Print(" %s", s.c_str());
        walkreplay::Print(")\n");
    }
    return rows.Report(room, tiesSettled, tieFrames) ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: player_translated_test walk.jsonl [trace.jsonl]\n");
        return 2;
    }
    const std::string walkPath = argv[1];
    const std::string tracePath = argc > 2 ? argv[2] : walkPath.substr(0, walkPath.find_last_of("/\\") + 1) + "trace.jsonl";
    // (the lines printed at the end: a run that runs itself again for a tie prints nothing of its own)
    std::string out;
    walkreplay::PrintBuffer() = &out;
    int code = 1;
    bool ranAgain = false;
    try {
        code = Replay(argv, walkPath, tracePath, ranAgain);
    } catch (const cs::Thrown& e) {
        out += std::string("C# exception: ") + (e.ex ? e.ex->ToString()->ToUtf8() : std::string("?")) + "\n";
        code = 1;
    }
    if (!ranAgain) std::fputs(out.c_str(), stdout);
    return code;
}

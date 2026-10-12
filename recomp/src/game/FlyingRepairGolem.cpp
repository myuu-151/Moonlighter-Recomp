#include "game/FlyingRepairGolem.h"

#include <cmath>

#include "game/MasterAudio.h"
#include "game/PolyNav.h"
#include "unity/Mathf.h"
#include "unity/Random.h"

namespace game {

using unity::Vector2;
using unity::mono::D;
using unity::mono::F;

namespace {

// LayerMask.NameToLayer("LevelBoundary").
constexpr int kLayerLevelBoundary = 11;

// Vector3 operators (each component one operation, rounded to a float as the constructor's
// argument).
GolemVector3 Add(const GolemVector3& a, const GolemVector3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
GolemVector3 Sub(const GolemVector3& a, const GolemVector3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
GolemVector3 Mul(const GolemVector3& a, float d) { return {a.x * d, a.y * d, a.z * d}; }

// Vector3.normalized: Vector3.Normalize(this): Magnitude ((float)Math.Sqrt(x x + y y + z z), the
// sum in double), above 1e-05 the vector divided by it, else zero.
GolemVector3 Normalized(const GolemVector3& v)
{
    const float num = F(std::sqrt(D(v.x) * D(v.x) + D(v.y) * D(v.y) + D(v.z) * D(v.z)));
    if (num > 1e-05f) return {v.x / num, v.y / num, v.z / num};
    return {};
}

// (Vector2)vector3: x and y.
Vector2 Xy(const GolemVector3& v) { return {v.x, v.y}; }

// float * Vector2 (Vector2.op_Multiply(float, Vector2)).
Vector2 Mul(float d, Vector2 a) { return {a.x * d, a.y * d}; }

} // namespace

FlyingRepairGolemBehaviour::~FlyingRepairGolemBehaviour()
{
    // (The C++ object goes: the room's delegate forgets it.)
    if (RoomDrops* room = enemy.scene.drops)
        while (room->Subscriptions(this) > 0) room->Unsubscribe(this);
}

GolemVector3 FlyingRepairGolemBehaviour::Position3() const
{
    const Vector2 p = enemy.position();
    return {p.x, p.y, transformZ};
}

GolemVector3 FlyingRepairGolemBehaviour::Hero3() const
{
    const Vector2 h = HeroPosition();
    return {h.x, h.y, heroZ};
}

void FlyingRepairGolemBehaviour::Init()
{
    EnemyBehaviour::Init();
    stolenItems.clear(); // new List<ItemStack>() (body: the Enemy's)
}

void FlyingRepairGolemBehaviour::Update(float deltaTime)
{
    if (beforeUpdate) beforeUpdate(*this);
    EnemyBehaviour::Update(deltaTime);
}

void FlyingRepairGolemBehaviour::OnDeath()
{
    EnemyBehaviour::OnDeath();
    if (RoomDrops* room = Room()) room->Unsubscribe(this);
    DropAllStolenItems();
    if (registerDeath) registerDeath();
}

void FlyingRepairGolemBehaviour::StartTargetingPlayerAfterTime(float time)
{
    EnemyBehaviour::StartTargetingPlayerAfterTime(time);
    if (RoomDrops* room = Room()) room->Subscribe(this, [this](RoomDrops::List& list) { OnItemDroppedInRoom(list); });
}

void FlyingRepairGolemBehaviour::CancelPlayerTargetingAfterTime()
{
    EnemyBehaviour::CancelPlayerTargetingAfterTime();
    if (RoomDrops* room = Room()) room->Unsubscribe(this);
}

void FlyingRepairGolemBehaviour::ChooseAndApplyAction(float deltaTime)
{
    if (canMove) MoveEnemy(deltaTime);
}

void FlyingRepairGolemBehaviour::MoveEnemy(float deltaTime)
{
    if (stolenItems.empty()) {
        if (Alive(targetItem) && targetItem->isActiveAndEnabled()) ChaseItem(deltaTime);
        else if (canMove) ChasePlayer();
        return;
    }
    MoveRandomly();
    if (patrolLapse > 0.0f) {
        patrolLapse -= deltaTime;
        return;
    }
    patrolLapse = unity::Random::Range(0.5f, 1.0f);
    ChooseRandomPoint();
}

void FlyingRepairGolemBehaviour::EnemyWasHit(float newHealthValue)
{
    wasHit = true;
    EnemyBehaviour::EnemyWasHit(newHealthValue);
}

void FlyingRepairGolemBehaviour::MoveRandomly()
{
    if (!enemy.body) return;
    const float num = 0.85f;
    // (body.velocity.normalized * num + (1f - num) * (Vector2)moveDirection.normalized)
    //     .normalized * speed: each operator's result stored; 1 - 0.85f is exact.
    const Vector2 a = unity::Normalized(enemy.body->velocity()) * num;
    const Vector2 b = Mul(F(1.0 - D(num)), Xy(Normalized(moveDirection)));
    const Vector2 v = unity::Normalized(a + b) * enemy.enemyStats.speed();
    enemy.body->SetVelocity(v);
}

void FlyingRepairGolemBehaviour::ChooseRandomPoint()
{
    enemyState = EnemyState::RunningAway;
    lastPoints.clear();
    int num = 0;
    // transform.position + new Vector3(circle.x, circle.y, transform.position.z) * Random.Range(40, 90)
    // (the circle drawn first, then the int range, made a float).
    auto point = [this] {
        const Vector2 c = unity::Random::insideUnitCircle();
        const GolemVector3 p = Position3();
        const float k = static_cast<float>(unity::Random::Range(40, 90));
        const GolemVector3 v = Add(p, Mul(GolemVector3{c.x, c.y, p.z}, k));
        lastPoints.push_back(v);
        return v;
    };
    GolemVector3 vector = point();
    while (!CheckIfDestinationPointIsReachable(vector) && num < 6) {
        num++;
        vector = point();
    }
    moveDirection = Sub(vector, Position3());
}

bool FlyingRepairGolemBehaviour::CheckIfDestinationPointIsReachable(const GolemVector3& point) const
{
    // PolyNav2D.current.PointIsValid((Vector2)point). (No map: the game would throw; here any
    // point is taken.)
    PolyNav2D* nav = PolyNav2D::current;
    return nav == nullptr || nav->PointIsValid(Xy(point));
}

void FlyingRepairGolemBehaviour::ChasePlayer()
{
    if (!enemy.body) return;
    if (wasHit && unity::Magnitude(enemy.body->velocity()) == 0.0f) {
        (void)unity::Random::insideUnitCircle();
        const GolemVector3 d = Sub(Hero3(), Position3());
        enemy.body->SetVelocity(Vector2{-d.x, -d.y});
        wasHit = false;
    }
    enemyState = EnemyState::PursuingPlayer;
    const Vector2 p = enemy.position();
    myPos = Vector2{p.x, p.y};
    moveDirection = Sub(Hero3(), Position3());
    // (body.velocity.normalized * velocityPercent + (1f - velocityPercent) *
    //     (Vector2)moveDirection.normalized).normalized * speed * 1.6f
    const Vector2 a = unity::Normalized(enemy.body->velocity()) * velocityPercent;
    const Vector2 b = Mul(F(1.0 - D(velocityPercent)), Xy(Normalized(moveDirection)));
    const Vector2 v = (unity::Normalized(a + b) * enemy.enemyStats.speed()) * 1.6f;
    enemy.body->SetVelocity(v);
}

void FlyingRepairGolemBehaviour::ChaseItem(float deltaTime)
{
    const Vector2 vector = targetItem->position; // (Vector2)targetItem.transform.position
    const Vector2 p = enemy.position();
    myPos = Vector2{p.x, p.y};
    const Vector2 d = vector - myPos;
    moveDirection = GolemVector3{d.x, d.y, 0.0f};
    // transform.Translate(moveDirection.normalized * speed * Time.deltaTime): Space.Self, the
    // translation through TransformDirection (no rotation: itself), added to the position.
    const GolemVector3 t = Mul(Mul(Normalized(moveDirection), enemy.enemyStats.speed()), deltaTime);
    const GolemVector3 to = Add(Position3(), t);
    enemy.setPosition(Vector2{to.x, to.y});
    transformZ = to.z;
}

void FlyingRepairGolemBehaviour::OnItemDroppedInRoom(RoomDrops::List& objectsInRoom)
{
    dropListCalls++;
    if (!enemy.enemyStats.isAlive()) return;
    // objectsInRoom.GetRandom(): default with none, else list[Random.Range(0, Count)].
    targetItem = objectsInRoom.empty() ? nullptr : objectsInRoom[static_cast<size_t>(unity::Random::Range(0, static_cast<int32_t>(objectsInRoom.size())))];
    itemsDroppedInRoom = &objectsInRoom;
}

void FlyingRepairGolemBehaviour::OnCollisionEnter2D(int otherLayer, bool hasContact, Vector2 normal)
{
    // collision.gameObject.layer == 1 << LayerMask.NameToLayer("LevelBoundary"): a layer index
    // against a mask (2048): never.
    if (otherLayer != (1 << kLayerLevelBoundary)) return;
    if (enemy.body) enemy.body->SetVelocity(Vector2::zero());
    if (hasContact) {
        moveDirection.x = 0.0f - normal.y;
        moveDirection.y = normal.x;
    }
}

void FlyingRepairGolemBehaviour::OnTriggerEnter2D(const std::string& otherTag, const DroppedItem* otherItem)
{
    // collision.gameObject.CompareTag("Item") && targetItem != null && targetItem.gameObject ==
    // collision.gameObject.
    if (otherTag == "Item" && Alive(targetItem) && targetItem == otherItem) SetAnimatorTrigger("PickItem", true);
}

void FlyingRepairGolemBehaviour::PickItemCollisionedAndRemoveItFromRoom()
{
    if (!Alive(targetItem)) return;
    stolenItems.push_back(targetItem);
    itemSprite = targetItem->sprite;
    targetItem->active = false;  // gameObject.SetActive(false)
    targetItem->parent = &enemy; // transform.parent = transform
    DroppedItem* item = targetItem;
    targetItem = nullptr;
    if (RoomDrops* room = Room()) room->RemoveDropItem(item);
}

void FlyingRepairGolemBehaviour::HealthDecreasedWhenHit(float newValue)
{
    EnemyBehaviour::HealthDecreasedWhenHit(newValue);
    if (enemy.body) enemy.body->SetVelocity(Vector2::zero()); // body.velocity = Vector3.zero
}

void FlyingRepairGolemBehaviour::DropAllStolenItems()
{
    if (!stolenItems.empty()) {
        RoomDrops* room = enemy.scene.drops;
        for (DroppedItem* stolenItem : stolenItems) {
            if (!Alive(stolenItem)) continue;
            stolenItem->active = true; // gameObject.SetActive(true)
            // stolenItem.Drop(transform.position, 30, the current room's transform) (no room:
            // Drop(transform.position), 30 its default).
            if (room && room->drop) room->drop(*stolenItem, enemy.position(), 30.0f, true);
        }
        stolenItems.clear();
    }
    itemSprite.clear(); // itemSprite.sprite = null
}

void FlyingRepairGolemBehaviour::PlayFlyingRepairPick()
{
    static const char* const kSound = "golem_dungeon_flyingrepair_pick";
    if (enemy.scene.audio) enemy.scene.audio->PlaySound(kSound);
    if (enemy.scene.playSound) enemy.scene.playSound(enemy, kSound);
}

bool FlyingRepairGolemBehaviour::OnAnimationEvent(const unity::AnimationEvent& e)
{
    const std::string& f = e.function;
    if (f == "PickItemCollisionedAndRemoveItFromRoom") PickItemCollisionedAndRemoveItFromRoom();
    else if (f == "PlayFlyingRepairPick") PlayFlyingRepairPick(); // (SoundFlyingRepairPick's)
    else if (f == "DropAllStolenItems") DropAllStolenItems();
    else return EnemyBehaviour::OnAnimationEvent(e);
    return true;
}

std::unique_ptr<FlyingRepairGolemBehaviour> MakeFlyingRepairGolemBehaviour(Enemy& enemy, const Behaviour& fields)
{
    auto golem = std::make_unique<FlyingRepairGolemBehaviour>(enemy);
    const auto it = fields.raw.find("itemSprite");
    golem->hasItemSprite = it == fields.raw.end() || (it->second.IsString() && !it->second.s.empty());
    return golem;
}

} // namespace game

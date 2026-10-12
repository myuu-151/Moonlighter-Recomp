#pragma once
// EnemyStats.cs: an enemy's stats and health. The base values are EnemyStatManager's, read
// from the "enemy_stats" TextAsset (data/extracted/enemy_stats.json: one EnemyStatModificator
// per enemy name, FullSerializer filling a default-constructed one, so a missing
// isModifiedByFloor stays true). Init scales them as the game does for the floor (level,
// night), the difficulty option, New Game+ and the dungeon/enemy culture tables.
//
// CurrentHealth's setter is where hits land: nothing while invincible or dead, else
// OnHealthChanged, OnHealthDecreased (when it goes down: EnemyBehaviour.EnemyWasHit, which
// reads the health *before* the change), the new value (at least 0), and at 0 OnDeath then
// Die (the room's EnemyDefeated, through OnDie).

#include <functional>
#include <string>

#include "util/Json.h"

namespace game {

// EnemyStatModificator: the per-enemy values of enemy_stats.json.
struct EnemyStatModificator {
    bool isModifiedByFloor = true;
    float size = 0.0f;
    float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float maxHealth = 0.0f, speed = 0.0f, strength = 0.0f, defense = 0.0f, weight = 0.0f, shieldPushForce = 0.0f;
    int culture = 0; // ItemMaster.Culture (0 Merchant, 1 Golem, 2 Forest, 3 Desert, 4 Tech, ... 7 Wanderer)

    // enemy_stats.json's entry for `name` (EnemyStatManager.GetModificator); false if none.
    static bool FromJson(const json::Value& enemyStats, const std::string& name, EnemyStatModificator& out);
};

// What EnemyStats.Init reads from the managers.
struct EnemyStatsContext {
    bool hasDungeon = true;       // DungeonManager.Instance
    int level = 0;                // DungeonManager.level (0 on the first floor)
    bool afternoon = false;       // TimeManagerV2: DayTime.Afternoon (the night's leveling value)
    int difficulty = -1;          // OptionsManager "GAME" "Difficulty" (-1: unset)
    bool newGamePlus = false;     // GameSlot.gameType NewGamePlus...
    bool dlcCompleted = false;    // ... and wasDlcCompleted
    int dungeonCulture = 1;       // the autoscale tables' row: the dungeon's culture (intermediate: its culture)
};

class EnemyStats {
public:
    bool isChampion = false;
    bool isSpeedModifiedByFloor = false; // (a serialized field of the component; false unless a prefab sets it)

    // Init(name): the base stats scaled; CurrentHealth = maxHealth. `rigidbodyMass` gets the
    // weight (the Rigidbody2D's mass), if given.
    void Init(const EnemyStatModificator& base, const EnemyStatsContext& context, float* rigidbodyMass = nullptr);

    float size() const { return current_.size; }
    float maxHealth() const { return current_.maxHealth; }
    float strength() const { return current_.strength; }
    float speed() const { return current_.speed; }
    float weight() const { return current_.weight; }
    float defense() const { return current_.defense; }
    float shieldPushForce() const { return current_.shieldPushForce; }
    int culture() const { return current_.culture; }
    const float* tint() const { return current_.tint; }
    bool isAlive() const { return isAlive_; }

    float CurrentHealth() const { return currentHealth_; }
    void SetCurrentHealth(float value);
    bool Invencible() const { return invincible_; }
    void SetInvencible(bool value);
    void Resurrect();

    std::function<void(float)> OnHealthChanged, OnHealthDecreased;
    std::function<void()> OnDeath;
    // Die(): achievements (none here) and MarkEnemyAsDefeated (the room's EnemyDefeated).
    std::function<void()> OnDie;

    // Overwrites the current stats (a replay starting from a recorded enemy).
    void Restore(const EnemyStatModificator& current, float currentHealth, bool invincible, bool alive)
    {
        current_ = current;
        currentHealth_ = currentHealth;
        invincible_ = invincible;
        isAlive_ = alive;
    }
    const EnemyStatModificator& current() const { return current_; }

private:
    EnemyStatModificator base_, current_;
    bool isAlive_ = true;
    float currentHealth_ = 0.0f;
    bool invincible_ = false;
};

} // namespace game

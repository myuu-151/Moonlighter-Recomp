#pragma once
// PoolManager.cs (DungeonManager.Instance.poolManager): named pools of objects made from a
// model, kept for the whole floor. The game pools GameObjects under an inactive parent
// ("[POOLS PARENT]/[POOL] - <name>"): a pooled object is inactive in the hierarchy until a
// caller takes it and parents it somewhere active (the enemies' projectiles go under their
// room), and ReturnObject parents it back (inactive again).
//
//   - CreateOrFillGameObjectPool(name, model, n): a new pool of n, or n more in the pool of
//     that name (the enemies' Init: BrokenGolemTurretBehaviour's
//     `enemy.name + "_" + projectilePrefab.name`, 6 each, so every turret of the floor adds
//     6 to the one pool their shots share);
//   - GetObject(name): the pool's first (List.RemoveAt(0)); an empty pool instantiates one
//     more first;
//   - ReturnObject(obj): back to the end of the pool its name says (if it isn't there
//     already), deactivated. A name with no pool makes one with the object as its model.
//
// The objects are the pool manager's (it owns what it made); PooledObject is what a pool
// holds: the object's name (GameObject.name, the pool's item name) and its activation.

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace game {

class PooledObject {
public:
    virtual ~PooledObject() = default;
    std::string name; // GameObject.name: the pool's item name (FillPool sets it)
    int instance = 0; // the order the pool manager made it in (0 first)
    // Parented back under the inactive pool (ReturnObject): inactive in the hierarchy.
    virtual void OnReturnedToPool() {}
};

class PoolManager {
public:
    // Instantiate(model, pool parent): a new object (inactive: the pool's parent is).
    using Maker = std::function<std::unique_ptr<PooledObject>()>;

    struct Pool {
        std::string itemName;
        Maker model;                            // none: a pool made by ReturnObject without a model
        std::vector<PooledObject*> pooledObjects; // the List, first out first
    };

    // CreateOrFillGameObjectPool(name, model, initialAmount, limitedInitialAmount).
    Pool& CreateOrFillGameObjectPool(const std::string& name, const Maker& model, int initialAmount, bool limitedInitialAmount);
    Pool& CreateOrFillGameObjectPool(const std::string& name, const Maker& model, int initialAmount)
    {
        return CreateOrFillGameObjectPool(name, model, initialAmount, false);
    }
    // GetObject(name, instantiateIfRequired): nullptr if there's no such pool (or it's empty
    // and mustn't instantiate).
    PooledObject* GetObject(const std::string& name, bool instantiateIfRequired);
    PooledObject* GetObject(const std::string& name) { return GetObject(name, true); }
    // ReturnObject(go): to the pool of its name.
    void ReturnObject(PooledObject* object);

    Pool* Find(const std::string& name);
    const Pool* Find(const std::string& name) const;
    // Every object the pools made, in the order made.
    const std::vector<std::unique_ptr<PooledObject>>& Objects() const { return objects_; }
    // FillPool(amount) of a pool.
    void FillPool(Pool& pool, int amount);

private:
    std::vector<std::unique_ptr<Pool>> pools_; // (Dictionary by name; stable addresses)
    std::vector<std::unique_ptr<PooledObject>> objects_;
};

} // namespace game

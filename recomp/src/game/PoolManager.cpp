#include "game/PoolManager.h"

#include <algorithm>

namespace game {

PoolManager::Pool* PoolManager::Find(const std::string& name)
{
    for (auto& p : pools_)
        if (p->itemName == name) return p.get();
    return nullptr;
}

const PoolManager::Pool* PoolManager::Find(const std::string& name) const
{
    for (const auto& p : pools_)
        if (p->itemName == name) return p.get();
    return nullptr;
}

void PoolManager::FillPool(Pool& pool, int amount)
{
    if (!pool.model) return; // (modelObject null)
    for (int i = 0; i < amount; i++) {
        std::unique_ptr<PooledObject> made = pool.model();
        made->name = pool.itemName;
        made->instance = static_cast<int>(objects_.size());
        pool.pooledObjects.push_back(made.get());
        objects_.push_back(std::move(made));
    }
}

PoolManager::Pool& PoolManager::CreateOrFillGameObjectPool(const std::string& name, const Maker& model, int initialAmount,
                                                           bool limitedInitialAmount)
{
    if (Pool* pool = Find(name)) {
        if (!limitedInitialAmount) FillPool(*pool, initialAmount);
        else if (initialAmount > static_cast<int>(pool->pooledObjects.size()))
            FillPool(*pool, initialAmount - static_cast<int>(pool->pooledObjects.size()));
        return *pool;
    }
    pools_.push_back(std::make_unique<Pool>());
    Pool& pool = *pools_.back();
    pool.itemName = name;
    pool.model = model;
    FillPool(pool, initialAmount);
    return pool;
}

PooledObject* PoolManager::GetObject(const std::string& name, bool instantiateIfRequired)
{
    Pool* pool = Find(name);
    if (!pool) return nullptr;
    if (pool->pooledObjects.empty() && instantiateIfRequired) FillPool(*pool, 1);
    if (pool->pooledObjects.empty()) return nullptr;
    PooledObject* result = pool->pooledObjects.front();
    pool->pooledObjects.erase(pool->pooledObjects.begin());
    return result;
}

void PoolManager::ReturnObject(PooledObject* object)
{
    if (!object) return;
    Pool* pool = Find(object->name);
    if (!pool) {
        // CreateGameObjectPool(go.name, go): a pool with the object as its model (the port
        // can't copy an object: one made this way never instantiates).
        pools_.push_back(std::make_unique<Pool>());
        pool = pools_.back().get();
        pool->itemName = object->name;
    }
    if (std::find(pool->pooledObjects.begin(), pool->pooledObjects.end(), object) != pool->pooledObjects.end()) return;
    pool->pooledObjects.push_back(object);
    object->OnReturnedToPool(); // go.transform.SetParent(pooledObjectsParent): inactive
}

} // namespace game

// src/globalELock.cpp
#include "eAlloc.hpp"
#include "globalELock.hpp"
#include <utility> // for std::forward

namespace elock {

template <typename LockType, typename... Args>
ILockable* createLock(dsa::eAlloc& allocator, Args&&... args)
{
    void* memory = allocator.allocate_raw(sizeof(LockType));
    if (!memory)
    {
        LOG::ERROR("E_ALLOC", "Memory allocation failed for lock object.");
        return nullptr;
    }
    try
    {
        LockType* lock = new (memory) LockType(std::forward<Args>(args)...);
        return lock;
    }
    catch (...)
    {
        allocator.free(memory);
        LOG::ERROR("E_ALLOC", "Lock object construction failed.");
        return nullptr;
    }
}

void destroyLock(dsa::eAlloc& allocator, ILockable* lock)
{
    if (!lock) return;
    lock->~ILockable();
    allocator.free(static_cast<void*>(lock));
}

} // namespace elock
/**
 * @file eAlloc.cpp
 * @brief Implementation of TLSF-based MCU/host-agnostic memory allocator (dsa::eAlloc).
 *
 * See eAlloc.hpp for API, usage, and thread safety notes.
 */
#include "eAlloc.hpp"

namespace dsa
{

    eAlloc::eAlloc(void* mem, size_t bytes)
    {
        tlsf::initialise_control(&control);
        pool_count = 0;
        lock_ = nullptr;
        auto_defragment_ = false;
        defragment_threshold_ = 0.7;
        alloc_count_ = 0;
        usePerPoolLocking_ = false;
       
        for (size_t i = 0; i < MAX_POOL; ++i) {
            pool_configs[i] = PoolConfig();
            memory_pools[i] = nullptr;
            pool_sizes[i] = 0;
            pool_locks_[i] = nullptr;
        }
        initialised = true;
        if(!add_pool(mem, bytes))
        {
            LOG::ERROR("E_ALLOC", "Failed to initialize allocator with initial pool (%p, %zu bytes).\n",
                       mem, bytes);
        }
    }

void* eAlloc::add_pool(void* mem, size_t bytes, const PoolConfig& config)
{
#if !EALLOC_NO_LOCKING
    if (usePerPoolLocking_ && pool_count < MAX_POOL && pool_locks_[pool_count]) {
        elock::LockGuard guard(*pool_locks_[pool_count]);
    } else if (lock_) {
        elock::LockGuard guard(*lock_);
    }
#endif
    if(pool_count >= MAX_POOL)
    {
        LOG::ERROR("E_ALLOC", "Maximum number of pools (%d) reached.\n", MAX_POOL);
        return nullptr;
    }

    size_t pool_overhead = tlsf::pool_overhead();
    size_t align_size = tlsf::align_size();
    size_t min_block_size = tlsf::min_block_size();
    size_t max_block_size = tlsf::max_block_size();
    size_t alloc_overhead = tlsf::alloc_overhead();

    const size_t pool_bytes = tlsf::align_down(bytes - pool_overhead, align_size);
    if((reinterpret_cast<ptrdiff_t>(mem) % align_size) != 0)
    {
        LOG::ERROR("E_ALLOC", "add_pool: Memory must be aligned by %zu bytes", align_size);
        return nullptr;
    }

    if(pool_bytes < min_block_size || pool_bytes > max_block_size)
    {
        LOG::ERROR("E_ALLOC", "add_pool: Memory size must be between %zu and %zu bytes.\n",
                   static_cast<unsigned int>(pool_overhead + min_block_size),
                   static_cast<unsigned int>(pool_overhead + max_block_size));
        return nullptr;
    }

    /*
     ** Create the main free block. Offset the start of the block slightly
     ** so that the prev_phys_block field falls outside of the pool -
     ** it will never be used.
     */

    BlockHeader* block =
        tlsf::offset_to_block_nc(mem, -static_cast<tlsf::tlsfptr_t>(alloc_overhead));

    tlsf::set_size(block, pool_bytes);
    tlsf::set_free(block);
    tlsf::set_prev_used(block);
    tlsf::insert(&control, block);
    /* Split the block to create a zero-size sentinel block. */
    BlockHeader* next = tlsf::link_next(block);
    tlsf::set_size(next, 0);
    tlsf::set_used(next);
    tlsf::set_prev_free(next);

    memory_pools[pool_count] = mem;
    pool_sizes[pool_count] = pool_bytes;
    pool_configs[pool_count] = config;
    pool_count++;
    LOG::SUCCESS("E_ALLOC", "Added pool %p (%zu bytes). Total pools: %d\n", mem, bytes, pool_count);
    return mem;
}

void eAlloc::remove_pool(void* pool)
{
#if !EALLOC_NO_LOCKING
    size_t poolIndex = get_pool_index(pool);
    if (usePerPoolLocking_ && poolIndex < MAX_POOL && pool_locks_[poolIndex]) {
        elock::LockGuard guard(*pool_locks_[poolIndex]);
    } else if (lock_) {
        elock::LockGuard guard(*lock_);
    }
#endif
    for(size_t i = 0; i < pool_count; ++i)
    {
        if(memory_pools[i] == pool)
        {
            BlockHeader* block =
                tlsf::offset_to_block_nc(pool, -static_cast<int>(tlsf::alloc_overhead()));
            BlockHeader* next = tlsf::next(block);
            if(!tlsf::is_free(block) || tlsf::get_size(block) != pool_sizes[i]
               || !tlsf::is_last(next))
            {
                LOG::ERROR("E_ALLOC", "Cannot remove pool %p: it contains allocated blocks.\n",
                           pool);
                return;
            }
            int fl = 0, sl = 0;
            tlsf::mapping_insert(tlsf::get_size(block), &fl, &sl);
            tlsf::remove_free_block(&control, block, fl, sl);
            if(i < pool_count - 1)
            {
                memory_pools[i] = memory_pools[pool_count - 1];
                pool_sizes[i] = pool_sizes[pool_count - 1];
            }
            pool_count--;
            LOG::INFO("E_ALLOC", "Removed pool %p. Remaining pools: %d\n", pool, pool_count);
            return;
        }
    }
    LOG::ERROR("E_ALLOC", "Pool %p not found.\n", pool);
}

void* eAlloc::get_pool(void* pool) { return pool; }

int eAlloc::check_pool(void* pool)
{
    IntegrityResult integ = {0, 0};
    walk_pool(pool, integrity_walker, &integ);
    return integ.status;
}

int eAlloc::check() { return tlsf::check(&control); }

void eAlloc::integrity_walker(void* ptr, size_t size, int used, void* user)
{
    // No locking here! Locking must be handled by the caller, not the walker.
    BlockHeader* block = tlsf::from_ptr_nc(ptr);
    IntegrityResult* integ = static_cast<IntegrityResult*>(user);
    const int this_prev_status = tlsf::is_prev_free(block) ? 1 : 0;
    const int this_status = tlsf::is_free(block) ? 1 : 0;
    const size_t this_block_size = tlsf::get_size(block);
    int status = 0;
    (void)used;
    dsa_insist(integ->prev_status == this_prev_status && "prev status incorrect");
    dsa_insist(size == this_block_size && "block size incorrect");
    integ->prev_status = this_status;
    integ->status += status;
}

void* eAlloc::malloc(size_t size)
{
#if !EALLOC_NO_LOCKING
    // For malloc, we may not know the specific pool until allocation,
    // so use global lock unless per-pool locking is fully implemented with pool selection logic
    if (lock_) {
        elock::LockGuard guard(*lock_);
    }
#endif
    if(!initialised) return nullptr;
    
    // Check for auto-defragmentation if enabled and fragmentation is high
    if(auto_defragment_)
    {
        alloc_count_++;
        // Only check fragmentation every 10 allocations to avoid overhead
        if(alloc_count_ % 10 == 0)
        {
            StorageReport sr = report();
            if(sr.fragmentationFactor > defragment_threshold_)
            {
                LOG::INFO("E_ALLOC", "High fragmentation (%.2f) detected during malloc. Triggering auto-defragmentation.", sr.fragmentationFactor);
                defragment();
            }
        }
    }
    
    size_t adjusted_size = size;

    
    const size_t adjust = tlsf::adjust_request_size(adjusted_size, tlsf::align_size());
    BlockHeader* block = tlsf::locate_free(&control, adjust);
    void* ptr = tlsf::prepare_used(&control, block, adjust);
    if(!ptr && failure_handler_)
    {
        failure_handler_(size, failure_handler_data_);
    }

    return ptr;
}

void eAlloc::free(void* ptr)
{
#if !EALLOC_NO_LOCKING
    // Similar to malloc, free may not directly map to a specific pool without additional lookup
    if (lock_) {
        
        elock::LockGuard guard(*lock_);
    }
#endif
    if(!ptr || !initialised) return;

    void* actual_ptr = ptr;
#if EALLOC_BOUNDS_CHECKING
    // Adjust pointer back to include start canary
    actual_ptr = static_cast<char*>(ptr) - 4;
    // Get the block size to calculate end canary position
    BlockHeader* block_for_size = tlsf::from_ptr_nc(actual_ptr);
    size_t total_size = tlsf::get_size(block_for_size);
    size_t user_size = total_size - 8; // Subtract canary space
    // Check start canary
    if(*static_cast<uint32_t*>(actual_ptr) != CANARY_VALUE)
    {
        LOG::ERROR("E_ALLOC", "Start canary corrupted for block %p! Potential buffer underflow.", ptr);
    }
    // Check end canary
    if(total_size >= 8 && *static_cast<uint32_t*>(static_cast<char*>(actual_ptr) + total_size - 4) != CANARY_VALUE)
    {
        LOG::ERROR("E_ALLOC", "End canary corrupted for block %p! Potential buffer overflow.", ptr);
    }
#endif
    if(actual_ptr)
    {
        BlockHeader* block = tlsf::from_ptr_nc(actual_ptr);
        if(tlsf::is_free(block))
        {
            LOG::ERROR("E_ALLOC", "Double free detected for block %p! Ignoring.\n", actual_ptr);
            return;
        }
        tlsf::mark_as_free(block);
        block = tlsf::merge_prev(&control, block);
        block = tlsf::merge_next(&control, block);
        tlsf::insert(&control, block);
    }
}



void eAlloc::walk_pool(void* pool, Walker walker, void* user)
{
#if !EALLOC_NO_LOCKING
    size_t poolIndex = get_pool_index(pool);
    elock::ILockable* poolLock = (poolIndex < MAX_POOL && pool_locks_[poolIndex]) ? pool_locks_[poolIndex] : lock_;
    if (poolLock) {
        elock::LockGuard guard(*poolLock);
    }
#endif
    Walker pool_walker = walker ? walker : tlsf::default_walker;
    BlockHeader* block = tlsf::offset_to_block_nc(pool, -static_cast<int>(tlsf::alloc_overhead()));
    while(block && !tlsf::is_last(block))
    {
        pool_walker(tlsf::to_ptr_nc(block), tlsf::get_size(block), !tlsf::is_free(block), user);
        block = tlsf::next(block);
    }
}

void* eAlloc::memalign(size_t align, size_t size)
{
#if !EALLOC_NO_LOCKING
    if (lock_) {
        elock::LockGuard guard(*lock_);
    }
#endif
    if((align & (align - 1)) != 0 || align == 0)
    {
        LOG::ERROR("E_ALLOC", "Alignment must be a non-zero power of two.");
        return nullptr;
    }

    const size_t adjust = tlsf::adjust_request_size(size, tlsf::align_size());
    const size_t gap_minimum = sizeof(BlockHeader);
    const size_t size_with_gap = tlsf::adjust_request_size(adjust + align + gap_minimum, align);
    const size_t aligned_size = (adjust && align > tlsf::align_size()) ? size_with_gap : adjust;
    BlockHeader* block = tlsf::locate_free(&control, aligned_size);
    if(!block) return nullptr;
    dsa_assert(sizeof(BlockHeader) == tlsf::min_block_size() + tlsf::alloc_overhead());

    void* ptr = tlsf::to_ptr_nc(block);
    void* aligned = tlsf::align_ptr(ptr, align);
    uintptr_t aligned_addr = reinterpret_cast<uintptr_t>(aligned);
    uintptr_t block_start = reinterpret_cast<uintptr_t>(ptr);
    size_t gap = aligned_addr - block_start;

    if(gap < gap_minimum)
    {
        size_t needed = gap_minimum - gap;
        size_t adjust = ((needed + align - 1) / align) * align;
        aligned_addr += adjust;
        aligned = reinterpret_cast<void*>(aligned_addr);
        gap = aligned_addr - block_start;
    }

    if(gap)
    {
        BlockHeader* remaining = tlsf::trim_free_leading(&control, block, gap);
        if(!remaining) return nullptr;
        block = remaining;
    }
    return tlsf::prepare_used(&control, block, adjust);
}

void* eAlloc::realloc(void* ptr, size_t size)
{
#if !EALLOC_NO_LOCKING
    if (lock_) {
        elock::LockGuard guard(*lock_);
    }
#endif
    void* p = nullptr;
    if(ptr && size == 0)
    {
        free(ptr);
    }
    else if(!ptr)
    {
        p = malloc(size);
    }
    else
    {
        BlockHeader* block = tlsf::from_ptr_nc(ptr);
        BlockHeader* next = tlsf::next(block);
        const size_t cursize = tlsf::get_size(block);
        const size_t combined = cursize + tlsf::get_size(next) + tlsf::alloc_overhead();
        const size_t adjust = tlsf::adjust_request_size(size, tlsf::align_size());
        dsa_assert(!tlsf::is_free(block) && "block already marked as free");
        if(adjust > cursize && (!tlsf::is_free(next) || adjust > combined))
        {
            p = malloc(size);
            if(p)
            {
                const size_t minsize = dsa_min(cursize, size);
                memcpy(p, ptr, minsize);
                free(ptr);
            }
        }
        else
        {
            if(adjust > cursize)
            {
                tlsf::merge_next(&control, block);
                tlsf::mark_as_used(block);
            }
            tlsf::trim_used(&control, block, adjust);
            p = ptr;
        }
    }
    return p;
}

void* eAlloc::calloc(size_t num, size_t size)
{
#if !EALLOC_NO_LOCKING
    if (lock_) {
        elock::LockGuard guard(*lock_);
    }
#endif
    size_t total_size;
    if(num > 0 && size > SIZE_MAX / num)
    {
        LOG::ERROR("E_ALLOC", "calloc overflow detected.");
        return nullptr;
    }
    total_size = num * size;
    void* ptr = malloc(total_size);
    if(ptr)
    {
        memset(ptr, 0, total_size);
    }
    return ptr;
}

eAlloc::StorageReport eAlloc::report() const
{
#if !EALLOC_NO_LOCKING
    if (lock_) {
        elock::LockGuard guard(*lock_);
    }
#endif
    StorageReport result;
    result.totalFreeSpace = 0;
    result.largestFreeRegion = 0;
    result.smallestFreeRegion = std::numeric_limits<size_t>::max();
    result.freeBlockCount = 0;
    result.averageFreeBlockSize = 0;
    double sumFragmentation = 0.0;
    size_t activePoolCount = 0;

    for (size_t i = 0; i < pool_count; ++i)
    {
        if (memory_pools[i])
        {
            size_t poolFreeSpace = 0;
            size_t poolLargestFree = 0;
            size_t poolSmallestFree = std::numeric_limits<size_t>::max();
            size_t poolFreeBlocks = 0;

            BlockHeader* block = tlsf::offset_to_block_nc(memory_pools[i], -tlsf::alloc_overhead());
            while (block && !tlsf::is_last(block))
            {
                if (tlsf::is_free(block))
                {
                    size_t block_size = tlsf::get_size(block);
                    poolFreeSpace += block_size;
                    poolFreeBlocks++;
                    if (block_size > poolLargestFree) poolLargestFree = block_size;
                    if (block_size < poolSmallestFree) poolSmallestFree = block_size;
                }
                block = tlsf::next(block);
            }

            result.totalFreeSpace += poolFreeSpace;
            result.freeBlockCount += poolFreeBlocks;
            if (poolLargestFree > result.largestFreeRegion) result.largestFreeRegion = poolLargestFree;
            if (poolSmallestFree < result.smallestFreeRegion) result.smallestFreeRegion = poolSmallestFree;

            double poolFrag = (poolFreeSpace > 0) ? 1.0 - static_cast<double>(poolLargestFree) / poolFreeSpace : 0.0;
            sumFragmentation += poolFrag;
            activePoolCount++;
        }
    }

    result.averageFreeBlockSize = (result.freeBlockCount > 0) ? result.totalFreeSpace / result.freeBlockCount : 0;
    result.fragmentationFactor = (activePoolCount > 0) ? sumFragmentation / activePoolCount : 0.0;
    return result;
}

void eAlloc::logStorageReport() const
{
#if !EALLOC_NO_LOCKING
    if (lock_) {
        elock::LockGuard guard(*lock_);
    }
#endif
    StorageReport sr = report();
    LOG::INFO("E_ALLOC", "=== Storage Report ===");
    LOG::INFO("E_ALLOC", "Total Free Space: %zu bytes", sr.totalFreeSpace);
    LOG::INFO("E_ALLOC", "Largest Free Region: %zu bytes", sr.largestFreeRegion);
    LOG::INFO("E_ALLOC", "Smallest Free Region: %zu bytes", sr.smallestFreeRegion);
    LOG::INFO("E_ALLOC", "Number of Free Blocks: %zu", sr.freeBlockCount);
    LOG::INFO("E_ALLOC", "Average Free Block Size: %zu bytes", sr.averageFreeBlockSize);
    LOG::INFO("E_ALLOC", "Average Fragmentation Factor: %.4f", sr.fragmentationFactor);
    // Add per-pool breakdown to diagnose fragmentation
    LOG::INFO("E_ALLOC", "Per-Pool Breakdown:");
    for(size_t i = 0; i < pool_count; ++i)
    {
        if(memory_pools[i])
        {
            size_t pool_size = pool_sizes[i];
            size_t free_space = 0;
            size_t free_blocks = 0;
            size_t largest_free = 0;
            BlockHeader* block = tlsf::offset_to_block_nc(memory_pools[i], -tlsf::alloc_overhead());
            while(block && !tlsf::is_last(block))
            {
                if(tlsf::is_free(block))
                {
                    size_t block_size = tlsf::get_size(block);
                    free_space += block_size;
                    free_blocks++;
                    if(block_size > largest_free) largest_free = block_size;
                }
                block = tlsf::next(block);
            }
            double pool_frag = (free_space > 0) ? 1.0 - static_cast<double>(largest_free) / free_space : 0.0;
            LOG::INFO("E_ALLOC", "  Pool %zu: Total Size=%zu, Free=%zu, Blocks=%zu, Largest Free=%zu, Frag=%.4f", 
                      i, pool_size, free_space, free_blocks, largest_free, pool_frag);
        }
    }
    if(sr.fragmentationFactor > defragment_threshold_)
    {
        LOG::INFO("E_ALLOC", "High fragmentation detected. Consider calling defragment().");
    }
}

size_t eAlloc::defragment()
{
#if !EALLOC_NO_LOCKING
    if (lock_) {
        elock::LockGuard guard(*lock_);
    }
#endif
    size_t merge_count = 0;
    for(size_t i = 0; i < pool_count; ++i)
    {
        BlockHeader* block = tlsf::offset_to_block_nc(memory_pools[i], -static_cast<int>(tlsf::alloc_overhead()));
        while(block && !tlsf::is_last(block))
        {
            if(tlsf::is_free(block))
            {
                BlockHeader* original_block = block;
                block = tlsf::merge_prev(&control, block);
                if(block == original_block) // No previous merge occurred
                {
                    block = tlsf::merge_next(&control, block);
                }
                if(block != original_block) // A merge occurred
                {
                    tlsf::insert(&control, block);
                    merge_count++;
                }
            }
            block = tlsf::next(block);
        }
    }
    LOG::INFO("E_ALLOC", "Defragmentation completed. Merged %zu free blocks.", merge_count);
    return merge_count;
}

void eAlloc::setAutoDefragment(bool enable, double threshold)
{
#if !EALLOC_NO_LOCKING
    if (lock_) {
        
        elock::LockGuard guard(*lock_);
    }
#endif
    auto_defragment_ = enable;
    if(threshold >= 0.0 && threshold <= 1.0)
    {
        defragment_threshold_ = threshold;
    }
    else
    {
        LOG::ERROR("E_ALLOC", "Invalid defragmentation threshold %.2f. Must be between 0.0 and 1.0. Using default 0.7.", threshold);
        defragment_threshold_ = 0.7;
    }
    LOG::INFO("E_ALLOC", "Auto-defragmentation %s with threshold %.2f.", enable ? "enabled" : "disabled", defragment_threshold_);
}

void eAlloc::setLock(elock::ILockable* lock)
{
#if !EALLOC_NO_LOCKING
    lock_ = lock;
#endif
}

void eAlloc::setLockForPool(size_t poolIndex, elock::ILockable* lock)
{
#if !EALLOC_NO_LOCKING
    if(poolIndex < MAX_POOL)
    {
        pool_locks_[poolIndex] = lock;
    }
#endif
}

void eAlloc::setPerPoolLocking(bool enable)
{
#if !EALLOC_NO_LOCKING
    if (lock_) {
        elock::LockGuard guard(*lock_);
    }
#endif
    usePerPoolLocking_ = enable;
    LOG::INFO("E_ALLOC", "Per-pool locking %s.", enable ? "enabled" : "disabled");
}

size_t eAlloc::get_pool_index(void* pool) const
{
    for(size_t i = 0; i < pool_count; ++i)
    {
        if(memory_pools[i] == pool)
        {
            return i;
        }
    }
    return MAX_POOL;
}

} // namespace dsa
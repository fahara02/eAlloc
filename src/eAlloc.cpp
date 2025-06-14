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
    if(!add_pool(mem, bytes))
    {
        LOG::ERROR("E_ALLOC", "Failed to initialize allocator with initial pool (%p, %zu bytes).\n",
                   mem, bytes);
    }
    initialised = true;
}

void* eAlloc::add_pool(void* mem, size_t bytes)
{
    if(lock_) elock::LockGuard guard(*lock_);
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
    pool_count++;
    LOG::SUCCESS("E_ALLOC", "Added pool %p (%zu bytes). Total pools: %d\n", mem, bytes, pool_count);
    return mem;
}

void eAlloc::remove_pool(void* pool)
{
    if(lock_) elock::LockGuard guard(*lock_);
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
    if(lock_) elock::LockGuard guard(*lock_);
    const size_t adjust = tlsf::adjust_request_size(size, tlsf::align_size());
    BlockHeader* block = tlsf::locate_free(&control, adjust);
    return tlsf::prepare_used(&control, block, adjust);
}

void eAlloc::free(void* ptr)
{
    if(lock_) elock::LockGuard guard(*lock_);
    if(ptr)
    {
        BlockHeader* block = tlsf::from_ptr_nc(ptr);
        if(tlsf::is_free(block))
        {
            LOG::ERROR("E_ALLOC", "Double free detected for block %p! Ignoring.\n", ptr);
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
    if(lock_) elock::LockGuard guard(*lock_);
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

    // if (gap && gap < gap_minimum) {
    //   const size_t gap_remain = gap_minimum - gap;
    //   const size_t offset = dsa_max(gap_remain, align);
    //   const void *next_aligned = reinterpret_cast<void *>(
    //       reinterpret_cast<tlsf::tlsfptr_t>(aligned) + offset);
    //   aligned = tlsf::align_ptr(next_aligned, align);
    //   gap = static_cast<size_t>(reinterpret_cast<tlsf::tlsfptr_t>(aligned) -
    //                             reinterpret_cast<tlsf::tlsfptr_t>(ptr));
    // }
    // if (gap) {
    //   dsa_assert(gap >= gap_minimum && "gap size too small");
    //   block = tlsf::trim_free_leading(&control, block, gap);
    // }
    // Adjust alignment to ensure sufficient gap
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
    if(lock_) elock::LockGuard guard(*lock_);
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
    if(lock_) elock::LockGuard guard(*lock_);
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
    if(lock_) elock::LockGuard guard(*lock_);
    StorageReport report{};
    for(size_t fl = 0; fl < tlsf::cabinets(); ++fl)
    {
        if(!(control.fl_bitmap & (1U << fl))) continue;
        for(size_t sl = 0; sl < tlsf::shelves(); ++sl)
        {
            if(!(control.cabinets[fl].sl_bitmap & (1U << sl))) continue;
            BlockHeader* block = control.cabinets[fl].shelves[sl];
            BlockHeader* current = block;
            while(current != &control.block_null)
            {
                size_t size = tlsf::get_size(current);
                report.totalFreeSpace += size;
                if(size > report.largestFreeRegion) report.largestFreeRegion = size;
                report.freeBlockCount++;
                current = current->next_free;
            }
        }
    }
    if(report.totalFreeSpace > 0)
    {
        report.fragmentationFactor =
            1.0 - static_cast<double>(report.largestFreeRegion) / report.totalFreeSpace;
    }
    else
    {
        report.fragmentationFactor = 0.0;
    }
    return report;
}

void eAlloc::logStorageReport() const
{
    StorageReport sr = report();
    LOG::INFO("E_ALLOC", "=== Storage Report ===");
    LOG::INFO("E_ALLOC", "Total Free Space: %zu bytes", sr.totalFreeSpace);
    LOG::INFO("E_ALLOC", "Largest Free Region: %zu bytes", sr.largestFreeRegion);
    LOG::INFO("E_ALLOC", "Number of Free Blocks: %zu", sr.freeBlockCount);
    LOG::INFO("E_ALLOC", "Fragmentation Factor: %.4f", sr.fragmentationFactor);
}

} // namespace dsa
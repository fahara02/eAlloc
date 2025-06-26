/**
 * @file eAlloc.cpp
 * @brief Implementation of TLSF-based MCU/host-agnostic memory allocator (dsa::eAlloc).
 *
 * See eAlloc.hpp for API, usage, and thread safety notes.
 */
 #include "eAlloc.hpp"

 namespace dsa
 {
 
 eAlloc::eAlloc(void* memory, size_t bytes)
 {
     pool_count = 0;
     lock_ = nullptr;
     initialised = false;
     auto_defragment_ = false;
     defragment_threshold_ = DEFRAGMENTATION_THRESH ;
    
     failure_handler_ = nullptr;
     failure_handler_data_ = nullptr;
     usePerPoolLocking_ = false;
     resize_handler_ = nullptr;
     resize_handler_data_ = nullptr;
     alloc_count_ = 0;
 #if defined(EALLOC_ENABLE_OWNERSHIP_TAG) && EALLOC_ENABLE_OWNERSHIP_TAG
     ownership_tag_ = 0;
 #endif
     for(size_t i = 0; i < MAX_POOL; ++i)
     {
         tlsf::initialise_control(&controls[i]);
         pool_configs[i] = PoolConfig();
         memory_pools[i] = nullptr;
         pool_sizes[i] = 0;
         pool_locks_[i] = nullptr;
     }
     if(!add_pool(memory, bytes))
     {
         LOG::ERROR("E_ALLOC", "Failed to initialize allocator with initial pool (%p, %zu bytes).\n",
                    memory, bytes);
     }
     else
     {
         initialised = true;
     }
 }
 
 void* eAlloc::add_pool(void* mem, size_t bytes, const PoolConfig& config)
 {
 #if !EALLOC_NO_LOCKING
     if(usePerPoolLocking_ && pool_count < MAX_POOL && pool_locks_[pool_count])
     {
         elock::LockGuard guard(*pool_locks_[pool_count]);
     }
     else if(lock_)
     {
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
     tlsf::insert(&controls[pool_count], block);
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
     if(usePerPoolLocking_ && poolIndex < MAX_POOL && pool_locks_[poolIndex])
     {
         elock::LockGuard guard(*pool_locks_[poolIndex]);
     }
     else if(lock_)
     {
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
             tlsf::remove_free_block(&controls[i], block, fl, sl);
             if(i < pool_count - 1)
             {
                 memory_pools[i] = memory_pools[pool_count - 1];
                 pool_sizes[i] = pool_sizes[pool_count - 1];
                 pool_configs[i] = pool_configs[pool_count - 1];
                 controls[i] = controls[pool_count - 1];
             }
             pool_count--;
             LOG::INFO("E_ALLOC", "Removed pool %p. Remaining pools: %d\n", pool, pool_count);
             return;
         }
     }
     LOG::ERROR("E_ALLOC", "Pool %p not found.\n", pool);
 }
 
 void* eAlloc::get_pool(void* ptr)
 {
     if(!ptr) return nullptr;
     return get_pool_from_block(ptr);
 }
 
 size_t eAlloc::get_pool_index(void* pool) const
 {
     for(size_t i = 0; i < pool_count; ++i)
     {
         if(memory_pools[i] == pool) return i;
     }
     return MAX_POOL;
 }
 
 void* eAlloc::get_pool_from_block(const void* ptr)
 {
     if(!ptr) return nullptr;
     const BlockHeader* block = tlsf::from_ptr(ptr);
     for(size_t i = 0; i < pool_count; ++i)
     {
         void* pool_start = memory_pools[i];
         void* pool_end = static_cast<char*>(pool_start) + pool_sizes[i];
         if(pool_start <= ptr && ptr < pool_end)
         {
             return pool_start;
         }
     }
     return nullptr;
 }
 
 int eAlloc::check_pool(void* pool)
 {
 #if !EALLOC_NO_LOCKING
     size_t poolIndex = get_pool_index(pool);
     if(usePerPoolLocking_ && poolIndex < MAX_POOL && pool_locks_[poolIndex])
     {
         elock::LockGuard guard(*pool_locks_[poolIndex]);
     }
     else if(lock_)
     {
         elock::LockGuard guard(*lock_);
     }
 #endif
     size_t index = get_pool_index(pool);
     if(index == MAX_POOL) return -1;
     IntegrityResult integ = {0, 0};
     walk_pool(pool, integrity_walker, &integ);
     return integ.status;
 }
 
 int eAlloc::check()
 {
 #if !EALLOC_NO_LOCKING
     if(lock_)
     {
         elock::LockGuard guard(*lock_);
     }
 #endif
     int status = 0;
     for(size_t i = 0; i < pool_count; ++i)
     {
         status += tlsf::check(&controls[i]);
     }
     return status;
 }
 
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
 
 void eAlloc::walk_pool(void* pool, Walker walker, void* user)
 {
 #if !EALLOC_NO_LOCKING
     size_t poolIndex = get_pool_index(pool);
     elock::ILockable* poolLock =
         (poolIndex < MAX_POOL && pool_locks_[poolIndex]) ? pool_locks_[poolIndex] : lock_;
     if(poolLock)
     {
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
 
 void* eAlloc::malloc(size_t size) { return malloc(size, -1, Policy::DEFAULT_POLICY); }
 
 void* eAlloc::malloc(size_t size, int priority, Policy policy)
 {
 #if !EALLOC_NO_LOCKING
     if(lock_)
     {
         elock::LockGuard guard(*lock_);
     }
 #endif
     if(!size) return nullptr;
     if(auto_defragment_)
     {
         alloc_count_++;
         // Only check fragmentation every 10 allocations to avoid overhead
         if(alloc_count_ % 10 == 0)
         {
             StorageReport sr = report();
             if(sr.fragmentationFactor > defragment_threshold_)
             {
                 LOG::INFO("E_ALLOC",
                           "High fragmentation (%.2f) detected during malloc. Triggering "
                           "auto-defragmentation.",
                           sr.fragmentationFactor);
                 defragment();
             }
         }
     }
 
     size_t adjusted_size = tlsf::adjust_request_size(size, tlsf::align_size());
     if(!adjusted_size) return nullptr;
 
     // Select pool based on priority and policy
     size_t selected_pool = 0;
     bool found = false;
     int highest_priority = -1;
     for(size_t i = 0; i < pool_count; ++i)
     {
         if(priority >= 0 && pool_configs[i].priority < priority) continue;
         if(policy != Policy::DEFAULT_POLICY && pool_configs[i].policy != policy) continue;
         if(pool_configs[i].priority > highest_priority)
         {
             highest_priority = pool_configs[i].priority;
             selected_pool = i;
             found = true;
         }
     }
     if(!found && priority >= 0)
     {
         // Relax priority constraint if no matching pool found
         for(size_t i = 0; i < pool_count; ++i)
         {
             if(policy != Policy::DEFAULT_POLICY && pool_configs[i].policy != policy) continue;
             if(pool_configs[i].priority > highest_priority)
             {
                 highest_priority = pool_configs[i].priority;
                 selected_pool = i;
                 found = true;
             }
         }
     }
     void* ptr = nullptr;
     if(!found)
     {
         // If still not found, ignore policy and select based on availability
         for(size_t i = 0; i < pool_count; ++i)
         {
             BlockHeader* block = tlsf::locate_free(&controls[i], adjusted_size);
             if(block)
             {
                 ptr = tlsf::prepare_used(&controls[i], block, adjusted_size);
                 selected_pool = i;
                 break;
             }
         }
     }
     else
     {
         BlockHeader* block = tlsf::locate_free(&controls[selected_pool], adjusted_size);
         if(block)
         {
             ptr = tlsf::prepare_used(&controls[selected_pool], block, adjusted_size);
         }
         // If selected pool has no space, try others
         if(!ptr)
         {
             for(size_t i = 0; i < pool_count; ++i)
             {
                 if(i == selected_pool) continue;
                 block = tlsf::locate_free(&controls[i], adjusted_size);
                 if(block)
                 {
                     ptr = tlsf::prepare_used(&controls[i], block, adjusted_size);
                     selected_pool = i;
                     break;
                 }
             }
         }
     }
 
     if(!ptr && failure_handler_)
     {
         failure_handler_(size, failure_handler_data_);
     }
 #if defined(EALLOC_ENABLE_OWNERSHIP_TAG) && EALLOC_ENABLE_OWNERSHIP_TAG && !EALLOC_NO_OWNERSHIP_CHECKING
     if(ptr)
     {
         tlsf::BlockHeader* block = tlsf::from_ptr_nc(ptr);
         tlsf::set_owner_tag(block, ownership_tag_);
         LOG::INFO("E_ALLOC", "Allocated block %p with owner tag %u.\n", ptr, ownership_tag_);
     }
 #endif
     return ptr;
 }
 
 void eAlloc::free(void* ptr)
 {
     if(!ptr) return;
 #if !EALLOC_NO_LOCKING
     size_t poolIndex = get_pool_index(get_pool(ptr));
     if(usePerPoolLocking_ && poolIndex < MAX_POOL && pool_locks_[poolIndex])
     {
         elock::LockGuard guard(*pool_locks_[poolIndex]);
     }
     else if(lock_)
     {
         elock::LockGuard guard(*lock_);
     }
 #endif
     if(!ptr || !initialised) return;
     BlockHeader* block = tlsf::from_ptr_nc(ptr);
     size_t pool_index = get_pool_index(get_pool(ptr));
     if(pool_index == MAX_POOL) return;
 
 #if defined(EALLOC_ENABLE_OWNERSHIP_TAG) && EALLOC_ENABLE_OWNERSHIP_TAG && !EALLOC_NO_OWNERSHIP_CHECKING
     uint32_t tag = tlsf::get_owner_tag(block);
     if(tag != ownership_tag_)
     {
         LOG::WARNING("E_ALLOC", "Freeing block %p with mismatched owner tag %u (expected %u).\n", ptr, tag, ownership_tag_);
     }
 #endif
 
     void* actual_ptr = ptr;
 
     if(actual_ptr)
     {
         if(tlsf::is_free(block))
         {
             LOG::ERROR("E_ALLOC", "Double free detected for block %p! Ignoring.\n", actual_ptr);
             return;
         }
         tlsf::mark_as_free(block);
         block = tlsf::merge_prev(&controls[pool_index], block);
         block = tlsf::merge_next(&controls[pool_index], block);
         tlsf::insert(&controls[pool_index], block);
     }
 }
 
 void* eAlloc::memalign(size_t align, size_t size)
 {
 #if !EALLOC_NO_LOCKING
     if(lock_)
     {
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
 
     // Try to find a block in any pool
     for(size_t i = 0; i < pool_count; ++i)
     {
         BlockHeader* block = tlsf::locate_free(&controls[i], aligned_size);
         if(block)
         {
             void* ptr = tlsf::to_ptr_nc(block);
             void* aligned = tlsf::align_ptr(ptr, align);
             uintptr_t aligned_addr = reinterpret_cast<uintptr_t>(aligned);
             uintptr_t block_start = reinterpret_cast<uintptr_t>(ptr);
             size_t gap = aligned_addr - block_start;
 
             if(gap < gap_minimum)
             {
                 size_t needed = gap_minimum - gap;
                 size_t adjust_gap = ((needed + align - 1) / align) * align;
                 aligned_addr += adjust_gap;
                 aligned = reinterpret_cast<void*>(aligned_addr);
                 gap = aligned_addr - block_start;
             }
 
             if(gap)
             {
                 BlockHeader* remaining = tlsf::trim_free_leading(&controls[i], block, gap);
                 if(!remaining) continue; // Try next pool if trimming fails
                 block = remaining;
             }
             return tlsf::prepare_used(&controls[i], block, adjust);
         }
     }
 
     // If allocation fails and a handler is set, invoke it
     if(failure_handler_)
     {
         failure_handler_(size, failure_handler_data_);
     }
     return nullptr;
 }
 
 void* eAlloc::realloc(void* ptr, size_t size)
 {
 #if !EALLOC_NO_LOCKING
     size_t poolIndex = get_pool_index(get_pool(ptr));
     if(usePerPoolLocking_ && poolIndex < MAX_POOL && pool_locks_[poolIndex])
     {
         elock::LockGuard guard(*pool_locks_[poolIndex]);
     }
     else if(lock_)
     {
         elock::LockGuard guard(*lock_);
     }
 #endif
     if(!ptr)
     {
         return malloc(size);
     }
     if(!size)
     {
         free(ptr);
         return nullptr;
     }
 
     size_t pool_index = get_pool_index(get_pool(ptr));
     if(pool_index == MAX_POOL) return nullptr;
 
     BlockHeader* block = tlsf::from_ptr_nc(ptr);
     size_t current_size = tlsf::get_size(block);
     size_t adjusted_size = tlsf::adjust_request_size(size, tlsf::align_size());
     if(!adjusted_size) return nullptr;
 
     if(adjusted_size <= current_size)
     {
         tlsf::trim_used(&controls[pool_index], block, adjusted_size);
         return ptr;
     }
 
     BlockHeader* next_block = tlsf::next(block);
     if(tlsf::is_free(next_block))
     {
         size_t combined_size =
             current_size + tlsf::get_size(next_block)
             + sizeof(size_t); // Use sizeof(size_t) as the overhead value directly
         if(combined_size >= adjusted_size)
         {
             int fl = 0, sl = 0;
             tlsf::mapping_insert(tlsf::get_size(next_block), &fl, &sl);
             tlsf::remove_free_block(&controls[pool_index], next_block, fl, sl);
             block = tlsf::absorb(block, next_block);
             tlsf::trim_used(&controls[pool_index], block, adjusted_size);
             return ptr;
         }
     }
 
     void* new_ptr = malloc(size);
     if(!new_ptr) return nullptr;
 
     memcpy(new_ptr, ptr, current_size);
     free(ptr);
     return new_ptr;
 }
 
 void* eAlloc::calloc(size_t num, size_t size)
 {
     size_t total = num * size;
     void* ptr = malloc(total);
     if(ptr)
     {
         memset(ptr, 0, total);
     }
     return ptr;
 }
 
 eAlloc::StorageReport eAlloc::report() const
 {
 #if !EALLOC_NO_LOCKING
     if(lock_)
     {
         elock::LockGuard guard(*lock_);
     }
 #endif
     StorageReport report;
     report.totalFreeSpace = 0;
     report.largestFreeRegion = 0;
     report.smallestFreeRegion = 0;
     report.freeBlockCount = 0;
     report.averageFreeBlockSize = 0;
     report.fragmentationFactor = 0.0;
     size_t fragmented_sum = 0;
     for(size_t i = 0; i < pool_count; ++i)
     {
         BlockHeader* block =
             tlsf::offset_to_block_nc(memory_pools[i], -static_cast<int>(tlsf::alloc_overhead()));
         size_t free_space = 0;
         size_t free_blocks = 0;
         size_t largest = 0;
         size_t smallest = pool_sizes[i];
         while(block && !tlsf::is_last(block))
         {
             size_t block_size = tlsf::get_size(block);
             if(tlsf::is_free(block))
             {
                 free_space += block_size;
                 free_blocks++;
                 largest = (block_size > largest) ? block_size : largest;
                 smallest = (block_size < smallest) ? block_size : smallest;
             }
             block = tlsf::next(block);
         }
         report.totalFreeSpace += free_space;
         report.freeBlockCount += free_blocks;
         report.largestFreeRegion =
             (largest > report.largestFreeRegion) ? largest : report.largestFreeRegion;
         report.smallestFreeRegion =
             (smallest < report.smallestFreeRegion || report.smallestFreeRegion == 0) ?
                 smallest :
                 report.smallestFreeRegion;
         if(free_space > 0 && largest > 0)
             fragmented_sum += (free_space - largest);
     }
     if(report.freeBlockCount > 0)
     {
         report.averageFreeBlockSize = report.totalFreeSpace / report.freeBlockCount;
     }
     if(report.totalFreeSpace > 0)
     {
         report.fragmentationFactor = static_cast<double>(fragmented_sum) / report.totalFreeSpace;
     }
     return report;
 }
 
 void eAlloc::logStorageReport() const
 {
 #if !EALLOC_NO_LOCKING
     if(lock_)
     {
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
             double pool_frag =
                 (free_space > 0) ? 1.0 - static_cast<double>(largest_free) / free_space : 0.0;
             LOG::INFO(
                 "E_ALLOC",
                 "  Pool %zu: Total Size=%zu, Free=%zu, Blocks=%zu, Largest Free=%zu, Frag=%.4f", i,
                 pool_size, free_space, free_blocks, largest_free, pool_frag);
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
     if(lock_)
     {
         elock::LockGuard guard(*lock_);
     }
 #endif
     size_t merged = 0;
     for(size_t i = 0; i < pool_count; ++i)
     {
         BlockHeader* block =
             tlsf::offset_to_block_nc(memory_pools[i], -static_cast<int>(tlsf::alloc_overhead()));
         while(!tlsf::is_last(block))
         {
             if(tlsf::is_free(block))
             {
                 BlockHeader* next_block = tlsf::next(block);
                 if(!tlsf::is_last(next_block) && tlsf::is_free(next_block))
                 {
                     int fl = 0, sl = 0;
                     tlsf::mapping_insert(tlsf::get_size(next_block), &fl, &sl);
                     tlsf::remove_free_block(&controls[i], next_block, fl, sl);
                     block = tlsf::absorb(block, next_block);
                     merged++;
                 }
                 else
                 {
                     block = next_block;
                 }
             }
             else
             {
                 block = tlsf::next(block);
             }
         }
     }
     return merged;
 }
 
 bool eAlloc::resize_pool(void* pool, size_t new_bytes)
 {
 #if !EALLOC_NO_LOCKING
     size_t poolIndex = get_pool_index(pool);
     if(usePerPoolLocking_ && poolIndex < MAX_POOL && pool_locks_[poolIndex])
     {
         elock::LockGuard guard(*pool_locks_[poolIndex]);
     }
     else if(lock_)
     {
         elock::LockGuard guard(*lock_);
     }
 #endif
     size_t index = get_pool_index(pool);
     if(index >= pool_count)
     {
         LOG::ERROR("E_ALLOC", "Pool %p not found for resizing.\n", pool);
         return false;
     }
 
     size_t current_size = pool_sizes[index];
     if(new_bytes == current_size)
     {
         return true; // No change needed
     }
 
     if(new_bytes < tlsf::min_block_size() || new_bytes > tlsf::max_block_size())
     {
         LOG::ERROR("E_ALLOC", "New pool size must be between %zu and %zu bytes.\n",
                    tlsf::min_block_size(), tlsf::max_block_size());
         return false;
     }
 
     // Check if pool has allocated blocks (prevent resizing if data would be lost)
     BlockHeader* block = tlsf::offset_to_block_nc(pool, -static_cast<int>(tlsf::alloc_overhead()));
     BlockHeader* next = tlsf::next(block);
     if(!tlsf::is_free(block) || !tlsf::is_last(next))
     {
         // If there are allocated blocks beyond the first, or first block isn't free
         LOG::ERROR("E_ALLOC", "Cannot resize pool %p: it contains allocated blocks.\n", pool);
         return false;
     }
 
     // If shrinking, check if the first free block can accommodate the reduction
     if(new_bytes < current_size)
     {
         if(tlsf::get_size(block) < new_bytes)
         {
             LOG::ERROR("E_ALLOC", "Cannot shrink pool %p: free space less than new size.\n", pool);
             return false;
         }
         // Adjust the size of the free block
         tlsf::set_size(block, new_bytes);
         next = tlsf::link_next(block);
         tlsf::set_size(next, 0);
         tlsf::set_used(next);
         tlsf::set_prev_free(next);
         pool_sizes[index] = new_bytes;
         LOG::SUCCESS("E_ALLOC", "Shrunk pool %p to %zu bytes.\n", pool, new_bytes);
         return true;
     }
 
     // If expanding, use the user-provided callback if available
     if(new_bytes > current_size && resize_handler_)
     {
         void* new_pool = resize_handler_(pool, current_size, new_bytes, resize_handler_data_);
         if(new_pool)
         {
             // Remove old pool entry and its TLSF control structure
             tlsf::initialise_control(&controls[index]); // Reset the control structure for this pool index
             memory_pools[index] = nullptr;
             pool_sizes[index] = 0;
             pool_count--; // Decrease pool count as we're replacing this pool
             // Add new pool with the expanded size at the same index
             if(add_pool(new_pool, new_bytes, pool_configs[index]))
             {
                 LOG::SUCCESS("E_ALLOC", "Expanded pool from %p to %p with size %zu bytes.\n",
                              pool, new_pool, new_bytes);
                 return true;
             }
             else
             {
                 LOG::ERROR("E_ALLOC", "Failed to add expanded pool %p with size %zu bytes.\n",
                            new_pool, new_bytes);
                 pool_count++; // Restore pool count if add_pool failed
                 return false;
             }
         }
         else
         {
             LOG::ERROR("E_ALLOC", "Resize handler failed to allocate memory for pool %p to size %zu bytes.\n",
                        pool, new_bytes);
             return false;
         }
     }
 
     // If no handler or handler failed, report expansion not supported
     LOG::ERROR("E_ALLOC", "Expanding pool %p is not supported without additional memory mapping or resize handler.\n",
                pool);
     return false;
 }
 
 void eAlloc::setAutoDefragment(bool enable, double threshold)
 {
     auto_defragment_ = enable;
     defragment_threshold_ = (threshold >= 0.0 && threshold <= 1.0) ? threshold : 0.7;
 }
 
 #if !EALLOC_NO_LOCKING
 void eAlloc::setLock(elock::ILockable* lock) { lock_ = lock; }
 
 void eAlloc::setLockForPool(size_t poolIndex, elock::ILockable* lock)
 {
     if(poolIndex < MAX_POOL)
     {
         pool_locks_[poolIndex] = lock;
     }
 }
 
 void eAlloc::setPerPoolLocking(bool enable) { usePerPoolLocking_ = enable; }
 #endif
 
 } // namespace dsa
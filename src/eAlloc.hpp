/**
 * @file eAlloc.hpp
 * @brief TLSF-based MCU/host-agnostic memory allocator with multi-pool support.
 *
 * Provides efficient, deterministic allocation and deallocation with minimal fragmentation.
 * Supports multiple memory pools, alignment, reallocation, and MCU/host-agnostic locking via elock.
 *
 * Usage:
 *   - Use dsa::eAlloc for all heap management (malloc/free, STL, or custom allocators).
 *   - For thread safety, call setLock() with a elock::ILockable* mutex adapter.
 *   - Add or remove pools as needed for flexible memory regions.
 *
 * Thread Safety:
 *   - Not thread-safe by default. To enable, set a elock (see globalELock.hpp).
 *   - Only one elock per allocator instance is recommended.
 */
 #pragma once
 #include "Logger.hpp"
 #include "tlsf.hpp"
 #include "globalELock.hpp"
 
 #ifndef EALLOC_NO_LOCKING
     #define EALLOC_NO_LOCKING 0
 #endif
 
 #ifndef EALLOC_NO_OWNERSHIP_CHECKING
     #define EALLOC_NO_OWNERSHIP_CHECKING 0
 #endif
 
 #ifndef EALLOC_ENABLE_OWNERSHIP_TAG
     #define EALLOC_ENABLE_OWNERSHIP_TAG 0
 #endif
 
 namespace dsa
 {
 
 /**
  * @brief A memory allocator class based on the Two-Level Segregated Fit (TLSF)
  * algorithm.
  *
  * The eAlloc class provides efficient memory management with support for
  * multiple pools, alignment, reallocation, and storage reporting.
  */
 class eAlloc
 {
     using tlsf = dsa::TLSF<5>;             ///< TLSF allocator with 32 second-level lists.
     using Control = tlsf::Control;         ///< TLSF control structure type.
     using BlockHeader = tlsf::BlockHeader; ///< TLSF block header type.
     using Walker = tlsf::tlsf_walker;      ///< Function type for walking memory blocks.
     
     /**
      * @brief Callback type for handling allocation failures.
      * @param requested_size Size of the failed allocation request.
      * @param user_data User-provided data for the callback.
      * @return Pointer to memory if recovery is successful, nullptr otherwise.
      */
     using AllocationFailureHandler = void* (*)(size_t requested_size, void* user_data);
 
     
     /**
      * @brief Callback type for memory allocation during pool resizing.
      * @param current_pool Pointer to the current pool being resized.
      * @param current_size Current size of the pool in bytes.
      * @param requested_size Requested new size of the pool in bytes.
      * @param user_data User-provided data for context.
      * @return Pointer to the new memory block if successful, nullptr otherwise.
      */
     using ResizeAllocationHandler = void* (*)(void* current_pool, size_t current_size, size_t requested_size, void* user_data);
 
 
    public:
     static constexpr size_t MAX_POOL = 5; ///< Maximum number of memory pools allowed.
                                           /**
                                            * @brief Policy for pool allocation.
                                            */
     enum class Policy {
         DEFAULT_POLICY,          ///< Default policy.
         CRITICAL_ONLY,    ///< For critical tasks only.
         FAST_ACCESS,      ///< Optimized for fast access.
         LOW_FRAGMENTATION ///< Optimized for low fragmentation.
     };
 
     /**
      * @brief Pool configuration for advanced memory pool management.
      */
     struct PoolConfig
     {
         size_t min_block_size;      ///< Minimum block size for allocations in this pool (reduces
                                     ///< fragmentation).
         size_t preferred_alignment; ///< Preferred alignment for allocations in this pool.
         int priority;  ///< Priority for pool selection (higher value = higher priority).
         Policy policy; ///< Allocation policy for this pool.
         PoolConfig()
         {
             this->priority = 0;
             this->min_block_size = tlsf::min_block_size();
             this->preferred_alignment = tlsf::align_size();
             this->policy = Policy::DEFAULT_POLICY;
         }
         PoolConfig(int priority)
         {
             this->priority = priority;
             this->min_block_size = tlsf::min_block_size();
             this->preferred_alignment = tlsf::align_size();
             this->policy = Policy::DEFAULT_POLICY;
         }
         PoolConfig(int priority, size_t min_block_size, size_t preferred_alignment)
         {
             this->priority = priority;
             this->min_block_size = min_block_size;
             this->preferred_alignment = preferred_alignment;
             this->policy = Policy::DEFAULT_POLICY;
         }
         PoolConfig(int priority, size_t min_block_size, size_t preferred_alignment, Policy policy)
         {
             this->priority = priority;
             this->min_block_size = min_block_size;
             this->preferred_alignment = preferred_alignment;
             this->policy = policy;
         }
     };
 
      private:
     Control controls[MAX_POOL];        ///< TLSF control structure.
     void* memory_pools[MAX_POOL];      ///< Array of memory pool pointers.
     size_t pool_sizes[MAX_POOL];       ///< store all pool sizes
     PoolConfig pool_configs[MAX_POOL]; ///< Array of pool configurations.
     size_t pool_count = 0;             ///< Number of active pools.
     bool initialised = false;          ///< Flag indicating if the allocator is initialized.
 #if !EALLOC_NO_LOCKING
     elock::ILockable* lock_ = nullptr;
     elock::ILockable* pool_locks_[MAX_POOL] = {nullptr};
 #endif
     AllocationFailureHandler failure_handler_ = nullptr;
     void* failure_handler_data_ = nullptr;
     ResizeAllocationHandler resize_handler_ = nullptr;
     void* resize_handler_data_ = nullptr;
     bool auto_defragment_ = false;      ///< Flag indicating if auto-defragmentation is enabled.
     double defragment_threshold_ = 0.7; ///< Fragmentation threshold for auto-defragmentation.
     size_t alloc_count_ =
         0; ///< Counter for malloc calls to control auto-defragmentation frequency.
     bool usePerPoolLocking_ = false; // Flag to toggle between global and per-pool locking
 #if defined(EALLOC_ENABLE_OWNERSHIP_TAG) && EALLOC_ENABLE_OWNERSHIP_TAG
     uint32_t ownership_tag_ = 0; ///< Default ownership tag for new allocations.
 #endif
 
 public:
 
     /**
      * @brief Walks through the blocks in a pool with a specified walker function.
      * @param pool Pointer to the pool to walk.
      * @param walker Function to apply to each block.
      * @param user User-defined data passed to the walker.
      */
     void walk_pool(void* pool, Walker walker, void* user);
 
     /**
      * @brief Allocates raw memory of a specified size without constructing an object.
      * @param size Size of memory to allocate in bytes.
      * @param priority Optional priority for pool selection (-1 for default).
      * @param policy Optional policy for pool selection.
      * @return Pointer to the allocated raw memory, or nullptr if allocation fails.
      */
     void* allocate_raw(size_t size, int priority = -1, Policy policy = Policy::DEFAULT_POLICY)
     {
         void* memory = malloc(size, priority, policy);
         if(!memory && failure_handler_)
         {
             memory = failure_handler_(size, failure_handler_data_);
         }
         return memory;
     }
 
     /**
      * @brief Template version of allocate_raw for type-safe size calculation.
      * @tparam T The type whose size will be used for allocation.
      * @param priority Optional priority for pool selection (-1 for default).
      * @param policy Optional policy for pool selection.
      * @return Pointer to the allocated raw memory, or nullptr if allocation fails.
      */
     template <typename T>
     void* allocate_raw(int priority = -1, Policy policy = Policy::DEFAULT_POLICY)
     {
         return allocate_raw(sizeof(T), priority, policy);
     }
 
     /**
      * @brief Sets the lock object for thread safety.
      * @param lock Pointer to an ILockable object to use for locking.
      */
     void setLock(elock::ILockable* lock);
 
     /**
      * @brief Constructs an eAlloc instance with an initial memory pool.
      * @param mem Pointer to the initial memory block.
      * @param bytes Size of the initial memory block in bytes.
      */
     explicit eAlloc(void* mem, size_t bytes);
 
     /// @brief Holds integrity check results.
     struct IntegrityResult
     {
         int prev_status;
         int status;
     };
 
 
     /**
      * @brief Reports storage usage statistics for the allocator.
      *
      * This structure provides a summary of memory usage data including:
      * - The total free memory available in bytes.
      * - The size (in bytes) of the largest contiguous free memory block.
      * - The count of free blocks within the allocator.
      * - A fragmentation factor ranging from 0 (no fragmentation) to 1 (high fragmentation).
      *
      * Use this report to monitor the efficiency and fragmentation of the allocator's memory usage.
      */
     struct StorageReport
     {
         size_t totalFreeSpace = 0;        ///< Total free space in bytes.
         size_t largestFreeRegion = 0;     ///< Size of the largest free region in bytes.
         size_t freeBlockCount = 0;        ///< Number of free blocks.
         double fragmentationFactor = 0.0; ///< Fragmentation factor (0 to 1).
         size_t smallestFreeRegion = 0;    ///< Size of the smallest contiguous free region in bytes.
         size_t averageFreeBlockSize = 0;  ///< Average size of free blocks in bytes.
     };
 
     /**
     * @brief Allocates a block of memory of the specified size.
     * @param size The size of the memory block to allocate in bytes.
     * @param priority Optional priority for pool selection (-1 for default).
 
     * @return Pointer to the allocated memory, or nullptr if allocation fails.
     */
     void* malloc(size_t size);
 
     /**
      * @brief Allocates a block of memory of the specified size.
      * @param size The size of the memory block to allocate in bytes.
      * @param priority Optional priority for pool selection (-1 for default).
      * @param policy Optional policy for pool selection.
      * @return Pointer to the allocated memory, or nullptr if allocation fails.
      */
     void* malloc(size_t size, int priority, Policy policy);
 
     /**
      * @brief Frees a previously allocated memory block.
      * @param ptr Pointer to the memory block to free.
      */
     void free(void* ptr);
 
     /**
      * @brief Allocates a memory block with specified alignment and size.
      * @param align The alignment requirement in bytes (must be a power of 2).
      * @param size The size of the memory block to allocate in bytes.
      * @return Pointer to the allocated memory, or nullptr if allocation fails.
      */
     void* memalign(size_t align, size_t size);
 
     /**
      * @brief Reallocates a memory block to a new size.
      * @param ptr Pointer to the memory block to reallocate.
      * @param size The new size of the memory block in bytes.
      * @return Pointer to the reallocated memory, or nullptr if reallocation
      * fails.
      */
     void* realloc(void* ptr, size_t size);
 
     /**
      * @brief Allocates memory for an array and initializes it to zero.
      * @param num Number of elements in the array.
      * @param size Size of each element in bytes.
      * @return Pointer to the allocated memory, or nullptr if allocation fails.
      */
     void* calloc(size_t num, size_t size);
 
     /**
      * @brief Allocates raw memory for an object without constructing it.
      * @tparam T The type of the object for sizing purposes.
      * @return Pointer to the allocated raw memory, or nullptr if allocation fails.
      */
 
     /**
      * @brief Allocates memory for an object and constructs it by copying.
      * @tparam T The type of the object to allocate.
      * @param obj The object to copy-construct.
      * @return Pointer to the constructed object, or nullptr if allocation fails.
      */
     template <typename T>
     T* allocate(const T& obj)
     {
         void* memory = malloc(sizeof(T));
         if(!memory && failure_handler_)
         {
             memory = failure_handler_(sizeof(T), failure_handler_data_);
         }
         if(!memory)
         {
             LOG::ERROR("E_ALLOC", "Memory allocation failed for object.");
             return nullptr;
         }
         return new(memory) T(obj);
     }
 
     /**
      * @brief Allocates memory for an object and constructs it with the provided arguments.
      * @tparam T The type of the object to allocate and construct.
      * @tparam Args Types of the constructor arguments.
      * @param args Arguments to pass to the object's constructor.
      * @return Pointer to the constructed object, or nullptr if allocation fails.
      */
     template <typename T, typename... Args>
     T* allocate(Args&&... args)
     {
         void* memory = allocate_raw(sizeof(T));
         if(!memory && failure_handler_)
         {
             memory = failure_handler_(sizeof(T), failure_handler_data_);
         }
         if(!memory)
         {
             LOG::ERROR("E_ALLOC", "Memory allocation failed for object.");
             return nullptr;
         }
         T* obj = new(memory) T(std::forward<Args>(args)...);
         return obj;
     }
 
     /**
      * @brief Deallocates an object or raw memory and destroys it if applicable.
      * @tparam T The type of the object to deallocate (ignored for raw memory).
      * @param obj Pointer to the object or raw memory to deallocate.
      * @param destroy If true, calls the destructor for the object; if false, treats as raw memory.
      */
     template <typename T>
     void deallocate(T* obj, bool destroy = true)
     {
         if(!obj) return;
         if(destroy)
         {
             obj->~T();
         }
         free(static_cast<void*>(obj));
     }
 
     /**
      * @brief Adds a new memory pool to the allocator.
      * @param mem Pointer to the memory block to add as a pool.
      * @param bytes Size of the memory block in bytes.
      * @param config Configuration for the pool.
      * @return Pointer to the added pool, or nullptr if addition fails.
      */
     void* add_pool(void* mem, size_t bytes, const PoolConfig& config = PoolConfig());
 
     /**
      * @brief Removes a memory pool from the allocator.
      * @param pool Pointer to the pool to remove.
      * @note The pool must contain no allocated blocks; all memory allocated
      *       from it must be freed prior to removal, or undefined behavior
      *       will result.
      */
     void remove_pool(void* pool);
 
     /**
      * @brief Resizes an existing memory pool at runtime.
      * @param pool Pointer to the existing pool memory to resize.
      * @param new_bytes New size of the memory pool in bytes.
      * @return True if resizing was successful, false otherwise.
      */
     bool resize_pool(void* pool, size_t new_bytes);
 
     /**
      * @brief Checks the integrity of a specific memory pool.
      * @param pool Pointer to the pool to check.
      * @return Integrity status (0 if intact, non-zero if issues found).
      */
     int check_pool(void* pool);
 
     /**
      * @brief Retrieves the usable memory pointer of a pool.
      * @param pool Pointer to the pool.
      * @return Pointer to the pool's usable memory.
      */
     void* get_pool(void* pool);
 
     /**
      * @brief Retrieves the pool associated with a given memory block.
      * @param block Pointer to the memory block.
      * @return Pointer to the pool, or nullptr if not found.
      */
     void* get_pool_from_block(const void* block);
 
     /**
      * @brief Checks the overall integrity of the allocator.
      * @return Integrity status (0 if intact, non-zero if issues found).
      */
     int check();
 
     /**
      * @brief Walker function for integrity checks.
      * @param ptr Pointer to the block.
      * @param size Size of the block in bytes.
      * @param used Flag indicating if the block is used (non-zero) or free (0).
      * @param user User-defined data passed to the walker.
      */
     static void integrity_walker(void* ptr, size_t size, int used, void* user);
 
     /**
      * @brief Generates a storage usage report.
      * @return StorageReport containing free space and fragmentation details.
      */
     StorageReport report() const;
 
     /**
      * @brief Logs the storage usage report.
      */
     void logStorageReport() const;
 
     /**
      * @brief Sets a handler for allocation failures to enable error recovery.
      * @param handler Callback function to invoke on allocation failure.
      * @param user_data User data to pass to the callback.
      */
     void setAllocationFailureHandler(AllocationFailureHandler handler, void* user_data = nullptr)
     {
         failure_handler_ = handler;
         failure_handler_data_ = user_data;
     }
 
     /**
      * @brief Sets a callback handler for memory allocation during pool resizing.
      * @param handler Callback function to invoke for allocating new memory during resize.
      * @param user_data User data to pass to the callback.
      */
     void setResizeAllocationHandler(ResizeAllocationHandler handler, void* user_data = nullptr)
     {
         resize_handler_ = handler;
         resize_handler_data_ = user_data;
     }
 
     /**
      * @brief Creates a lock object of the specified type and sets it for the allocator.
      * @tparam LockType The type of lock to create (must implement ILockable).
      * @tparam Args Types of the constructor arguments for the lock.
      * @param autoSet If true, automatically sets the created lock as the allocator's lock.
      * @param args Arguments to pass to the lock's constructor.
      * @return Pointer to the created ILockable object, or nullptr if allocation fails.
      */
     template <typename LockType, typename... Args>
     elock::ILockable* createLock(bool autoSet = true, Args&&... args)
     {
         void* memory = allocate_raw(sizeof(LockType));
         if(!memory)
         {
             LOG::ERROR("E_ALLOC", "Memory allocation failed for lock object.");
             return nullptr;
         }
         LockType* lock = new(memory) LockType(std::forward<Args>(args)...);
         if(autoSet)
         {
             setLock(lock);
         }
         return lock;
     }
 
     /**
      * @brief Destroys and deallocates the lock object previously created by createLock.
      * @param lock Pointer to the ILockable object to destroy.
      * @note This will unset the lock if it is the current lock of the allocator.
      */
     void destroyLock(elock::ILockable* lock)
     {
         if(!lock) return;
         if(lock_ == lock)
         {
             setLock(nullptr);
         }
         // Call destructor explicitly
         lock->~ILockable();
         free(static_cast<void*>(lock));
     }
 
     /**
      * @brief Attempts to reduce memory fragmentation by merging adjacent free blocks.
      * @return The number of free blocks merged during the defragmentation process.
      */
     size_t defragment();
 
     /**
      * @brief Enables or disables automatic defragmentation when fragmentation exceeds a threshold.
      * @param enable Whether to enable auto-defragmentation.
      * @param threshold Fragmentation factor threshold (0.0 to 1.0) above which defragmentation is
      * triggered. Default is 0.7.
      */
     void setAutoDefragment(bool enable, double threshold = 0.7);
 
     /**
      * @brief Enable or disable per-pool locking to customize locking granularity.
      *        When enabled, operations on specific pools will use the corresponding pool lock if
      * set, reducing contention compared to using a global lock for all operations.
      * @param enable True to use per-pool locks when available, false to use global lock.
      */
     void setPerPoolLocking(bool enable);
 
     /**
      * @brief Set a lock for a specific memory pool.
      * @param poolIndex Index of the pool (0 to MAX_POOL-1).
      * @param lock Pointer to the lock object for this pool.
      */
     void setLockForPool(size_t poolIndex, elock::ILockable* lock);
 
     /**
      * @brief Get the index of a pool from its memory address.
      * @param pool Pointer to the pool memory.
      * @return Index of the pool, or MAX_POOL if not found.
      */
     size_t get_pool_index(void* pool) const;
 
 #if defined(EALLOC_ENABLE_OWNERSHIP_TAG) && EALLOC_ENABLE_OWNERSHIP_TAG
     /**
      * @brief Sets the ownership tag for allocated memory blocks.
      * @param tag 32-bit tag representing the owner (e.g., task or thread ID).
      */
     void setOwnershipTag(uint32_t tag) { ownership_tag_ = tag; }
 
     /**
      * @brief Gets the current ownership tag used for new allocations.
      * @return 32-bit tag representing the owner.
      */
     uint32_t getOwnershipTag() const { return ownership_tag_; }
 #endif
 
   
 };
 
 } // namespace dsa
 
 /**
  * @def EALLOC_NO_LOCKING
  * @brief Compile-time option to disable all locking mechanisms for single-threaded applications.
  *        Define this macro (e.g., via compiler flag -DEALLOC_NO_LOCKING=1) to exclude locking code,
  *        improving performance in environments where thread safety is not required.
  *        WARNING: Using this option in a multi-threaded environment will result in data races and
  * undefined behavior. Usage: Add -DEALLOC_NO_LOCKING=1 to your compiler flags or define in build
  * system (e.g., PlatformIO).
  */
 #ifndef EALLOC_NO_LOCKING
     #define EALLOC_NO_LOCKING 0
 #endif
 
 #ifndef EALLOC_NO_OWNERSHIP_CHECKING
     #define EALLOC_NO_OWNERSHIP_CHECKING 0
 #endif
 
 #ifndef EALLOC_ENABLE_OWNERSHIP_TAG
     #define EALLOC_ENABLE_OWNERSHIP_TAG 0
 #endif
 
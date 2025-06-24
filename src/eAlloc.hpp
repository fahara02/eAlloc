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
   public:
    static constexpr size_t MAX_POOL = 5; ///< Maximum number of memory pools allowed.

    /**
     * @brief Allocates raw memory of a specified size without constructing an object.
     * @param size Size of memory to allocate in bytes.
     * @return Pointer to the allocated raw memory, or nullptr if allocation fails.
     */
    void* allocate_raw(size_t size)
    {
        return malloc(size);
    }

    /**
     * @brief Template version of allocate_raw for type-safe size calculation.
     * @tparam T The type whose size will be used for allocation.
     * @return Pointer to the allocated raw memory, or nullptr if allocation fails.
     */
    template <typename T>
    void* allocate_raw()
    {
        return allocate_raw(sizeof(T));
    }

    /**
     * @brief Sets the lock object for thread safety.
     * @param lock Pointer to an ILockable object to use for locking.
     */
    void setLock(elock::ILockable* lock)
    {
        lock_ = lock;
    }

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

    using tlsf = dsa::TLSF<5>;             ///< TLSF allocator with 32 second-level lists.
    using Control = tlsf::Control;         ///< TLSF control structure type.
    using BlockHeader = tlsf::BlockHeader; ///< TLSF block header type.
    using Walker = tlsf::tlsf_walker;      ///< Function type for walking memory blocks.

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
    };

    /**
     * @brief Allocates a block of memory of the specified size.
     * @param size The size of the memory block to allocate in bytes.
     * @return Pointer to the allocated memory, or nullptr if allocation fails.
     */
    void* malloc(size_t size);

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
        if(!memory)
        {
            LOG::ERROR("E_ALLOC", "Memory allocation failed for object.");
            return nullptr;
        }
        try
        {
            return new(memory) T(obj);
        }
        catch(...)
        {
            free(memory);
            LOG::ERROR("E_ALLOC", "Object construction failed.");
            return nullptr;
        }
    }

    /**
     * @brief Allocates memory for an object and constructs it with arguments.
     * @tparam T The type of the object to allocate.
     * @tparam Args Types of the constructor arguments.
     * @param args Arguments to pass to the object's constructor.
     * @return Pointer to the constructed object, or nullptr if allocation fails.
     */
    template <typename T, typename... Args>
    T* allocate(Args&&... args)
    {
        void* memory = malloc(sizeof(T));
        if(!memory)
        {
            LOG::ERROR("E_ALLOC", "Memory allocation failed for object.");
            return nullptr;
        }
        try
        {
            return new(memory) T(std::forward<Args>(args)...);
        }
        catch(...)
        {
            free(memory);
            LOG::ERROR("E_ALLOC", "Object construction failed.");
            return nullptr;
        }
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
     * @return Pointer to the added pool, or nullptr if addition fails.
     */
    void* add_pool(void* mem, size_t bytes);

    /**
     * @brief Removes a memory pool from the allocator.
     * @param pool Pointer to the pool to remove.
     * @note The pool must contain no allocated blocks; all memory allocated
     *       from it must be freed prior to removal, or undefined behavior
     *       will result.
     */
    void remove_pool(void* pool);

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
        if (!memory)
        {
            LOG::ERROR("E_ALLOC", "Memory allocation failed for lock object.");
            return nullptr;
        }
        try
        {
            LockType* lock = new (memory) LockType(std::forward<Args>(args)...);
            if (autoSet)
            {
                setLock(lock);
            }
            return lock;
        }
        catch (...)
        {
            free(memory);
            LOG::ERROR("E_ALLOC", "Lock object construction failed.");
            return nullptr;
        }
    }
    
    /**
     * @brief Destroys and deallocates the lock object previously created by createLock.
     * @param lock Pointer to the ILockable object to destroy.
     * @note This will unset the lock if it is the current lock of the allocator.
     */
    void destroyLock(elock::ILockable* lock)
    {
        if (!lock) return;
        if (lock_ == lock)
        {
            setLock(nullptr);
        }
        // Call destructor explicitly
        lock->~ILockable();
        free(static_cast<void*>(lock));
    }

   private:
    Control control;              ///< TLSF control structure.
    void* memory_pools[MAX_POOL]; ///< Array of memory pool pointers.
    size_t pool_sizes[MAX_POOL];  ///< store all pool sizes
    size_t pool_count = 0;        ///< Number of active pools.
    bool initialised = false;     ///< Flag indicating if the allocator is initialized.
    elock::ILockable* lock_ = nullptr;
    /**
     * @brief Walks through the blocks in a pool with a specified walker function.
     * @param pool Pointer to the pool to walk.
     * @param walker Function to apply to each block.
     * @param user User-defined data passed to the walker.
     */
    void walk_pool(void* pool, Walker walker, void* user);
};

} // namespace dsa
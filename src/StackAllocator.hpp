#pragma once
#include "eAlloc.hpp"
#include <cstddef>
#include <stdexcept>
#include <utility> // Added for std::pair

namespace dsa
{

/**
 * @file StackAllocator.hpp
 * @brief MCU/host-agnostic C++ allocator using a stack-allocated memory pool and TLSF.
 *
 * This allocator is suitable for embedded and host builds, provides minimal STL usage,
 * and supports optional thread safety via elock::ILockable. It is intended for use with
 * STL containers or custom allocation needs where a fixed-size stack buffer is desired.
 */

/**
 * @brief Standard C++ allocator interface using a stack-allocated buffer and TLSF.
 *
 * @tparam T Type of object to allocate.
 * @tparam PoolSize Size of the internal stack buffer in bytes.
 *
 * Usage:
 *   dsa::StackAllocator<int, 1024> alloc; // No lock
 *   std::vector<int, dsa::StackAllocator<int, 1024>> v(alloc);
 *
 * Thread Safety:
 *   - Not thread-safe by default.
 *   - Users can provide a lock instance via the constructor or setLock() method for thread safety.
 *   - No STL bloat for MCU: lock is optional and only used if specified.
 */
template <typename T, size_t PoolSize>
class StackAllocator
{
   public:
    /** @name Standard C++ allocator typedefs */
    ///@{
    using value_type = T;              ///< Value type
    using pointer = T*;                ///< Pointer type
    using const_pointer = const T*;    ///< Const pointer type
    using reference = T&;              ///< Reference type
    using const_reference = const T&;  ///< Const reference type
    using size_type = size_t;          ///< Size type
    using difference_type = ptrdiff_t; ///< Difference type
    ///@}

    /**
     * @brief Rebind template for allocator compatibility with different types.
     * @tparam U The new type to allocate.
     */
    template <typename U>
    struct rebind
    {
        using other = StackAllocator<U, PoolSize>;
    };

    /**
     * @brief Constructor initializes the stack allocator with a fixed-size buffer.
     */
    StackAllocator() : allocator(memoryPool, PoolSize)
    {
        // Use setLock or the lock constructor if thread safety is needed
    }

    /**
     * @brief Constructor initializes the stack allocator with a specific lock instance.
     * @param lock Pointer to a lock instance to be used for thread safety.
     */
    StackAllocator(elock::ILockable* lock) : allocator(memoryPool, PoolSize)
    {
        allocator.setLock(lock);
    }

    /**
     * @brief Copy constructor for allocator propagation (required by STL).
     * @tparam U Other type for rebind.
     */
    template <typename U>
    StackAllocator(const StackAllocator<U, PoolSize>&) : allocator(memoryPool, PoolSize)
    {
        // Use setLock or the lock constructor if thread safety is needed
    }

    /**
     * @brief Allocates uninitialized storage for n objects of type T.
     * @param n Number of objects to allocate.
     * @return Pointer to the allocated storage.
     * @throws std::bad_alloc if allocation fails.
     */
    pointer allocate(size_type n)
    {
        void* ptr = allocator.malloc(n * sizeof(T));
        if(!ptr)
        {
            throw std::bad_alloc();
        }
        return static_cast<pointer>(ptr);
    }

    /**
     * @brief Deallocates storage previously allocated by this allocator.
     * @param p Pointer to the memory to deallocate.
     * @note The number of objects parameter is present for interface compliance
     *       but is not used by this specific deallocation function.
     */
    void deallocate(pointer p, size_type /*n*/) { allocator.free(p); }

    /**
     * @brief Constructs an object of type T at the given memory location.
     * @param p Pointer to memory.
     * @param val Value to copy-construct.
     */
    void construct(pointer p, const_reference val) { new(p) T(val); }

    /**
     * @brief Destroys an object of type T at the given memory location.
     * @param p Pointer to object to destroy.
     */
    void destroy(pointer p) { p->~T(); }

    /**
     * @brief Equality operator: compares based on memory pool identity.
     * @param other Another StackAllocator instance.
     * @return True if using the same memory pool.
     */
    bool operator==(const StackAllocator& other) const { return memoryPool == other.memoryPool; }

    /**
     * @brief Inequality operator.
     * @param other Another StackAllocator instance.
     * @return True if not using the same memory pool.
     */
    bool operator!=(const StackAllocator& other) const { return !(*this == other); }

    /**
     * @brief Sets a lock for thread safety (optional, MCU/host agnostic).
     * @param lock Pointer to a elock::ILockable mutex adapter.
     *
     * If not set, the allocator is not thread-safe. 
     * For host builds, use elock::StdMutex.
     * For embedded, use the appropriate platform adapter. Locking is delegated to eAlloc.
     */
    void setLock(elock::ILockable* lock) { allocator.setLock(lock); }

    /**
     * @brief Sets a failure handler for allocation errors.
     * @param handler Function to call when allocation fails.
     * @param user_data User data to pass to the handler.
     */
    void setFailureHandler(void* (*handler)(size_t, void*), void* user_data)
    {
        allocator.setAllocationFailureHandler(handler, user_data);
    }

    /**
     * @brief Retrieves storage usage statistics.
     * @return StorageReport structure containing memory usage details.
     */
    dsa::eAlloc::StorageReport getStorageReport() const
    {
        return allocator.report();
    }

    
    /**
     * @brief Logs storage usage statistics .
     
     */
     void logStorageReport()const
     {
         return allocator.logStorageReport();
     }

    /**
     * @brief Configures auto-defragmentation.
     * @param enable Whether to enable auto-defragmentation.
     * @param threshold Fragmentation threshold for triggering defragmentation.
     */
    void configureDefragmentation(bool enable, double threshold)
    {
        allocator.setAutoDefragment(enable, threshold);
    }

    /**
     * @brief Manually triggers defragmentation of the memory pool.
     */
    void defragment()
    {
        allocator.defragment();
    }

   private:
    char memoryPool[PoolSize]; ///< Stack-allocated memory pool
    dsa::eAlloc allocator;     ///< Internal TLSF allocator managing the pool
};

} // namespace dsa
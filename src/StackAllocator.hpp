#pragma once
#include "eAlloc.hpp"
#include <cstddef>
#include <stdexcept>

namespace dsa {

template <typename T, size_t PoolSize> class StackAllocator {
public:
  // Standard C++ allocator required types
  using value_type = T;
  using pointer = T *;
  using const_pointer = const T *;
  using reference = T &;
  using const_reference = const T &;
  using size_type = size_t;
  using difference_type = ptrdiff_t;

  // Rebind template for allocator compatibility with different types
  template <typename U> struct rebind {
    using other = StackAllocator<U, PoolSize>;
  };

  // Constructor initializes eAlloc with the stack-allocated memory pool
  StackAllocator() : allocator(memoryPool, PoolSize) {}

  // Copy constructor (for allocator propagation)
  template <typename U>
  StackAllocator(const StackAllocator<U, PoolSize> &)
      : allocator(memoryPool, PoolSize) {}

  // Allocate memory for n objects of type T
  pointer allocate(size_type n) {
    void *ptr = allocator.malloc(n * sizeof(T));
    if (!ptr) {
      throw std::bad_alloc();
    }
    return static_cast<pointer>(ptr);
  }

  // Deallocate memory (size_type n is unused but required by the interface)
  void deallocate(pointer p, size_type /*n*/) { allocator.free(p); }

  // Construct an object at the given pointer with value
  void construct(pointer p, const_reference val) { new (p) T(val); }

  // Destroy an object at the given pointer
  void destroy(pointer p) { p->~T(); }

  // Equality operators (compare based on memory pool identity)
  bool operator==(const StackAllocator &other) const {
    return memoryPool == other.memoryPool;
  }

  bool operator!=(const StackAllocator &other) const {
    return !(*this == other);
  }

private:
  char memoryPool[PoolSize]; // Stack-allocated memory pool
  dsa::eAlloc allocator;     // Manages the memory pool using TLSF
};

} // namespace dsa
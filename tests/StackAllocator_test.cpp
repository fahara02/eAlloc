#include "gtest/gtest.h"
#include "StackAllocator.hpp"
#include <vector>

struct StackTestObj {
    int x;
    float y;
    StackTestObj(int x_, float y_) : x(x_), y(y_) {}
};

TEST(StackAllocatorTest, AllocateDeallocateSingleObject) {
    dsa::StackAllocator<StackTestObj, 256> alloc;
    StackTestObj* obj = alloc.allocate(1);
    ASSERT_NE(obj, nullptr);
    alloc.construct(obj, StackTestObj{42, 3.14f});
    EXPECT_EQ(obj->x, 42);
    EXPECT_FLOAT_EQ(obj->y, 3.14f);
    alloc.destroy(obj);
    alloc.deallocate(obj, 1);
}

TEST(StackAllocatorTest, AllocateVectorWithStackAllocator) {
    using MyAlloc = dsa::StackAllocator<int, 512>;
    std::vector<int, MyAlloc> v;
    v.reserve(10);
    for (int i = 0; i < 10; ++i) v.push_back(i * 2);
    for (int i = 0; i < 10; ++i) EXPECT_EQ(v[i], i * 2);
}

TEST(StackAllocatorTest, OutOfMemoryThrows) {
    dsa::StackAllocator<int, 16> alloc; // Small pool
    EXPECT_THROW({
        int* arr = alloc.allocate(100); // Should throw
        (void)arr;
    }, std::bad_alloc);
}

// Optional: Thread safety test (host only)
#if defined(EALLOC_PC_HOST)
#include "globalLock.hpp"
#include <mutex>
TEST(StackAllocatorTest, ThreadSafeLock) {
    dsa::StackAllocator<int, 128> alloc;
    std::timed_mutex raw_mtx;
    elock::StdMutex mtx(raw_mtx);
    alloc.setLock(&mtx);
    int* p = alloc.allocate(1);
    *p = 123;
    alloc.deallocate(p, 1);
}
#endif

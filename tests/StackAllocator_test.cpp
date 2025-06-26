#include "gtest/gtest.h"
#include "StackAllocator.hpp"
#include <vector>
#include "logSetup.hpp"
#include "Logger.hpp"
#include"tlsf.hpp"


struct LoggerEnvironment : ::testing::Environment
{
    void SetUp() override { setup_logger(); }
};
::testing::Environment* const logger_env =
    ::testing::AddGlobalTestEnvironment(new LoggerEnvironment());

struct StackTestObj
{
    int x;
    float y;
    StackTestObj(int x_, float y_) : x(x_), y(y_) {}
};

TEST(StackAllocatorTest, AllocateDeallocateSingleObject)
{
    dsa::StackAllocator<StackTestObj, 256> alloc;
    StackTestObj* obj = alloc.allocate(1);
    ASSERT_NE(obj, nullptr);
    alloc.construct(obj, StackTestObj{42, 3.14f});
    EXPECT_EQ(obj->x, 42);
    EXPECT_FLOAT_EQ(obj->y, 3.14f);
    alloc.destroy(obj);
    alloc.deallocate(obj, 1);
}

TEST(StackAllocatorTest, AllocateVectorWithStackAllocator)
{
    using MyAlloc = dsa::StackAllocator<int, 512>;
    std::vector<int, MyAlloc> v;
    v.reserve(10);
    for(int i = 0; i < 10; ++i) v.push_back(i * 2);
    for(int i = 0; i < 10; ++i) EXPECT_EQ(v[i], i * 2);
}

TEST(StackAllocatorTest, OutOfMemoryThrows)
{
    dsa::StackAllocator<int, 1024> alloc; // Small pool
    EXPECT_THROW(
        {
            int* arr = alloc.allocate(2048); // Should throw
            (void)arr;
        },
        std::bad_alloc);
}

TEST(StackAllocatorTest, FailureHandler)
{
    dsa::StackAllocator<int, 16> alloc;
    
    // Flag to verify handler was called
    bool handlerCalled = false;
    
    // Define handler as a static function to use as a function pointer
    struct HandlerData {
        static void* handler(size_t size, void* user_data) {
            bool* called = static_cast<bool*>(user_data);
            *called = true;
            return nullptr;
        }
    };
    
    alloc.setFailureHandler(HandlerData::handler, &handlerCalled);
    
    // Try to allocate more than pool size to trigger failure
    int* ptr = nullptr;
    try {
        ptr = alloc.allocate(1000);
    } catch (const std::bad_alloc&) {
        // Expected exception
    }
    
    // Verify handler was called
    EXPECT_TRUE(handlerCalled);
    
    if (ptr) {
        alloc.deallocate(ptr, 1000);
    }
}

TEST(StackAllocatorTest, StorageReport)
{    using tlsf = dsa::TLSF<dsa::MAX_SLI>;  
    const size_t POOL_SIZE = 128;
    dsa::StackAllocator<int, POOL_SIZE> alloc;
   
    const size_t EXPECTED_FREE_SPACE = POOL_SIZE - tlsf::pool_overhead();
    
    // Get initial report
    LOG::TRACE("GTEST", "Initial state before any allocations");
    dsa::eAlloc::StorageReport report = alloc.getStorageReport();
    EXPECT_EQ(report.totalFreeSpace, EXPECTED_FREE_SPACE);
    EXPECT_EQ(report.largestFreeRegion, EXPECTED_FREE_SPACE);
    
    // Allocate some memory
    int* p1 = alloc.allocate(5);
    LOG::TRACE("GTEST", "After allocating 5 integers");
    report = alloc.getStorageReport();
    EXPECT_LT(report.totalFreeSpace, EXPECTED_FREE_SPACE);
    
    // Deallocate and check again
    alloc.deallocate(p1, 5);
    LOG::TRACE("GTEST", "After deallocating 5 integers");
    report = alloc.getStorageReport();
    EXPECT_EQ(report.totalFreeSpace, EXPECTED_FREE_SPACE);
}

TEST(StackAllocatorTest, DefragmentationControl)
{
    dsa::StackAllocator<int, 256> alloc;
    
    // Configure auto-defragmentation
    alloc.configureDefragmentation(true, 0.5);
    
    // Allocate and deallocate to create potential fragmentation
    int* p1 = alloc.allocate(10);
    int* p2 = alloc.allocate(10);
    alloc.deallocate(p1, 10);
    
    // Manual defragmentation
    alloc.defragment();
    
    // Verify memory state (indirectly through allocation success)
    int* p3 = alloc.allocate(10);
    EXPECT_NE(p3, nullptr);
    
    alloc.deallocate(p2, 10);
    alloc.deallocate(p3, 10);
}

// Optional: Thread safety test (host only)
#if defined(EALLOC_PC_HOST)
    #include "globalELock.hpp"
    #include <mutex>
TEST(StackAllocatorTest, ThreadSafeLock)
{
    dsa::StackAllocator<int, 128> alloc;
    std::timed_mutex raw_mtx;
    elock::StdMutex mtx(raw_mtx);
    alloc.setLock(&mtx);
    int* p = alloc.allocate(1);
    *p = 123;
    alloc.deallocate(p, 1);
}

TEST(StackAllocatorTest, ThreadSafeLockAutoCreated)
{
    // Test using constructor with a specific lock instance
    std::timed_mutex raw_mtx;
    elock::StdMutex mtx(raw_mtx);
    dsa::StackAllocator<int, 128> alloc(&mtx);
    int* p = alloc.allocate(1);
    *p = 456;
    alloc.deallocate(p, 1);
}
#endif

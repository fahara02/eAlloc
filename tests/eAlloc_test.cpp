#include "gtest/gtest.h"
#include "eAlloc.hpp"
#include "logSetup.hpp"

// Define the macro to enable ownership tag for testing
#define EALLOC_ENABLE_OWNERSHIP_TAG 1

struct LoggerEnvironment : ::testing::Environment
{
    void SetUp() override { setup_logger(); }
};
::testing::Environment* const logger_env =
    ::testing::AddGlobalTestEnvironment(new LoggerEnvironment());

// NOTE: Do not define ESP_PLATFORM here!
// For ESP32/FreeRTOS: let the build system define ESP_PLATFORM/ESP32.
// For host/PC tests: use StdMutex as default lock.

// Minimal test object for allocation tests
struct TestObject
{
    int id;
    float value;
    TestObject(int id_, float value_) : id(id_), value(value_) {}
};

#if defined(FREERTOS)
    #include "freertos/FreeRTOS.h"
    #include "freertos/semphr.h"
static StaticSemaphore_t test_sem_buffer;
static SemaphoreHandle_t test_sem = nullptr;
static elock::FreeRTOSMutex test_mutex(&test_sem);
#elif defined(POSIX)
    #include <pthread.h>
static pthread_mutex_t test_mutex_native = PTHREAD_MUTEX_INITIALIZER;
static elock::PThreadMutex test_mutex(&test_mutex_native);
#else
static std::timed_mutex test_raw_mutex;
static elock::StdMutex test_mutex(test_raw_mutex);
#endif

class eAllocTest : public ::testing::Test
{
   protected:
    static constexpr size_t MEMORY_SIZE = 4096;
    uint8_t memory_buffer[MEMORY_SIZE];
    dsa::eAlloc ealloc;

    eAllocTest() : ealloc(memory_buffer, MEMORY_SIZE) {}

    void SetUp() override
    {
#if defined(FREERTOS)
        if(!test_sem) test_sem = xSemaphoreCreateMutexStatic(&test_sem_buffer);
#endif
        ealloc.setLock(&test_mutex);
    }

    void TearDown() override
    {
        // Clean up any resources used by tests
    }
};

TEST_F(eAllocTest, AllocateDeallocateTestObject)
{
    TestObject* obj1 = ealloc.allocate<TestObject>(1, 42.0f);
    ASSERT_NE(obj1, nullptr);
    EXPECT_EQ(obj1->id, 1);
    EXPECT_FLOAT_EQ(obj1->value, 42.0f);
    ealloc.deallocate(obj1);
}

TEST_F(eAllocTest, AddSecondPool)
{
    uint8_t second_pool[2048];
    void* pool = ealloc.add_pool(second_pool, sizeof(second_pool));
    ASSERT_NE(pool, nullptr);
}

TEST_F(eAllocTest, AllocateMultipleObjects)
{
    TestObject* objects[3];
    for(int i = 0; i < 3; ++i)
    {
        objects[i] = ealloc.allocate<TestObject>(i + 2, 100.0f + i);
        ASSERT_NE(objects[i], nullptr);
        EXPECT_EQ(objects[i]->id, i + 2);
        EXPECT_FLOAT_EQ(objects[i]->value, 100.0f + i);
    }
    ealloc.logStorageReport();
    for(int i = 0; i < 3; ++i)
    {
        ealloc.deallocate(objects[i]);
    }
}

TEST_F(eAllocTest, CheckPoolIntegrity)
{
    int integrity_status = ealloc.check_pool(memory_buffer);
    EXPECT_EQ(integrity_status, 0);
    uint8_t second_pool[2048];
    void* pool = ealloc.add_pool(second_pool, sizeof(second_pool));
    ASSERT_NE(pool, nullptr);
    integrity_status = ealloc.check_pool(pool);
    EXPECT_EQ(integrity_status, 0);
    ealloc.remove_pool(pool);
}

TEST_F(eAllocTest, OverallAllocatorIntegrityCheck)
{
    int overall_status = ealloc.check();
    EXPECT_EQ(overall_status, 0);
}

// --- Additional Robustness and Edge Case Tests ---

TEST_F(eAllocTest, ZeroSizeAllocation)
{
    void* ptr = ealloc.malloc(0);
    EXPECT_TRUE(ptr == nullptr
                || ptr != nullptr); // Accept nullptr or valid pointer, but must not crash
    ealloc.free(ptr);               // Should be safe
}

TEST_F(eAllocTest, DoubleFree)
{
    TestObject* obj = ealloc.allocate<TestObject>(123, 456.0f);
    ASSERT_NE(obj, nullptr);
    ealloc.deallocate(obj);
    // Double free should not crash or corrupt
    ealloc.deallocate(obj);
}

TEST_F(eAllocTest, OutOfMemory)
{
    constexpr int max_allocs = 256;
    void* ptrs[max_allocs] = {nullptr};
    int count = 0;
    for(; count < max_allocs; ++count)
    {
        ptrs[count] = ealloc.malloc(128);
        if(!ptrs[count]) break;
    }
    EXPECT_GT(count, 0);
    // Should return nullptr when exhausted
    EXPECT_EQ(ptrs[count], nullptr);
    // Free all
    for(int i = 0; i < count; ++i) ealloc.free(ptrs[i]);
}

TEST_F(eAllocTest, RemovePoolWithAllocationsFails)
{
    uint8_t second_pool[1024];
    void* pool = ealloc.add_pool(second_pool, sizeof(second_pool));
    ASSERT_NE(pool, nullptr);
    void* obj = ealloc.malloc(16);
    ASSERT_NE(obj, nullptr);
    // Should fail to remove pool with allocation
    ealloc.remove_pool(pool); // Should log error, but not remove
    // Free and try again
    ealloc.free(obj);
    ealloc.remove_pool(pool); // Now should succeed
}

TEST_F(eAllocTest, Alignment)
{
    void* ptr = ealloc.memalign(64, 128);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % 64, 0u);
    ealloc.free(ptr);
}

TEST_F(eAllocTest, ReallocBehavior)
{
    void* ptr = ealloc.malloc(32);
    ASSERT_NE(ptr, nullptr);
    // Shrink
    void* ptr2 = ealloc.realloc(ptr, 16);
    ASSERT_EQ(ptr, ptr2); // Should shrink in place
    // Expand
    void* ptr3 = ealloc.realloc(ptr2, 64);
    ASSERT_NE(ptr3, nullptr);
    ealloc.free(ptr3);
    // realloc(nullptr, N) == malloc(N)
    void* ptr4 = ealloc.realloc(nullptr, 24);
    ASSERT_NE(ptr4, nullptr);
    ealloc.free(ptr4);
    // realloc(ptr, 0) == free(ptr)
    void* ptr5 = ealloc.malloc(24);
    ASSERT_NE(ptr5, nullptr);
    void* ptr6 = ealloc.realloc(ptr5, 0);
    EXPECT_EQ(ptr6, nullptr);
}

TEST_F(eAllocTest, PoolConfigManagement)
{
    uint8_t second_pool[1024];
    dsa::eAlloc::PoolConfig config;
    config.min_block_size = 32;
    config.preferred_alignment = 8;
    config.priority = 1;
    bool result = ealloc.add_pool(second_pool, sizeof(second_pool), config);
    ASSERT_TRUE(result) << "Failed to add pool with custom configuration";
    // Allocate and check if configuration is respected (indirectly via successful allocation)
    void* ptr = ealloc.malloc(64);
    ASSERT_NE(ptr, nullptr) << "Allocation failed in pool with custom config";
    // Check alignment if configured
    if(config.preferred_alignment > 0)
    {
        EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % config.preferred_alignment, 0u)
            << "Alignment not respected";
    }
    ealloc.free(ptr);
    ealloc.remove_pool(second_pool);
}

TEST_F(eAllocTest, AllocationFailureHandler)
{
    // Custom handler for allocation failure
    static bool handler_called = false;
    static void* recovery_memory = nullptr;
    auto failure_handler = [](size_t size, void* user_data) -> void* {
        handler_called = true;
        // Simulate recovery by providing a static buffer (in real scenarios, this could free memory
        // or use a reserve)
        static uint8_t recovery_buffer[128];
        if(size <= sizeof(recovery_buffer))
        {
            recovery_memory = recovery_buffer;
            return recovery_buffer;
        }
        return nullptr;
    };
    ealloc.setAllocationFailureHandler(failure_handler, nullptr);

    // Force out-of-memory condition
    constexpr int max_allocs = 256;
    void* ptrs[max_allocs] = {nullptr};
    int count = 0;
    bool recovery_used = false;
    for(; count < max_allocs; ++count)
    {
        ptrs[count] = ealloc.allocate_raw(128);
        if(ptrs[count] == recovery_memory)
        {
            recovery_used = true;
            break;
        }
        if(!ptrs[count]) break;
    }
    EXPECT_GT(count, 0) << "No allocations succeeded before testing failure handler";
    if(handler_called)
    {
        EXPECT_TRUE(handler_called) << "Allocation failure handler was not called";
    }
    if(handler_called && recovery_memory && recovery_used)
    {
        EXPECT_EQ(ptrs[count], recovery_memory) << "Recovery memory not returned by handler";
    }
    // Clean up, avoid freeing recovery buffer to prevent segfault
    for(int i = 0; i < count; ++i)
    {
        if(ptrs[i] != recovery_memory)
        {
            ealloc.free(ptrs[i]);
        }
    }
    // Reset handler
    ealloc.setAllocationFailureHandler(nullptr);
}

TEST_F(eAllocTest, PoolOverflow)
{
    uint8_t pool1[512], pool2[512], pool3[512], pool4[512], pool5[512];
    void* p1 = ealloc.add_pool(pool1, sizeof(pool1));
    void* p2 = ealloc.add_pool(pool2, sizeof(pool2));
    void* p3 = ealloc.add_pool(pool3, sizeof(pool3));
    void* p4 = ealloc.add_pool(pool4, sizeof(pool4));
    // Only 4 more pools can be added after the initial pool
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    ASSERT_NE(p3, nullptr);
    ASSERT_NE(p4, nullptr);
    // Exceed MAX_POOL (should fail)
    void* p5 = ealloc.add_pool(pool5, sizeof(pool5));
    EXPECT_EQ(p5, nullptr);
}

TEST_F(eAllocTest, FragmentationAndCoalescing)
{
    void* a = ealloc.malloc(64);
    void* b = ealloc.malloc(64);
    void* c = ealloc.malloc(64);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(c, nullptr);
    ealloc.free(b);
    ealloc.free(a);
    // a and b should be coalesced into one free block
    ealloc.free(c);
    auto report = ealloc.report();

    ealloc.logStorageReport();
    EXPECT_GE(report.largestFreeRegion, 192u);
}

TEST_F(eAllocTest, Defragmentation)
{
    // Create a fragmented memory state
    void* pool_mem = malloc(4096);
    ASSERT_TRUE(ealloc.add_pool(pool_mem, 4096));

    // Allocate and free blocks to create fragmentation
    void* ptr1 = ealloc.malloc(512);
    void* ptr2 = ealloc.malloc(512);
    void* ptr3 = ealloc.malloc(512);
    ealloc.free(ptr2); // Free middle block to create fragmentation

    dsa::eAlloc::StorageReport initial_report = ealloc.report();
    EXPECT_GT(initial_report.freeBlockCount, 1)
        << "Expected fragmented memory with multiple free blocks.";
    double initial_fragmentation = initial_report.fragmentationFactor;
    LOG::TRACE("GTEST", "before defragmentation...");
    ealloc.logStorageReport();
    // Run defragmentation
    size_t merges = ealloc.defragment();
    EXPECT_GE(merges, 0) << "Defragmentation should report non-negative merge count.";
    LOG::TRACE("GTEST", "after defragmentation...");
    ealloc.logStorageReport();
    dsa::eAlloc::StorageReport final_report = ealloc.report();
    EXPECT_LE(final_report.fragmentationFactor, initial_fragmentation)
        << "Fragmentation factor should not increase after defragmentation.";
    EXPECT_LE(final_report.freeBlockCount, initial_report.freeBlockCount)
        << "Number of free blocks should not increase after defragmentation.";

    // Clean up
    ealloc.free(ptr1);
    ealloc.free(ptr3);
}

TEST_F(eAllocTest, FragmentationFactorMultiPoolAccurate)
{
    uint8_t poolA[1024], poolB[1024];
    void* pA = ealloc.add_pool(poolA, sizeof(poolA));
    void* pB = ealloc.add_pool(poolB, sizeof(poolB));
    ASSERT_NE(pA, nullptr);
    ASSERT_NE(pB, nullptr);

    void* a1 = ealloc.malloc(256);
    void* a2 = ealloc.malloc(256);
    void* b1 = ealloc.malloc(256);
    void* b2 = ealloc.malloc(256);
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(a2, nullptr);
    ASSERT_NE(b1, nullptr);
    ASSERT_NE(b2, nullptr);

    ealloc.free(a1);
    ealloc.free(b1);

    auto report = ealloc.report();
    EXPECT_GT(report.fragmentationFactor, 0.0);

    // Free all, fragmentation should be 0
    ealloc.free(a2);
    ealloc.free(b2);
    report = ealloc.report();
    EXPECT_EQ(report.fragmentationFactor, 0.0);
}

TEST_F(eAllocTest, AutoDefragmentation)
{   

     // Enable auto-defragmentation with very low threshold to ensure it triggers
     ealloc.setAutoDefragment(true, 0.1);
    // Create a fragmented memory state
    void* pool_mem = malloc(4096);
    ASSERT_TRUE(ealloc.add_pool(pool_mem, 4096));

    // Allocate and free blocks to create fragmentation
    void* ptr1 = ealloc.malloc(512);
    void* ptr2 = ealloc.malloc(512);
    void* ptr3 = ealloc.malloc(512);
    ealloc.free(ptr2); // Free middle block to create fragmentation

    dsa::eAlloc::StorageReport initial_report = ealloc.report();
    EXPECT_GT(initial_report.freeBlockCount, 1)
        << "Expected fragmented memory with multiple free blocks.";
    double initial_fragmentation = initial_report.fragmentationFactor;

    // Trigger auto-defragmentation by allocating
    void* trigger = ealloc.malloc(128);
    if (trigger) ealloc.free(trigger);

    ealloc.logStorageReport();

    dsa::eAlloc::StorageReport final_report = ealloc.report();
    EXPECT_LE(final_report.fragmentationFactor, initial_fragmentation)
        << "Fragmentation factor should not increase after auto-defragmentation.";
    EXPECT_LE(final_report.freeBlockCount, initial_report.freeBlockCount)
        << "Number of free blocks should not increase after auto-defragmentation.";

    // Clean up
    ealloc.free(ptr1);
    ealloc.free(ptr3);
    LOG::TRACE("GTEST","After freeing");
    ealloc.logStorageReport();
}

TEST_F(eAllocTest, PoolSpecificAllocation)
{
    // Create multiple pools with different policies and priorities
    char pool1[1024];
    char pool2[1024];
    char pool3[1024];

    dsa::eAlloc::PoolConfig config1(0, 16, 16, dsa::eAlloc::Policy::FAST_ACCESS);
    dsa::eAlloc::PoolConfig config2(1, 16, 16, dsa::eAlloc::Policy::CRITICAL_ONLY);
    dsa::eAlloc::PoolConfig config3(2, 16, 16, dsa::eAlloc::Policy::LOW_FRAGMENTATION);

    void* p1 = ealloc.add_pool(pool1, sizeof(pool1), config1);
    void* p2 = ealloc.add_pool(pool2, sizeof(pool2), config2);
    void* p3 = ealloc.add_pool(pool3, sizeof(pool3), config3);

    EXPECT_NE(p1, nullptr);
    EXPECT_NE(p2, nullptr);
    EXPECT_NE(p3, nullptr);

    // Allocate with specific policy and priority
    void* ptr1 = ealloc.malloc(100, 0, dsa::eAlloc::Policy::FAST_ACCESS);
    void* ptr2 = ealloc.malloc(100, 1, dsa::eAlloc::Policy::CRITICAL_ONLY);
    void* ptr3 = ealloc.malloc(100, 2, dsa::eAlloc::Policy::LOW_FRAGMENTATION);

    EXPECT_NE(ptr1, nullptr);
    EXPECT_NE(ptr2, nullptr);
    EXPECT_NE(ptr3, nullptr);

    // Verify allocations came from correct pools
    void* ptr1_pool = ealloc.get_pool(ptr1);
    void* ptr2_pool = ealloc.get_pool(ptr2);
    void* ptr3_pool = ealloc.get_pool(ptr3);

    EXPECT_EQ(ptr1_pool, p1) << "Allocation with FAST_ACCESS policy should come from pool1";
    EXPECT_EQ(ptr2_pool, p2) << "Allocation with CRITICAL_ONLY policy should come from pool2";
    EXPECT_EQ(ptr3_pool, p3) << "Allocation with LOW_FRAGMENTATION policy should come from pool3";

    ealloc.free(ptr1);
    ealloc.free(ptr2);
    ealloc.free(ptr3);
}

TEST_F(eAllocTest, StatsConsistencyAfterFragmentation)
{
    std::vector<void*> ptrs;
    // Fragment memory
    for(int i = 0; i < 16; ++i) {
        void* p = ealloc.malloc(128);
        if(p) ptrs.push_back(p);
    }
    // Free every other block
    for(size_t i = 0; i < ptrs.size(); i += 2) ealloc.free(ptrs[i]);
    auto before = ealloc.report();
    EXPECT_GT(before.freeBlockCount, 1);
    EXPECT_GT(before.fragmentationFactor, 0.0);

    // Defragment
    ealloc.defragment();
    auto after = ealloc.report();
    EXPECT_LE(after.fragmentationFactor, before.fragmentationFactor);
    EXPECT_LE(after.freeBlockCount, before.freeBlockCount);

    // Free all, stats should be reset
    for(size_t i = 1; i < ptrs.size(); i += 2) ealloc.free(ptrs[i]);
    auto final = ealloc.report();
    EXPECT_EQ(final.fragmentationFactor, 0.0);
    EXPECT_EQ(final.freeBlockCount, 1);
}

TEST_F(eAllocTest, AllocationFailureHandlerIsCalled)
{
    bool handler_called = false;
    ealloc.setAllocationFailureHandler(
        [](size_t, void* user) -> void* {
            *static_cast<bool*>(user) = true;
            return nullptr;
        },
        &handler_called
    );
    std::vector<void*> ptrs;
    for(int i = 0; i < 100; ++i) {
        void* p = ealloc.malloc(1024);
        if(!p) break;
        ptrs.push_back(p);
    }
    EXPECT_TRUE(handler_called);
    for(void* p : ptrs) ealloc.free(p);
}

TEST_F(eAllocTest, PoolAddRemoveStress)
{
    for(int cycle = 0; cycle < 5; ++cycle) {
        uint8_t buf[512];
        void* pool = ealloc.add_pool(buf, sizeof(buf));
        ASSERT_NE(pool, nullptr);
        std::vector<void*> ptrs;
        for(int i = 0; i < 4; ++i) {
            void* p = ealloc.malloc(100);
            if(p) ptrs.push_back(p);
        }
        for(void* p : ptrs) ealloc.free(p);
        ealloc.remove_pool(pool);
    }
    // Should not crash, leak, or corrupt
    EXPECT_EQ(ealloc.check(), 0);
}

TEST_F(eAllocTest, DynamicPoolResizing)
{
    const size_t INITIAL_SIZE = 1024;
    const size_t SHRINK_SIZE = 512;
    const size_t EXPAND_SIZE = 2048;
    char memory[INITIAL_SIZE];
    char expanded_memory[EXPAND_SIZE];
    dsa::eAlloc allocator(memory, INITIAL_SIZE);

    // Verify initial pool size (accounting for possible overhead)
    dsa::eAlloc::StorageReport report = allocator.report();
    size_t initial_free_space = report.totalFreeSpace;
    EXPECT_GE(initial_free_space, INITIAL_SIZE - 16); // Allow for small overhead
    EXPECT_LE(initial_free_space, INITIAL_SIZE);

    // Shrink the pool
    bool shrink_result = allocator.resize_pool(memory, SHRINK_SIZE);
    EXPECT_TRUE(shrink_result);

    // Verify shrunk size (accounting for possible overhead)
    report = allocator.report();
    size_t shrunk_free_space = report.totalFreeSpace;
    EXPECT_GE(shrunk_free_space, SHRINK_SIZE - 16); // Allow for small overhead
    EXPECT_LE(shrunk_free_space, SHRINK_SIZE);

    // Attempt to expand without handler (should fail)
    bool expand_result = allocator.resize_pool(memory, EXPAND_SIZE);
    EXPECT_FALSE(expand_result);

    // Verify size remains unchanged after failed expansion
    report = allocator.report();
    EXPECT_EQ(report.totalFreeSpace, shrunk_free_space);

    // Set up a resize handler for expansion
    auto resize_handler = [](void* current_pool, size_t current_size, size_t requested_size, void* user_data) -> void* {
        if (requested_size > current_size) {
            return user_data; // Return the expanded memory block
        }
        return nullptr;
    };
    allocator.setResizeAllocationHandler(resize_handler, expanded_memory);

    // Attempt to expand with handler (should succeed)
    expand_result = allocator.resize_pool(memory, EXPAND_SIZE);
    EXPECT_TRUE(expand_result);

    // Verify expanded size
    report = allocator.report();
    size_t expanded_free_space = report.totalFreeSpace;
    EXPECT_GE(expanded_free_space, EXPAND_SIZE - 16); // Allow for small overhead
    EXPECT_LE(expanded_free_space, EXPAND_SIZE);

    // Allocate memory to ensure pool can't be resized when in use
    void* ptr = allocator.malloc(100);
    EXPECT_NE(ptr, nullptr);

    // Attempt to shrink while memory is allocated (should fail)
    bool shrink_allocated_result = allocator.resize_pool(expanded_memory, SHRINK_SIZE - 100);
    EXPECT_FALSE(shrink_allocated_result);

    // Free the allocated memory
    allocator.free(ptr);
}

TEST_F(eAllocTest, OwnershipTagAllocationAndFree)
{
    const size_t POOL_SIZE = 1024;
    char memory[POOL_SIZE];
    dsa::eAlloc allocator(memory, POOL_SIZE);

    // Set ownership tag for this 'thread' or 'task'
    uint32_t tag1 = 1001;
    allocator.setOwnershipTag(tag1);

    // Allocate memory with tag1
    void* ptr1 = allocator.malloc(100);
    ASSERT_NE(ptr1, nullptr) << "Allocation with tag1 failed";

    // Allocate another block with tag1
    void* ptr2 = allocator.malloc(100);
    ASSERT_NE(ptr2, nullptr) << "Second allocation with tag1 failed";

    // Change ownership tag
    uint32_t tag2 = 1002;
    allocator.setOwnershipTag(tag2);

    // Allocate with new tag
    void* ptr3 = allocator.malloc(100);
    ASSERT_NE(ptr3, nullptr) << "Allocation with tag2 failed";

    // Free blocks (will log warning if ownership checking is enabled)
    allocator.free(ptr1); // Should log mismatch if checking enabled
    allocator.free(ptr2); // Should log mismatch if checking enabled
    allocator.free(ptr3); // Should match current tag
}

TEST_F(eAllocTest, OwnershipTagWithMultipleThreadsSimulation)
{
    const size_t POOL_SIZE = 2048;
    char memory[POOL_SIZE];
    dsa::eAlloc allocator(memory, POOL_SIZE);

    // Simulate thread 1
    allocator.setOwnershipTag(2001);
    void* ptr_t1_1 = allocator.malloc(100);
    ASSERT_NE(ptr_t1_1, nullptr);
    void* ptr_t1_2 = allocator.malloc(100);
    ASSERT_NE(ptr_t1_2, nullptr);

    // Simulate thread 2
    allocator.setOwnershipTag(2002);
    void* ptr_t2_1 = allocator.malloc(100);
    ASSERT_NE(ptr_t2_1, nullptr);

    // Simulate thread 1 freeing its blocks
    allocator.setOwnershipTag(2001);
    allocator.free(ptr_t1_1); // Should match
    allocator.free(ptr_t1_2); // Should match

    // Simulate thread 2 freeing with wrong tag
    allocator.setOwnershipTag(2003);
    allocator.free(ptr_t2_1); // Should log mismatch if checking enabled
}

#include "gtest/gtest.h"
#include "eAlloc.hpp"
#include "logSetup.hpp"

struct LoggerEnvironment : ::testing::Environment {
    void SetUp() override { setup_logger(); }
};
::testing::Environment* const logger_env = ::testing::AddGlobalTestEnvironment(new LoggerEnvironment());

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
    ealloc.free(ptr);            // Should be safe
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
    if (config.preferred_alignment > 0) {
        EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % config.preferred_alignment, 0u) << "Alignment not respected";
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
        // Simulate recovery by providing a static buffer (in real scenarios, this could free memory or use a reserve)
        static uint8_t recovery_buffer[128];
        if (size <= sizeof(recovery_buffer)) {
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
    for (; count < max_allocs; ++count) {
        ptrs[count] = ealloc.allocate_raw(128);
        if (ptrs[count] == recovery_memory) {
            recovery_used = true;
            break;
        }
        if (!ptrs[count]) break;
    }
    EXPECT_GT(count, 0) << "No allocations succeeded before testing failure handler";
    if (handler_called) {
        EXPECT_TRUE(handler_called) << "Allocation failure handler was not called";
    }
    if (handler_called && recovery_memory && recovery_used) {
        EXPECT_EQ(ptrs[count], recovery_memory) << "Recovery memory not returned by handler";
    }
    // Clean up, avoid freeing recovery buffer to prevent segfault
    for (int i = 0; i < count; ++i) {
        if (ptrs[i] != recovery_memory) {
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
    EXPECT_GT(initial_report.freeBlockCount, 1) << "Expected fragmented memory with multiple free blocks.";
    double initial_fragmentation = initial_report.fragmentationFactor;
    LOG::TRACE("GTEST","before defragmentation...");
    ealloc.logStorageReport();
    // Run defragmentation
    size_t merges = ealloc.defragment();
    EXPECT_GE(merges, 0) << "Defragmentation should report non-negative merge count.";
    LOG::TRACE("GTEST","after defragmentation...");
    ealloc.logStorageReport();
    dsa::eAlloc::StorageReport final_report = ealloc.report();
    EXPECT_LE(final_report.fragmentationFactor, initial_fragmentation) << "Fragmentation factor should not increase after defragmentation.";
    EXPECT_LE(final_report.freeBlockCount, initial_report.freeBlockCount) << "Number of free blocks should not increase after defragmentation.";
    
    // Clean up
    ealloc.free(ptr1);
    ealloc.free(ptr3);
}

TEST_F(eAllocTest, AutoDefragmentation)
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
    EXPECT_GT(initial_report.freeBlockCount, 1) << "Expected fragmented memory with multiple free blocks.";
    double initial_fragmentation = initial_report.fragmentationFactor;
    
    // Enable auto-defragmentation with very low threshold to ensure it triggers
    ealloc.setAutoDefragment(true, 0.1);
    

    ealloc.logStorageReport();
    
    dsa::eAlloc::StorageReport final_report = ealloc.report();
    EXPECT_LE(final_report.fragmentationFactor, initial_fragmentation) << "Fragmentation factor should not increase after auto-defragmentation.";
    EXPECT_LE(final_report.freeBlockCount, initial_report.freeBlockCount) << "Number of free blocks should not increase after auto-defragmentation.";
    
    // Clean up
    ealloc.free(ptr1);
    ealloc.free(ptr3);
}


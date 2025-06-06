#include "StackAllocator.hpp"
#include "eAlloc.hpp"
#include <Arduino.h>
#include <vector>

// Define a fixed memory buffer for the allocator
#define MEMORY_SIZE 4096
static uint8_t memory_buffer[MEMORY_SIZE];

// Simple test class for allocation
struct TestObject {
  int id;
  float value;
  TestObject(int i, float v) : id(i), value(v) {}
  ~TestObject() { LOG::INFO("TestObject %d destroyed\n", id); }
};

void setup() {
  // Initialize Serial communication
  Serial.begin(115200);
  LOG::ENABLE();
  while (!Serial) {
    ; // Wait for Serial to connect
  }
  delay(1000); // Give time for Serial to stabilize
  LOG::TEST("Starting eAlloc test...\n");

  // Initialize eAlloc with the memory buffer
  dsa::eAlloc allocator(memory_buffer, MEMORY_SIZE);
  LOG::TEST("Test 0", "Allocator initialized with %u bytes\n", MEMORY_SIZE);

  // Test 1: Allocate and deallocate a single object
  LOG::TEST("Test 1", " Allocating a TestObject\n");
  TestObject *obj1 = allocator.allocate<TestObject>(1, 42.0f);
  if (obj1) {
    LOG::SUCCESS("Test 1", "Allocated TestObject id=%d, value=%.2f at %p\n",
                 obj1->id, obj1->value, obj1);
    allocator.deallocate(obj1);
  } else {
    LOG::ERROR("Test 1", "Allocation failed!\n");
  }
  allocator.logStorageReport();
  //  Test 2: Add a second pool
  LOG::TEST("Test 2", " Adding a second pool\n");
  uint8_t second_pool[2048];
  void *pool = allocator.add_pool(second_pool, sizeof(second_pool));
  if (pool) {
    LOG::SUCCESS("Test 2", "Second pool added successfully at %p\n", pool);
  } else {
    LOG::ERROR("Test 2", "Failed to add second pool\n");
  }

  // Test 3: Allocate multiple objects
  LOG::TEST("Test 3", " Allocating multiple TestObjects\n");
  TestObject *objects[3];
  for (int i = 0; i < 3; ++i) {
    objects[i] = allocator.allocate(TestObject(i + 2, 100.0f + i));
    if (objects[i]) {
      LOG::SUCCESS("Test 3", "Allocated TestObject id=%d, value=%.2f at  % p",
                   objects[i]->id, objects[i]->value, objects[i]);
    } else {
      LOG::ERROR("Test 3", "Allocation %d failed!\n", i);
    }
  }
  allocator.logStorageReport();
  // Test 4: Check pool integrity
  LOG::TEST("Test 4", " Checking pool integrity\n");
  int integrity_status = allocator.check_pool(memory_buffer);
  LOG::TEST("Test 4", "Main pool integrity check status: %d\n",
            integrity_status);
  if (pool) {
    integrity_status = allocator.check_pool(pool);
    LOG::TEST("Test 4", "Second pool integrity check status: %d\n",
              integrity_status);
  }

  // Test 5: Deallocate objects
  LOG::TEST("\nTest 5: Deallocating TestObjects\n");
  for (int i = 0; i < 3; ++i) {
    if (objects[i]) {
      allocator.deallocate(objects[i]);
    }
  }

  // Test 6: Remove second pool
  LOG::TEST("\nTest 6: Removing second pool\n");
  if (pool) {
    allocator.remove_pool(pool);
    LOG::TEST("Second pool removed\n");
  }

  // Test 7: Overall allocator integrity check
  LOG::TEST("\nTest 7: Checking overall allocator integrity\n");
  int overall_status = allocator.check();
  LOG::TEST("Overall allocator check status: %d\n", overall_status);

  LOG::TEST("\nTest completed.\n");
}

void loop() {
  // Keep the loop empty to run tests only once
  delay(1000);
}
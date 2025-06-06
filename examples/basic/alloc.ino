// Example: Basic usage of eAlloc on Arduino/PlatformIO
#include <eAlloc.hpp>
#include <globalLock.hpp>

// 1KB memory pool for demonstration
static char pool[1024];
dsa::eAlloc alloc(pool, sizeof(pool));

void setup() {
  Serial.begin(115200);
  // No locking for single-threaded Arduino (optional)
  void* p = alloc.malloc(64);
  if (p) {
    Serial.println("Allocation successful!");
    alloc.free(p);
    Serial.println("Memory freed.");
  } else {
    Serial.println("Allocation failed!");
  }
}

void loop() {
  // Nothing here
}

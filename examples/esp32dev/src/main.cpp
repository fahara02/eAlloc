// Example: Basic usage of eAlloc on ESP32 (PlatformIO)
#include <Arduino.h>
#include <eAlloc.hpp>
#include <globalELock.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static char pool[4096];
SemaphoreHandle_t sem = nullptr;
elock::ILockable* lock_ = nullptr;
dsa::eAlloc alloc(pool, sizeof(pool));

void setup() {
    Serial.begin(115200);
    while (!Serial); // Wait for serial
    sem = xSemaphoreCreateMutex();
    // Use eAlloc to allocate memory for the lock object, avoiding uncontrolled dynamic allocation
    lock_ = static_cast<elock::ILockable*>(alloc.allocate_raw<elock::FreeRTOSMutex>());
    if (lock_) {
        // Construct the object in the allocated memory
        new(lock_) elock::FreeRTOSMutex(sem);
        alloc.setLock(lock_);
        void* p = alloc.malloc(128);
        if (p) {
            Serial.println("ESP32 allocation successful!");
            alloc.free(p);
            Serial.println("Memory freed.");
        } else {
            Serial.println("Allocation failed!");
        }
    } else {
        Serial.println("Failed to allocate memory for lock!");
    }
}

void loop() {
    // Nothing here
}

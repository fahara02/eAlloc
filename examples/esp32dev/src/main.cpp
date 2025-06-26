// Example: Basic usage of eAlloc on ESP32 (PlatformIO)
#include <Arduino.h>
#include <eAlloc.hpp>
#include <globalELock.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "Logger.hpp"

static char pool[4096];
SemaphoreHandle_t sem = nullptr;
elock::ILockable* lock_ = nullptr;
dsa::eAlloc alloc(pool, sizeof(pool));

inline void logger_out(const char* msg) { Serial.printf("%s", msg); }

inline void setup_logger()
{
    LOG::SETUP(logger_out);
    LOG::SETUP_TIMESTAMP(millis); // Use portable millis for PC
    LOG::ENABLE();
    LOG::ENABLE_TIMESTAMPS();
}



void setup() {
   Serial.begin(115200);
    while(!Serial)
    {
        ;
    }
    setup_logger();
    sem = xSemaphoreCreateMutex();
    if(sem){
      lock_ = alloc.createLock<elock::FreeRTOSMutex>(true, sem);
    }else{
      Serial.println("Failed to create semaphore!");
    }
    
  
    if (lock_) {
        
        // Lock is already set via createLock with autoSet=true
        void* p = alloc.malloc(128);
        if (p) {
            Serial.println("ESP32 allocation successful!");
            alloc.free(p);
            Serial.println("Memory freed.");
        } else {
            Serial.println("Allocation failed!");
        }
    } else {
        Serial.println("Failed to create lock object!");
    }
}

void loop() {
    // Nothing here
}

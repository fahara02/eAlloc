Quickstart
==========
Get started with **eAlloc** using a simple example.

Basic Usage (Host/PC Example)
-----------------------------
.. code-block:: cpp
   :linenos:

   #include <eAlloc.hpp>
   #include <globalELock.hpp>
   
   // Define a memory pool (4KB)
   static char memory_pool[4096];
   elock::StdMutex mutex;
   dsa::eAlloc alloc(memory_pool, sizeof(memory_pool));
   alloc.setLock(&mutex);
   void* block = alloc.malloc(128);
   if (block) {
       std::cout << "Allocated 128 bytes at " << block << std::endl;
       alloc.free(block);
   } else {
       std::cout << "Failed to allocate 128 bytes." << std::endl;
   }
   return 0;



ESP32 Example (MCU)
-----------------------------
.. code-block:: cpp
   :linenos:

   #include <eAlloc.hpp>
   #include <globalELock.hpp>
   #include "freertos/FreeRTOS.h"
   #include "freertos/task.h"
   #include "esp_log.h"
   
   static const char *TAG = "eAlloc_ESP32";
   
   // Define a smaller memory pool for MCU (2KB)
   static char mcu_pool[2048];
   
   extern "C" void app_main(void) {
       // Create a FreeRTOS mutex
       elock::FreeRTOSMutex mutex(xSemaphoreCreateMutex());
       
       dsa::eAlloc alloc(mcu_pool, sizeof(mcu_pool));
       alloc.setLock(&mutex);
   
       void* ptr = alloc.malloc(128);
       if (ptr) {
           ESP_LOGI(TAG, "Allocated 128 bytes at %p", ptr);
           alloc.free(ptr);
       } else {
           ESP_LOGE(TAG, "Failed to allocate memory.");
       }
       // Delay to allow logs to flush
       vTaskDelay(1000 / portTICK_PERIOD_MS);
   }



StackAllocator Example
-----------------------------
.. code-block:: cpp
   :linenos:

   #include <StackAllocator.hpp>
   #include <vector>
   #include <iostream>
   
   int main() {
       dsa::StackAllocator<int, 256> stack_alloc;
       std::vector<int, dsa::StackAllocator<int, 256>> vec(stack_alloc);
       for (int i = 0; i < 10; ++i) {
           vec.push_back(i * 5);
       }
       std::cout << "StackAllocator Vector: ";
       for (int val : vec) {
           std::cout << val << " ";
       }
       std::cout << std::endl;
       return 0;
   }


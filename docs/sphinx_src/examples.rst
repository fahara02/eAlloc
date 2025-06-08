Examples
========

This page provides practical examples of how to use **eAlloc** in various scenarios.

.. tip::
   For thread safety in concurrent environments, always set an elock for your allocator instance using ``alloc.setLock(&mutex_adapter)``.
   Use ``elock::StdMutex`` for host/PC applications, or the appropriate adapter (e.g., ``elock::FreeRTOSMutex``, ``elock::CMSISMutex``) for your specific MCU/RTOS platform.

MCU Example 1: FreeRTOS
-------------------------
A minimal, thread-safe allocator for FreeRTOS:

.. code-block:: cpp
   :linenos:

   #include <eAlloc.hpp>
   #include <globalELock.hpp> // For elock::FreeRTOSMutex
   #include "freertos/FreeRTOS.h"
   #include "freertos/semphr.h" // For xSemaphoreCreateMutex
   
   // Define a memory pool (e.g., 4KB)
   static char memory_pool[4096];
   
   // Create a FreeRTOS mutex
   SemaphoreHandle_t my_mutex_handle = xSemaphoreCreateMutex();
   elock::FreeRTOSMutex freertos_mutex(my_mutex_handle);
   
   // Initialize the allocator
   dsa::eAlloc alloc(memory_pool, sizeof(memory_pool));
   alloc.setLock(&freertos_mutex); // Set the lock for thread safety
   
   // Usage
   void* block = alloc.malloc(128);
   if (block) {
       // Use the allocated memory
       alloc.free(block);
   }



MCU Example 2: ESP-IDF (ESP32)
----------------------------------
A minimal, thread-safe allocator for ESP-IDF (ESP32/ESP8266):

.. code-block:: cpp
   :linenos:

   #include <eAlloc.hpp>
   #include <globalELock.hpp> // For elock::FreeRTOSMutex (ESP-IDF uses FreeRTOS)
   #include "freertos/FreeRTOS.h"
   #include "freertos/semphr.h"
   #include "esp_log.h"
   
   static const char *TAG_EX = "eAlloc_ESP_Example";
   
   // Define a memory pool (e.g., 2KB)
   static char esp_pool[2048];
   
   // Create a FreeRTOS mutex (as ESP-IDF uses FreeRTOS)
   SemaphoreHandle_t esp_mutex_handle = xSemaphoreCreateMutex();
   elock::FreeRTOSMutex esp_freertos_mutex(esp_mutex_handle);
   
   // Initialize the allocator
   dsa::eAlloc esp_alloc(esp_pool, sizeof(esp_pool));
   esp_alloc.setLock(&esp_freertos_mutex); // Set the lock
   
   // Usage within an ESP-IDF task
   void some_esp_task(void *pvParameters) {
       void* ptr = esp_alloc.malloc(64);
       if (ptr) {
           ESP_LOGI(TAG_EX, "Allocated 64 bytes at %p", ptr);
           // Use memory
           esp_alloc.free(ptr);
       } else {
           ESP_LOGE(TAG_EX, "Failed to allocate memory.");
       }
       vTaskDelete(NULL);
   }



Host/PC Example
------------------
A minimal, thread-safe allocator for host/PC applications:

.. code-block:: cpp
   :linenos:

   #include <eAlloc.hpp>
   #include <globalELock.hpp>
   #include <iostream>
   
   // Define a memory pool (e.g., 8KB)
   static char pc_pool[8192];
   elock::StdMutex std_mutex;
   dsa::eAlloc host_alloc(pc_pool, sizeof(pc_pool));
   host_alloc.setLock(&std_mutex);
   
   int main() {
       alloc2.setLock(&pc2_lock);
   
       // Using calloc: allocate memory for 10 integers, initialized to zero
       int* arr = (int*)alloc2.calloc(10, sizeof(int));
       if (arr) {
           std::cout << "calloc allocated 10 ints at " << arr << std::endl;
           bool all_zero = true;
           for (size_t i = 0; i < 10; ++i) {
               if (arr[i] != 0) {
                   all_zero = false;
                   break;
               }
           }
           std::cout << "Memory is " << (all_zero ? "" : "NOT ") << "zero-initialized by calloc." << std::endl;
   
           // Using realloc: resize the block to hold 20 integers
           int* resized_arr = (int*)alloc2.realloc(arr, 20 * sizeof(int));
           if (resized_arr) {
               arr = resized_arr; // arr might have moved
               std::cout << "realloc resized block to 20 ints at " << arr << std::endl;
               // Initialize the new part
               for (size_t i = 10; i < 20; ++i) arr[i] = i;
               std::cout << "arr[15] = " << arr[15] << std::endl;
           }
           alloc2.free(arr); // Free the final block
       } else {
           std::cout << "calloc failed." << std::endl;
       }
       return 0;
   }



StackAllocator Example (STL-compatible)
---------------------------------------
Use STL containers (e.g., ``std::vector``) with a fixed-size stack buffer, achieving zero heap allocations for the container's elements:

.. code-block:: cpp
   :linenos:

   #include <StackAllocator.hpp>
   #include <vector>
   #include <iostream>
   #include <string>
   
   int main() {
       // StackAllocator for 10 integers (approx 40 bytes)
       dsa::StackAllocator<int, 10 * sizeof(int)> int_alloc;
       std::vector<int, dsa::StackAllocator<int, 10 * sizeof(int)>> int_vec(int_alloc);
   
       for (int i = 0; i < 5; ++i) {
           int_vec.push_back(i * 10);
       }
       std::cout << "StackAllocator int_vec: ";
       for (int val : int_vec) {
           std::cout << val << " ";
       }
       std::cout << std::endl;
   
       // StackAllocator for a few small strings (e.g., 256 bytes buffer)
       // Note: std::string itself might do heap allocations for large strings internally,
       // but the vector's control structures and small string optimization (SSO)
       // can benefit from the StackAllocator.
       dsa::StackAllocator<std::string, 256> string_alloc;
       std::vector<std::string, dsa::StackAllocator<std::string, 256>> str_vec(string_alloc);
       
       str_vec.push_back("Hello");
       str_vec.push_back("Stack");
       str_vec.push_back("World");
   
       std::cout << "StackAllocator str_vec: ";
       for (const auto& s : str_vec) {
           std::cout << "\"" << s << "\" ";
       }
       std::cout << std::endl;
   
       return 0;
   }



See also the :doc:`api` page for full details on ``StackAllocator`` and other classes.

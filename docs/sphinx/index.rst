eAlloc
======

Welcome to the eAlloc documentation!

.. contents::
   :local:
   :depth: 1

Overview
========
eAlloc is a modern, MCU/host-agnostic C++17 memory allocator library designed for embedded and desktop systems. At its core, eAlloc uses the TLSF (Two-Level Segregate Fit) algorithm—a dynamic memory allocator specifically engineered to meet the demands of real-time and embedded applications.

**What is TLSF?**
TLSF (Two-Level Segregate Fit) is a general-purpose dynamic memory allocator designed for hard and soft real-time systems. Its key innovation is guaranteeing constant-time (O(1)) allocation and free operations, regardless of application data or memory pool state. This deterministic timing is essential for real-time operating systems (RTOS) and latency-sensitive applications.

**Why TLSF?**
- **Bounded Response Time:** TLSF ensures worst-case execution time (WCET) for allocation and deallocation is constant and predictable, making it ideal for real-time and safety-critical systems.
- **Low Fragmentation:** TLSF achieves low memory fragmentation (typically <15% average, <25% maximum), which is crucial for long-running embedded devices and systems with constrained memory.
- **Efficiency:** TLSF is fast and lightweight, executing a small, bounded number of instructions per operation. It supports multiple memory pools and adapts well to both MCU and host environments.
- **Proven in Practice:** TLSF is widely used in embedded, RTOS, multimedia, networking, and gaming applications, and is included in several Linux distributions.

For more details, see the official TLSF website: http://www.gii.upv.es/tlsf/

**eAlloc Features:**
- O(1) allocation and free (constant-time, no heap walk)
- Low fragmentation, suitable for real-time/embedded
- Deterministic timing (no hidden loops or recursion)
- Supports multiple memory pools
- Suitable for both MCU and host

Other Features:
- StackAllocator for STL compatibility
- Platform-adaptive RAII lock guards (glock::ILockable, glock::LockGuard)
- Minimal STL bloat (only on host)
- GoogleTest support for unit tests

Quickstart
==========

Get started with eAlloc in just a few lines. The API is the same for embedded MCUs and PC/host builds—just use the right lock adapter for your platform.

Platform Support
---------------
- **MCU/RTOS**: FreeRTOS, CMSIS RTOS, Zephyr, ThreadX, Mbed, Arduino, ESP-IDF
- **Host/PC**: Linux, Windows, macOS (uses std::timed_mutex)

.. tip::
   For thread safety, always set a lock via ``alloc.setLock(&mutex)``.
   Use ``glock::StdMutex`` for host/PC, or the appropriate adapter for your platform.

MCU/Embedded (FreeRTOS)
-----------------------
A minimal, thread-safe allocator for FreeRTOS:

.. code-block:: cpp
   :linenos:

   #include <eAlloc.hpp>
   #include <globalLock.hpp>
   static char pool[4096];
   glock::FreeRTOSMutex mutex(xSemaphoreCreateMutex());
   dsa::eAlloc alloc(pool, sizeof(pool));
   alloc.setLock(&mutex);
   void* p = alloc.malloc(128);
   alloc.free(p);

MCU/Embedded (ESP-IDF)
----------------------
A minimal, thread-safe allocator for ESP-IDF (ESP32/ESP8266):

.. code-block:: cpp
   :linenos:

   #include <eAlloc.hpp>
   #include <globalLock.hpp>
   #include <freertos/FreeRTOS.h>
   #include <freertos/semphr.h>
   static char pool[4096];
   SemaphoreHandle_t sem = xSemaphoreCreateMutex();
   glock::FreeRTOSMutex mutex(sem);
   dsa::eAlloc alloc(pool, sizeof(pool));
   alloc.setLock(&mutex);
   void* p = alloc.malloc(256);
   alloc.free(p);

PC/Host (std::timed_mutex)
--------------------------
A minimal, thread-safe allocator for desktop/host:

.. code-block:: cpp
   :linenos:

   #include <eAlloc.hpp>
   #include <globalLock.hpp>
   static char pool[4096];
   std::timed_mutex mtx;
   glock::StdMutex mutex(mtx);
   dsa::eAlloc alloc(pool, sizeof(pool));
   alloc.setLock(&mutex);
   void* p = alloc.malloc(128);
   alloc.free(p);

StackAllocator (STL-compatible)
-------------------------------
Use STL containers (e.g. std::vector) with a fixed-size stack buffer and zero heap allocation:

.. code-block:: cpp
   :linenos:

   #include <StackAllocator.hpp>
   #include <vector>

   dsa::StackAllocator<int, 128> alloc;
   std::vector<int, dsa::StackAllocator<int, 128>> vec(alloc);
   vec.push_back(42);

See also the StackAllocator class below for full API details.

API Reference
=============

.. rubric:: Main Classes

.. rubric:: Locking Interfaces

.. rubric:: Functions

.. rubric:: Full API Index

.. doxygenindex::
   :project: eAlloc

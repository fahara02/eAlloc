Overview
========

**eAlloc** is a modern, MCU/host-agnostic C++17 memory allocator library designed for embedded and desktop systems. At its core, **eAlloc** uses the TLSF (Two-Level Segregate Fit) algorithm—a dynamic memory allocator specifically engineered to meet the demands of real-time and embedded applications.

What is TLSF?
-------------
TLSF (Two-Level Segregate Fit) is a general-purpose dynamic memory allocator designed for hard and soft real-time systems. Its key innovation is guaranteeing constant-time (O(1)) allocation and deallocation, regardless of application data or memory pool state. This deterministic timing is essential for real-time operating systems (RTOS) and latency-sensitive applications.

Why TLSF?
---------
- **Bounded Response Time:** TLSF ensures worst-case execution time (WCET) for allocation and deallocation is constant and predictable, making it ideal for real-time and safety-critical systems.
- **Low Fragmentation:** TLSF achieves low memory fragmentation (typically <15% average, <25% maximum), which is crucial for long-running embedded devices and systems with constrained memory.
- **Efficiency:** TLSF is fast and lightweight, executing a small, bounded number of instructions per operation. It supports multiple memory pools and adapts well to both MCU and host environments.
- **Proven in Practice:** TLSF is widely used in embedded, RTOS, multimedia, networking, and gaming applications, and is included in several Linux distributions.

For more details, see the official TLSF website: `http://www.gii.upv.es/tlsf/ <http://www.gii.upv.es/tlsf/>`_

eAlloc Features
---------------
- O(1) allocation and free (constant-time, no heap walk).
- Low fragmentation, suitable for real-time/embedded.
- Deterministic timing (no hidden loops or recursion).
- Supports multiple memory pools.
- Suitable for both MCU and host.
- StackAllocator for STL compatibility.
- Platform-adaptive RAII elock guards (elock::IELockable, elock::ELockGuard).
- Minimal STL bloat (only on host).
- GoogleTest support for unit tests.

Design Philosophy
-----------------
- **Performance First**: Leverages TLSF for O(1) malloc/free, ensuring deterministic behavior crucial for real-time systems.
- **Memory Efficiency**: Designed to minimize fragmentation and overhead, maximizing usable memory in constrained environments.
- **Portability**: Core logic is C++17 standard-compliant, adaptable to various MCUs (Arduino, FreeRTOS, ESP-IDF) and host systems (Linux, Windows, macOS) with minimal platform-specific code.
- **Thread Safety**: Provides a flexible locking mechanism (via `elock` adapters) to ensure safe operation in multi-threaded applications.
- **Ease of Use**: Offers a familiar `malloc`-like API, complemented by features like `StackAllocator` for enhanced usability with STL containers.

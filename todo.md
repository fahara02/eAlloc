# eAlloc Library TODO List

This document outlines the critical missing features and areas of improvement for the eAlloc library, designed for embedded systems with a focus on static allocation and minimal overhead.

## Critical Missing Features

- [ ] **Advanced Memory Pool Management**
  - Implement dynamic resizing or adjustment of memory pools at runtime. (❌ Pending)
  - Add priority or policy-based allocation across pools for critical tasks. (❌ Pending)

- [ ] **Defragmentation Handling**
  - Develop a mechanism to handle long-term fragmentation, possibly through periodic compaction. (✅ Completed with defragment() and auto-defragmentation in malloc)
  - Provide statistics on fragmentation levels for debugging and optimization. (✅ Completed with enhanced StorageReport metrics)

- [ ] **Error Recovery Mechanisms**
  - Create a callback system to notify applications of critical memory issues. (❌ Pending)
  - Implement handling for out-of-memory conditions, such as fallback to a reserve pool or user-defined handlers. (✅ Completed with AllocationFailureHandler in malloc)

- [ ] **Memory Safety Features**
  - Add optional bounds checking or buffer overflow protection using canary values or guard bands. (❌ Pending)
  - Implement memory tagging or ownership tracking to prevent misuse across modules or tasks. (❌ Pending)

- [ ] **Performance Optimizations for MCUs**
  - Optimize for cache alignment and memory access patterns specific to embedded hardware like ESP32. (❌ Pending)
  - Enhance `StackAllocator` with customization options for alignment policies and block size restrictions. (❌ Pending)

- [ ] **Customizable Thread Safety**
  - Allow customization of locking granularity (e.g., per-pool locks vs. global lock) to reduce contention. (❌ Pending)
  - Provide a compile-time option to disable locking entirely for single-threaded applications. (❌ Pending)

- [ ] **Comprehensive Debugging Tools**
  - Develop profiling tools to track allocation patterns, peak usage, and detect memory leaks. (❌ Pending)
  - Add mechanisms to dump memory state or log allocation history for post-mortem analysis. (❌ Pending)

## Additional Tasks

- [ ] **Documentation Improvements**
  - Resolve Sphinx warnings and errors to ensure complete documentation. (❌ Pending)
  - Add guides for using eAlloc in various embedded scenarios (RTOS, bare-metal). (❌ Pending)

- [ ] **Expanded Examples**
  - Create more use cases showing integration with different platforms beyond ESP32. (❌ Pending)
  - Demonstrate specific allocation patterns and best practices. (❌ Pending)

Feel free to prioritize these tasks based on project needs or add new items as necessary.

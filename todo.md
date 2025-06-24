# eAlloc Library TODO List

This document outlines the critical missing features and areas of improvement for the eAlloc library, designed for embedded systems with a focus on static allocation and minimal overhead.

## Critical Missing Features

- [ ] **Advanced Memory Pool Management**
  - Implement dynamic resizing or adjustment of memory pools at runtime.
  - Add priority or policy-based allocation across pools for critical tasks.

- [ ] **Defragmentation Handling**
  - Develop a mechanism to handle long-term fragmentation, possibly through periodic compaction.
  - Provide statistics on fragmentation levels for debugging and optimization.

- [ ] **Error Recovery Mechanisms**
  - Create a callback system to notify applications of critical memory issues.
  - Implement handling for out-of-memory conditions, such as fallback to a reserve pool or user-defined handlers.

- [ ] **Memory Safety Features**
  - Add optional bounds checking or buffer overflow protection using canary values or guard bands.
  - Implement memory tagging or ownership tracking to prevent misuse across modules or tasks.

- [ ] **Performance Optimizations for MCUs**
  - Optimize for cache alignment and memory access patterns specific to embedded hardware like ESP32.
  - Enhance `StackAllocator` with customization options for alignment policies and block size restrictions.

- [ ] **Customizable Thread Safety**
  - Allow customization of locking granularity (e.g., per-pool locks vs. global lock) to reduce contention.
  - Provide a compile-time option to disable locking entirely for single-threaded applications.

- [ ] **Comprehensive Debugging Tools**
  - Develop profiling tools to track allocation patterns, peak usage, and detect memory leaks.
  - Add mechanisms to dump memory state or log allocation history for post-mortem analysis.

## Additional Tasks

- [ ] **Documentation Improvements**
  - Resolve Sphinx warnings and errors to ensure complete documentation.
  - Add guides for using eAlloc in various embedded scenarios (RTOS, bare-metal).

- [ ] **Expanded Examples**
  - Create more use cases showing integration with different platforms beyond ESP32.
  - Demonstrate specific allocation patterns and best practices.

Feel free to prioritize these tasks based on project needs or add new items as necessary.

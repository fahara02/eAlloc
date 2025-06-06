# eAlloc

**eAlloc** is a modern, MCU/host-agnostic C++17 memory allocator library designed for embedded and desktop systems. It provides deterministic, real-time dynamic memory management with minimal STL bloat, making it ideal for resource-constrained and safety-critical applications.

---

## Features

- **TLSF Core**: Uses the [Two-Level Segregate Fit (TLSF)](http://www.gii.upv.es/tlsf/) algorithm for constant-time (O(1)) allocation and free.
- **Real-Time Ready**: Bounded response time, deterministic timing, and low fragmentation (<15% avg, <25% max).
- **MCU & Host Support**: Works seamlessly on embedded RTOS (FreeRTOS, CMSIS RTOS, Zephyr, ThreadX, Mbed, Arduino, ESP-IDF) and desktop OSes (Linux, Windows, macOS).
- **Multiple Pools**: Supports multiple independent memory pools.
- **Minimal STL Bloat**: Only essential STL features are used on the host; no unnecessary dependencies for embedded targets.
- **StackAllocator**: STL-compatible allocator for fixed-size, stack-based containers.
- **Thread Safety**: Optional RAII elock guard for malloc/free and critical sections via `elock::ILockable`.
- **Unit-Tested**: GoogleTest suite provided.

---

## What is TLSF?

TLSF (Two-Level Segregate Fit) is a dynamic memory allocator specifically designed for real-time and embedded systems. Its key strengths are:

- **Constant-Time Operations**: Guarantees O(1) allocation and free, regardless of memory usage or fragmentation.
- **Low Fragmentation**: Maintains low memory fragmentation over long runtimes.
- **Deterministic**: No hidden loops or recursion; worst-case execution time (WCET) is predictable.
- **Proven**: Used in embedded, RTOS, multimedia, networking, and gaming applications, and included in several Linux distributions.

Learn more: [TLSF Official Site](http://www.gii.upv.es/tlsf/)

---

## Quickstart

### Embedded (FreeRTOS Example)
```cpp
#include <eAlloc.hpp>
#include <globalELock.hpp>
static char pool[4096];
elock::FreeRTOSMutex mutex(xSemaphoreCreateMutex());
dsa::eAlloc alloc(pool, sizeof(pool));
alloc.setELock(&mutex);
void* p = alloc.malloc(128);
alloc.free(p);
```

### Desktop/Host Example
```cpp
#include <eAlloc.hpp>
#include <globalELock.hpp>
static char pool[4096];
std::timed_mutex mtx;
elock::StdMutex mutex(mtx);
dsa::eAlloc alloc(pool, sizeof(pool));
alloc.setELock(&mutex);
void* p = alloc.malloc(128);
alloc.free(p);
```

### STL-Compatible Stack Allocator
```cpp
#include <StackAllocator.hpp>
#include <vector>

dsa::StackAllocator<int, 128> alloc;
std::vector<int, dsa::StackAllocator<int, 128>> vec(alloc);
vec.push_back(42);
```

---

## Platform Support
- **MCU/RTOS**: FreeRTOS, CMSIS RTOS, Zephyr, ThreadX, Mbed, Arduino, ESP-IDF
- **Host/PC**: Linux, Windows, macOS (uses `std::timed_mutex`)

---

## Thread Safety
For thread safety, always set a elock via `alloc.setELock(&mutex)`. Use `elock::StdMutex` for host/PC, or the appropriate adapter for your platform.

---

## Documentation
- Full API reference and guides: see `/docs/html/index.html` (auto-generated with Sphinx/Breathe/Doxygen)
- [TLSF algorithm details](http://www.gii.upv.es/tlsf/)

---

## License
BSD-licensed. See `LICENSE` file for details.

---

## Contributing
Pull requests and bug reports are welcome! Please ensure changes are covered by tests and documentation.

---

## Acknowledgments
- [TLSF](http://www.gii.upv.es/tlsf/) by M. Masmano et al.
- [Breathe](https://breathe.readthedocs.io/), [Sphinx](https://www.sphinx-doc.org/), [Doxygen](https://www.doxygen.nl/)

---

*eAlloc: Real-Time Dynamic Memory for Modern Embedded C++*

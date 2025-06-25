/**
 * @file globalELock.hpp
 * @brief Universal RAII lock guard and platform lock adapter system for eAlloc.
 *
 * Provides MCU-agnostic, minimal-overhead locking for critical sections and memory allocators.
 * Selects the appropriate mutex adapter for each platform via CMake and preprocessor macros.
 *
 * Usage:
 *   - Use elock::ILockable as the abstract mutex interface.
 *   - Use elock::LockGuard for RAII critical sections.
 *   - Use the correct adapter (e.g. elock::FreeRTOSMutex, elock::StdMutex) for your platform.
 *   - Platform is selected by CMake option and macro (EALLOC_PC_HOST, FREERTOS, etc).
 *
 * Thread Safety:
 *   - Only one lock per memory pool/allocator instance is recommended.
 *   - Avoid double-locking by letting the allocator own the lock and exposing setLock().
 *
 * No STL bloat for MCU: STL headers only included for PC/host builds.
 */
// src/globalELock.hpp
#pragma once

namespace dsa {
    class eAlloc;
}

// Platform Detection:
// For ESP32/ESP-IDF builds, CMake should define ESP32 or ESP_PLATFORM.
// For host/PC builds, do NOT define ESP_PLATFORM, FREERTOS, or ARDUINO.
#if defined(ESP32) && !defined(ESP_PLATFORM)
#  define ESP_PLATFORM
#endif

#include <stdint.h>

#if defined(EALLOC_PC_HOST)
#include <mutex>
#include <chrono>
#endif

#if defined(FREERTOS) || defined(ESP_PLATFORM) || defined(ARDUINO)
// FreeRTOS/ESP-IDF/Arduino/PlatformIO
#  include "freertos/FreeRTOS.h"
#  include "freertos/semphr.h"
#endif
#if defined(POSIX)
#  include <pthread.h>
#  include <time.h>
#endif
#if defined(STM32_CMSIS_RTOS)
#  include "cmsis_os.h"
#endif
#if defined(STM32_CMSIS_RTOS2)
#  include "cmsis_os2.h"
#endif
#if defined(ZEPHYR)
#  include <zephyr/kernel.h>
#endif
#if defined(THREADX)
#  include "tx_api.h"
#endif
#if defined(MBED_OS)
#  include "mbed.h"
#endif
namespace elock {

/**
 * @brief Abstract lockable interface for platform-agnostic mutexes.
 *
 * All platform adapters must implement ILockable.
 */
class ILockable {
public:
    virtual bool lock(uint32_t timeout_ms = 0xFFFFFFFF) = 0;
    virtual void unlock() = 0;
    virtual ~ILockable() {}
};



/**
 * @brief A universal RAII lock guard class that manages a lock's lifetime.
 *
 * This class provides a RAII-style mechanism to automatically acquire a lock upon construction
 * and release it upon destruction. It wraps a reference to an ILockable object and attempts to
 * acquire the lock using an optional timeout parameter. The lock is released in the destructor
 * if it was successfully acquired.
 *
 * @note Copy construction and copy-assignment are disabled to prevent multiple ownership of the same lock.
 *
 * @param lock A reference to an ILockable object managing the lock.
 * @param timeout_ms The timeout in milliseconds for acquiring the lock (default is 0xFFFFFFFF for no timeout).
 */


class LockGuard {
public:
    LockGuard(ILockable& lock, uint32_t timeout_ms = 0xFFFFFFFF)
        : lock_(lock), acquired_(lock.lock(timeout_ms)) {}
    ~LockGuard() { if (acquired_) lock_.unlock(); }
    bool acquired() const { return acquired_; }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
private:
    ILockable& lock_;
    bool acquired_;
};







// --- Platform Adapters ---

#if defined(FREERTOS) || defined(ESP_PLATFORM) || defined(ARDUINO)


/**
 * @brief FreeRTOS mutex adapter for global locking.
 *
 * This class implements the ILockable interface using a FreeRTOS semaphore.
 * It is intended for use as a global lock to synchronize access across tasks on
 * FreeRTOS/ESP-IDF/Arduino/PlatformIO platforms.
 */

class FreeRTOSMutex : public ILockable {
public:
    FreeRTOSMutex(SemaphoreHandle_t sem) : sem_(sem) {}
    bool lock(uint32_t timeout_ms) override {
        static int lock_counter = 0;
        LOG::INFO("E_ALLOC", "Locking FreeRTOS mutex %p. Attempt #%d\n", sem_, ++lock_counter);
        return xSemaphoreTake(sem_, timeout_ms / portTICK_PERIOD_MS) == pdTRUE;
    }
    void unlock() override { xSemaphoreGive(sem_); }
private:
    SemaphoreHandle_t sem_;
};

#elif defined(POSIX)
// POSIX pthreads (Linux, Mac, Unix)


/**
 * @brief POSIX mutex adapter for global locking.
 *
 * This class implements the ILockable interface using a POSIX pthread mutex.
 * It is designed for use as a global lock to synchronize access across threads
 * on Unix-like systems.
 */
class PThreadMutex : public ILockable {
public:
    PThreadMutex(pthread_mutex_t* mtx) : mtx_(mtx) {}
    bool lock(uint32_t timeout_ms) override {
        if (timeout_ms == 0xFFFFFFFF) return pthread_mutex_lock(mtx_) == 0;
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000;
        }
        return pthread_mutex_timedlock(mtx_, &ts) == 0;
    }
    void unlock() override { pthread_mutex_unlock(mtx_); }
private:
    pthread_mutex_t* mtx_;
};

#elif defined(STM32_CMSIS_RTOS)
/**
 * @brief CMSIS mutex adapter for global locking.
 *
 * This class implements the ILockable interface using a CMSIS RTOS mutex (osMutexId).
 * It is designed for use on STM32 platforms with CMSIS RTOS to synchronize access among threads.
 */

class CMSISMutex : public ILockable {
public:
    CMSISMutex(osMutexId id) : id_(id) {}
    bool lock(uint32_t timeout_ms) override { return osMutexWait(id_, timeout_ms) == osOK; }
    void unlock() override { osMutexRelease(id_); }
private:
    osMutexId id_;
};

#elif defined(STM32_CMSIS_RTOS2)

/**
 * @brief CMSIS RTOS2 mutex adapter for global locking.
 *
 * This class implements the ILockable interface using a CMSIS RTOS2 mutex (osMutexId_t).
 * It is intended for use on STM32 platforms with CMSIS RTOS2 to synchronize access among threads.
 */

class CMSIS2Mutex : public ILockable {
public:
    CMSIS2Mutex(osMutexId_t id) : id_(id) {}
    bool lock(uint32_t timeout_ms) override { return osMutexAcquire(id_, timeout_ms) == osOK; }
    void unlock() override { osMutexRelease(id_); }
private:
    osMutexId_t id_;
};

#elif defined(ZEPHYR)

/**
 * @brief Zephyr mutex adapter for global locking.
 *
 * This class implements the ILockable interface using a Zephyr kernel mutex (k_mutex).
 * It is designed for use on Zephyr RTOS to synchronize access among threads.
 */

class ZephyrMutex : public ILockable {
public:
    ZephyrMutex(struct k_mutex* mtx) : mtx_(mtx) {}
    bool lock(uint32_t timeout_ms) override { return k_mutex_lock(mtx_, K_MSEC(timeout_ms)) == 0; }
    void unlock() override { k_mutex_unlock(mtx_); }
private:
    struct k_mutex* mtx_;
};

#elif defined(THREADX)

/**
 * @brief ThreadX mutex adapter for global locking.
 *
 * This class implements the ILockable interface using a ThreadX mutex (TX_MUTEX).
 * It is intended for use on ThreadX platforms to synchronize access among threads.
 */

class ThreadXMutex : public ILockable {
public:
    ThreadXMutex(TX_MUTEX* mtx) : mtx_(mtx) {}
    bool lock(uint32_t timeout_ms) override {
        ULONG ticks = (timeout_ms == 0xFFFFFFFF) ? TX_WAIT_FOREVER : timeout_ms;
        return tx_mutex_get(mtx_, ticks) == TX_SUCCESS;
    }
    void unlock() override { tx_mutex_put(mtx_); }
private:
    TX_MUTEX* mtx_;
};

#elif defined(MBED_OS)
/**
 * @brief mbed OS mutex adapter for global locking.
 *
 * This class implements the ILockable interface using an mbed OS mutex (rtos::Mutex).
 * It is intended for use on mbed OS platforms to synchronize access among threads.
 */
class MbedMutex : public ILockable {
public:
    MbedMutex(rtos::Mutex& mtx) : mtx_(mtx) {}
    bool lock(uint32_t timeout_ms) override { return mtx_.trylock_for(timeout_ms); }
    void unlock() override { mtx_.unlock(); }
private:
    rtos::Mutex& mtx_;
};

#elif defined(BAREMETAL)

/**
 * @brief Dummy mutex adapter for global locking on bare-metal systems.
 *
 * This class implements the ILockable interface with no actual locking mechanism.
 * It is intended for use in bare-metal environments where synchronization is not required.
 */

class DummyMutex : public ILockable {
public:
    DummyMutex() {}
    bool lock(uint32_t) override { return true; }
    void unlock() override {}
};

#elif defined(EALLOC_PC_HOST)
/**
 * @brief Standard mutex adapter for global locking on host systems.
 *
 * This class implements the ILockable interface using a C++11 std::timed_mutex.
 * It is intended for use on host/PC environments supporting minimal STL with C++11.
 */
class StdMutex : public ILockable {
public:
    StdMutex(std::timed_mutex& mtx) : mtx_(mtx) {}
    bool lock(uint32_t timeout_ms) override {
        return mtx_.try_lock_for(std::chrono::milliseconds(timeout_ms));
    }
    void unlock() override { mtx_.unlock(); }
private:
    std::timed_mutex& mtx_;
};

#else
#error "No valid platform adapter selected for elock::ILockable. Define a supported platform macro (e.g. EALLOC_PC_HOST for host builds, FREERTOS, BAREMETAL, etc.)"
#endif // Platform Adapters

} // namespace elock

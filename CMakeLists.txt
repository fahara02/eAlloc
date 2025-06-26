cmake_minimum_required(VERSION 3.10)
project(eAlloc)



enable_testing()

set(CMAKE_CXX_STANDARD 17)

# GoogleTest setup
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/release-1.11.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Logger dependency
FetchContent_Declare(
  Logger
  GIT_REPOSITORY https://github.com/fahara02/Logger.git
  GIT_TAG main
)
FetchContent_MakeAvailable(Logger)



# === Platform Adapter Selection ===
option(EALLOC_USE_PC_HOST "Use PC/host STL mutex adapter" ON)
option(EALLOC_USE_FREERTOS "Use FreeRTOS mutex adapter" OFF)
option(EALLOC_USE_BAREMETAL "Use baremetal lock adapter" OFF)
option(EALLOC_USE_ARDUINO "Use Arduino lock adapter" OFF)

# Ensure only one is ON
set(_pc_host 0)
set(_freertos 0)
set(_baremetal 0)
set(_arduino 0)
if(EALLOC_USE_PC_HOST)
  set(_pc_host 1)
endif()
if(EALLOC_USE_FREERTOS)
  set(_freertos 1)
endif()
if(EALLOC_USE_BAREMETAL)
  set(_baremetal 1)
endif()
if(EALLOC_USE_ARDUINO)
  set(_arduino 1)
endif()
math(EXPR _sum "${_pc_host} + ${_freertos} + ${_baremetal} + ${_arduino}")
if(NOT _sum EQUAL 1)
  message(FATAL_ERROR "Exactly one of EALLOC_USE_PC_HOST, EALLOC_USE_FREERTOS, EALLOC_USE_BAREMETAL, EALLOC_USE_ARDUINO must be ON!")
endif()

if(EALLOC_USE_PC_HOST)
    set(EALLOC_PLATFORM_DEF EALLOC_PC_HOST)
elseif(EALLOC_USE_FREERTOS)
    set(EALLOC_PLATFORM_DEF FREERTOS)
elseif(EALLOC_USE_BAREMETAL)
    set(EALLOC_PLATFORM_DEF BAREMETAL)
elseif(EALLOC_USE_ARDUINO)
    set(EALLOC_PLATFORM_DEF ARDUINO)
endif()

# Add source files
file(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.cpp)

# Create library
add_library(eAlloc STATIC ${app_sources})
target_compile_definitions(eAlloc PUBLIC ${EALLOC_PLATFORM_DEF})
target_include_directories(eAlloc PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/_deps/logger-src/src
)
# Only link Logger if building for ESP-IDF; for host/PC, it's header-only.

# Enable ownership tag feature
add_definitions(-DEALLOC_ENABLE_OWNERSHIP_TAG=1)

# Only build tests if this is the main project
if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    add_executable(eAlloc_test
        ${CMAKE_SOURCE_DIR}/tests/eAlloc_test.cpp
        ${CMAKE_SOURCE_DIR}/tests/StackAllocator_test.cpp
    )
    target_link_libraries(eAlloc_test gtest_main eAlloc)
    target_include_directories(eAlloc_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/_deps/logger-src/src
    )
    target_compile_definitions(eAlloc_test PRIVATE ${EALLOC_PLATFORM_DEF})
    # GoogleTest CTest integration
    include(GoogleTest)
    gtest_discover_tests(eAlloc_test)
endif()




# Set C++ standard
set(CMAKE_CXX_STANDARD 17)

# Add subdirectory for source


# Add Logger library

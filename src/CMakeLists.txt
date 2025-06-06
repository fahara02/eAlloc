


# Set C++ standard
set(CMAKE_CXX_STANDARD 17)

# Automate CA bundle usage for FetchContent
# If a local cacert.pem is present, set CURL_CA_BUNDLE env var so FetchContent downloads (like GoogleTest) work without user intervention.
if(EXISTS "${CMAKE_SOURCE_DIR}/cacert.pem")
    set(ENV{CURL_CA_BUNDLE} "${CMAKE_SOURCE_DIR}/cacert.pem")
endif()

# Use FetchContent to download GoogleTest automatically
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/release-1.11.0.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Add source files
file(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.cpp)

# Create library
add_library(eAlloc STATIC ${app_sources})

# ESP32/ESP-IDF ONLY CMakeLists.txt
# This file is for building eAlloc for ESP32/ESP-IDF ONLY. All host/PC logic is removed.
# Make sure you are using the ESP-IDF toolchain and the correct ESP-IDF include paths.




# GoogleTest setup
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/release-1.11.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Add source files
file(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.cpp)

# Create library
add_library(eAlloc STATIC ${app_sources})

# Force ESP32 macro for all source files
add_definitions(-DESP32)
message(STATUS "[ESP32] Forcing -DESP32 for ESP32/ESP-IDF build!")

# Add ESP-IDF FreeRTOS include path (adjust if your ESP-IDF is elsewhere)
target_include_directories(eAlloc PUBLIC
    "C:/Espressif/frameworks/esp-idf-v5.4.1/components/freertos/FreeRTOS-Kernel/include/freertos"
    ${CMAKE_SOURCE_DIR}/src
)

# Add test executable
add_executable(eAlloc_test
    ${CMAKE_SOURCE_DIR}/tests/eAlloc_test.cpp
    ${CMAKE_SOURCE_DIR}/tests/FreeRTOS_stubs.cpp
)
target_link_libraries(eAlloc_test gtest_main eAlloc)
target_include_directories(eAlloc_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Enable testing
include(GoogleTest)
gtest_discover_tests(eAlloc_test)

add_executable(eAlloc_test
    ${CMAKE_SOURCE_DIR}/tests/eAlloc_test.cpp
    ${CMAKE_SOURCE_DIR}/tests/FreeRTOS_stubs.cpp
)
target_link_libraries(eAlloc_test gtest_main eAlloc)
target_include_directories(eAlloc_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Enable testing
include(GoogleTest)
gtest_discover_tests(eAlloc_test)

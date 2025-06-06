#!/bin/sh

# Set CA bundle for CMake FetchContent (GoogleTest)
export CURL_CA_BUNDLE="$(cd .. && pwd)/cacert.pem"

# Fallback: Disable SSL verification if CA bundle is not picked up (not secure, but works)
export CMAKE_TLS_VERIFY=0

# Create a build directory
mkdir -p build

# Configure the project with CMake and Ninja (from project root)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_POLICY_DEFAULT_CMP0077=NEW -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_SUPPRESS_DEVELOPER_WARNINGS=ON -Wno-dev

# Build the project
cmake --build build

# Automatically run tests after build
./build/eAlloc_test.exe

# Run the tests
# ctest

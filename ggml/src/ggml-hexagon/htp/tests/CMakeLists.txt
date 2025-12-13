cmake_minimum_required(VERSION 3.22.2)
project(ggml-htp-tests C CXX)

# Select host prebuilt dir for FastRPC libs
if (CMAKE_SYSTEM_NAME MATCHES "Android")
    if (ANDROID_ABI STREQUAL "arm64-v8a")
        set(PREBUILT_LIB_DIR "android_aarch64")
    elseif (ANDROID_ABI STREQUAL "armeabi-v7a")
        set(PREBUILT_LIB_DIR "android")
    endif()
elseif (CMAKE_SYSTEM_NAME MATCHES "UbuntuARM")
    set(PREBUILT_LIB_DIR "UbuntuARM_aarch64")
endif()

include(${HEXAGON_SDK_ROOT}/build/cmake/hexagon_fun.cmake)

add_executable(test-htp-inverse test-inverse.cpp)

# The host side uses generated headers from build dir
target_include_directories(test-htp-inverse PRIVATE
    ${CMAKE_BINARY_DIR}/ggml/src/ggml-hexagon)

# Ensure stubs are generated before building tests
add_dependencies(test-htp-inverse htp_iface)

if (CMAKE_SYSTEM_NAME MATCHES Android)
    target_link_libraries(test-htp-inverse log dl)
elseif (CMAKE_SYSTEM_NAME MATCHES Windows)
    target_precompile_headers(test-htp-inverse PRIVATE <sal.h>)
else()
    target_link_libraries(test-htp-inverse dl)
endif()

# Link Hexagon SDK host libs via helper to match project style
link_custom_library(test-htp-inverse cdsprpc)
link_custom_library(test-htp-inverse rpcmem)

# OBJECT library contains QAIC stubs
target_link_libraries(test-htp-inverse htp_iface)

# Register CTest
include(CTest)
add_test(NAME htp-inverse COMMAND test-htp-inverse)

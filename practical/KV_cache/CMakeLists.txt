cmake_minimum_required(VERSION 3.16)

project(kv_cache_bench LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(KV_CACHE_ENABLE_LTO "Enable IPO/LTO for release build" ON)

find_package(benchmark REQUIRED)

add_executable(kv_cache_bench main.cpp)
target_include_directories(kv_cache_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/assets)
target_link_libraries(kv_cache_bench PRIVATE benchmark::benchmark)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(kv_cache_bench PRIVATE
        -O3
        -march=native
        -Wall
        -Wextra
        -Wpedantic
    )
elseif(MSVC)
    target_compile_options(kv_cache_bench PRIVATE /O2 /W4)
endif()

if(KV_CACHE_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)
    if(IPO_SUPPORTED)
        set_property(TARGET kv_cache_bench PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(STATUS "IPO/LTO disabled: ${IPO_ERROR}")
    endif()
endif()


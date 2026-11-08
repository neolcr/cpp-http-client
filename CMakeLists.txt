cmake_minimum_required(VERSION 3.20)
project(windows_mtls_client VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(mtls_core src/core.cpp)
target_include_directories(mtls_core PUBLIC include)

if(MSVC)
    target_compile_options(mtls_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(mtls_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    add_executable(mtls_client src/main.cpp)
    target_link_libraries(mtls_client PRIVATE mtls_core winhttp crypt32 ncrypt advapi32)
    target_compile_definitions(mtls_client PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(mtls_client PRIVATE /W4 /permissive- /utf-8)
    endif()
else()
    message(STATUS "mtls_client is Windows-only; building portable tests only")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE mtls_core)
    add_test(NAME core_tests COMMAND core_tests)
endif()


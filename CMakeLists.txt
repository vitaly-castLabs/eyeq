cmake_minimum_required(VERSION 3.14)

set(CMAKE_CXX_COMPILER_NAMES clang++ g++ icpc c++ cxx)
project(eyeq VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(LIBAVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(LIBAVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(LIBSWSCALE REQUIRED IMPORTED_TARGET libswscale)
pkg_check_modules(LIBVMAF REQUIRED IMPORTED_TARGET libvmaf)

add_executable(eyeq src/eyeq.cpp)

target_compile_options(eyeq PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(
    eyeq
    PRIVATE
        PkgConfig::LIBAVFORMAT
        PkgConfig::LIBAVCODEC
        PkgConfig::LIBAVUTIL
        PkgConfig::LIBSWSCALE
        PkgConfig::LIBVMAF
)

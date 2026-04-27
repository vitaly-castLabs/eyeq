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

include(FetchContent)
FetchContent_Declare(
    ssimulacra2
    GIT_REPOSITORY https://github.com/cloudinary/ssimulacra2.git
    GIT_TAG        81feacf6a5b0bbd7ec11b4a881a4615033ab0b10
    SOURCE_SUBDIR  src
)
FetchContent_MakeAvailable(ssimulacra2)
if(TARGET ssimulacra2)
    set_target_properties(ssimulacra2 PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

add_library(ssimulacra2_core STATIC
    ${ssimulacra2_SOURCE_DIR}/src/ssimulacra2.cc
)
target_include_directories(ssimulacra2_core SYSTEM PUBLIC
    ${ssimulacra2_SOURCE_DIR}/src
)
target_link_libraries(ssimulacra2_core PUBLIC
    jxl
    jxl_extras-static
    hwy
)

add_executable(eyeq src/eyeq.cpp src/ssimulacra2.cpp)

target_compile_options(eyeq PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(
    eyeq
    PRIVATE
        ssimulacra2_core
        PkgConfig::LIBAVFORMAT
        PkgConfig::LIBAVCODEC
        PkgConfig::LIBAVUTIL
        PkgConfig::LIBSWSCALE
        PkgConfig::LIBVMAF
)

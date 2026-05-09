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

set(EYEQ_BUNDLED_VMAF_PREFIX "${CMAKE_SOURCE_DIR}/third_party/vmaf-install")
if(EXISTS "${EYEQ_BUNDLED_VMAF_PREFIX}/lib/pkgconfig/libvmaf.pc")
    set(ENV{PKG_CONFIG_PATH} "${EYEQ_BUNDLED_VMAF_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    message(STATUS "Using bundled libvmaf at ${EYEQ_BUNDLED_VMAF_PREFIX}")
else()
    message(STATUS "Bundled libvmaf not found; run scripts/install-vmaf.sh or install libvmaf system-wide")
endif()

pkg_check_modules(LIBAVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(LIBAVCODEC REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(LIBAVUTIL REQUIRED IMPORTED_TARGET libavutil)
pkg_check_modules(LIBSWSCALE REQUIRED IMPORTED_TARGET libswscale)
pkg_check_modules(FFTW3F REQUIRED IMPORTED_TARGET fftw3f)
pkg_check_modules(LIBVMAF REQUIRED IMPORTED_TARGET libvmaf)

add_executable(eyeq src/eyeq.cpp src/xpsnr.cpp src/mdsi.cpp src/fsim.cpp src/rgb24.cpp src/dssim.cpp)

target_compile_options(eyeq PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(
    eyeq
    PRIVATE
        PkgConfig::LIBAVFORMAT
        PkgConfig::LIBAVCODEC
        PkgConfig::LIBAVUTIL
        PkgConfig::LIBSWSCALE
        PkgConfig::FFTW3F
        PkgConfig::LIBVMAF
)

include(GNUInstallDirs)
install(TARGETS eyeq RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

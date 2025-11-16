cmake_minimum_required(VERSION 3.27)
project(HermesFlow
    VERSION 1.0.0
    DESCRIPTION "Audio workflows processing server"
    LANGUAGES CXX
)

# ----------------------------------------------------------------------
# Project configuration
# ----------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# ----------------------------------------------------------------------
# Sanitizer *Options* (Defined globally, applied in src/CMakeLists.txt)
# ----------------------------------------------------------------------
option(ENABLE_ASAN  "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN  "Enable ThreadSanitizer" OFF)
set(SANITIZER_FLAGS "")

if(ENABLE_ASAN)
    list(APPEND SANITIZER_FLAGS "address")
endif()
if(ENABLE_UBSAN)
    list(APPEND SANITIZER_FLAGS "undefined" "integer" "nullability")
endif()
if(ENABLE_TSAN)
    list(APPEND SANITIZER_FLAGS "thread")
endif()

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "ASan and TSan cannot be used together")
endif()

# ----------------------------------------------------------------------
# Platform‑specific bits
# ----------------------------------------------------------------------
if(WIN32)
    message(STATUS "Windows – setting _WIN32_WINNT")
    add_definitions(-D_WIN32_WINNT=0x0601)
endif()

# ----------------------------------------------------------------------
# Load Dependencies
# ----------------------------------------------------------------------
# This one line finds, fetches, and builds all 3rd-party code
# and creates the 'hermes_dependencies' target.
include(cmake/dependencies.cmake)

# ----------------------------------------------------------------------
# Build Our Code
# ----------------------------------------------------------------------
# This adds our 'models', 'utils', 'server', and 'Server' targets
add_subdirectory(src)

# ----------------------------------------------------------------------
# Summary
# ----------------------------------------------------------------------
message(STATUS "=== Hermes-flow Configuration ===")
message(STATUS "C++ standard : ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler     : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
if(HERMES_USE_IO_URING)
    message(STATUS "io_uring     : Enabled")
endif()

if(SANITIZER_FLAGS)
    message(STATUS "Sanitizers   : ${SANITIZER_FLAGS}")
endif()
message(STATUS "===============================")


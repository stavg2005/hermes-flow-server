cmake_minimum_required(VERSION 3.27)

project(HermesFlow
    VERSION 1.0.0
    DESCRIPTION "Audio workflows processing server"
    LANGUAGES CXX
)

# =============================================================================
# 1. Build Options & Sanitizers
# =============================================================================

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(ENABLE_ASAN  "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN  "Enable ThreadSanitizer" OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "ASan and TSan cannot be used together")
endif()

# Windows specific definitions
if(WIN32)
    add_definitions(-D_WIN32_WINNT=0x0601 -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
endif()

# =============================================================================
# 2. Dependencies
# =============================================================================

include(FetchContent)

# A. Spdlog (Logging)
set(SPDLOG_USE_STD_FORMAT ON CACHE BOOL "" FORCE)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
    GIT_SHALLOW    TRUE
)

# B. Boost (Networking & JSON)
set(BOOST_INCLUDE_LIBRARIES asio beast json system url uuid filesystem atomic) # Added atomic explicitly
FetchContent_Declare(boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.88.0/boost-1.88.0-cmake.zip
    DOWNLOAD_EXTRACT_TIMESTAMP ON
)

# C. AWS SigV4 (S3 Authentication)
FetchContent_Declare(aws_sigv4
    GIT_REPOSITORY https://github.com/zxwind/aws-sigv4-cpp.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

# D. toml++ (Configuration)
FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(spdlog boost aws_sigv4 tomlplusplus)

# E. OpenSSL
find_package(OpenSSL REQUIRED)

# F. AWS SigV4 Library Wrapper
add_library(awssigv4 STATIC
    ${aws_sigv4_SOURCE_DIR}/awssigv4.cc
    ${aws_sigv4_SOURCE_DIR}/awssigv4.h
)
target_include_directories(awssigv4 PUBLIC ${aws_sigv4_SOURCE_DIR})
target_link_libraries(awssigv4 PUBLIC OpenSSL::SSL OpenSSL::Crypto)

# G. io_uring (Linux Async I/O optimization)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(URING QUIET liburing)
        if(URING_FOUND)
            set(HERMES_USE_IO_URING TRUE)
            message(STATUS "io_uring: Enabled")
        endif()
    endif()
endif()

# =============================================================================
# 3. Common Library Interface
# =============================================================================

add_library(hermes_dependencies INTERFACE)

# -----------------------------------------------------------------------------
# Platform Hack: Atomic Library Standardization
# -----------------------------------------------------------------------------
# Issue:
# - Linux/Clang often requires explicitly linking '-latomic' for 64-bit atomics.
# - Windows/MSVC has 64-bit atomics built into the runtime; 'atomic.lib' does not exist.
#
# Solution:
# We create a dummy "atomic" interface target on Windows. This allows the rest
# of the build script to blindly link against 'atomic' without checking the OS
# every time.
if(WIN32 AND NOT TARGET atomic)
    add_library(atomic INTERFACE)
endif()

target_link_libraries(hermes_dependencies INTERFACE
    spdlog::spdlog
    Boost::asio
    Boost::beast
    Boost::json
    Boost::system
    Boost::url
    Boost::filesystem
    Boost::uuid
    Boost::atomic
    OpenSSL::SSL
    OpenSSL::Crypto
    awssigv4
    tomlplusplus::tomlplusplus
)

# LINK ATOMIC CONDITIONALLY (The Fix)
if(NOT WIN32)
    # On Linux/macOS, we might need libatomic for 64-bit atomics on some archs
    target_link_libraries(hermes_dependencies INTERFACE atomic)
endif()

if(HERMES_USE_IO_URING)
    target_compile_definitions(hermes_dependencies INTERFACE BOOST_ASIO_HAS_IO_URING=1)
    target_include_directories(hermes_dependencies INTERFACE ${URING_INCLUDE_DIRS})
    target_link_libraries(hermes_dependencies INTERFACE ${URING_LIBRARIES})
endif()

# =============================================================================
# 4. Source Files
# =============================================================================

# Infrastructure
set(SOURCES_INFRA
    src/infra/audio/alaw.cpp
    src/infra/io/io_context_pool.cpp
    src/infra/parsers/Json2Graph.cpp
    src/core/config/config.cpp
)

# Network
set(SOURCES_NETWORK
    src/network/s3/S3Client.cpp
    src/network/s3/S3Session.cpp
    src/network/rtp/packet.cpp
    src/network/rtp/RTPPacketizer.cpp
    src/network/http/http_session.cpp
    src/network/http/Listener.cpp
    src/network/http/Router.cpp
    src/network/http/Server.cpp
)

# Core
set(SOURCES_CORE
    src/core/session/ActiveSessions.cpp
    src/core/session/Session.cpp
    src/core/graph/NodeRegistry.cpp
    src/core/graph/Nodes.cpp
    src/core/graph/AudioExecutor.cpp
)

# =============================================================================
# 5. The Engine Library
# =============================================================================

add_library(hermes_engine STATIC
    ${SOURCES_INFRA}
    ${SOURCES_NETWORK}
    ${SOURCES_CORE}
)

target_include_directories(hermes_engine PUBLIC
    src
    src/core/session
    src/core/graph
    src/core/config
    src/network/http
    src/network/rtp
    src/network/s3
    src/network/websocket
    src/infra/audio
    src/infra/io
    src/infra/parsers
)

target_link_libraries(hermes_engine PUBLIC hermes_dependencies)

# =============================================================================
# 6. Main Executable
# =============================================================================

add_executable(Server src/app/main.cpp)
target_link_libraries(Server PRIVATE hermes_engine)

# =============================================================================
# 7. Sanitizer Configuration
# =============================================================================

if(ENABLE_ASAN OR ENABLE_UBSAN OR ENABLE_TSAN)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
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

        list(JOIN SANITIZER_FLAGS "," SANITIZER_FLAGS_STR)
        target_compile_options(Server PRIVATE "-fsanitize=${SANITIZER_FLAGS_STR}" "-fno-omit-frame-pointer" "-g")
        target_link_options(Server PRIVATE "-fsanitize=${SANITIZER_FLAGS_STR}")
    endif()
endif()

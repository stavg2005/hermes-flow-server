cmake_minimum_required(VERSION 3.27)

project(HermesFlow
    VERSION 1.0.0
    DESCRIPTION "Audio workflows processing server"
    LANGUAGES CXX
)

# =============================================================================
# Build Configuration
# =============================================================================

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# =============================================================================
# Options & Sanitizers
# =============================================================================

option(ENABLE_ASAN  "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN  "Enable ThreadSanitizer" OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "ASan and TSan cannot be used together")
endif()

if(WIN32)
    add_definitions(
        -D_WIN32_WINNT=0x0601
        -DWIN32_LEAN_AND_MEAN
        -DNOMINMAX
        -D_CRT_SECURE_NO_WARNINGS
    )
endif()

# =============================================================================
# Dependencies (FetchContent)
# =============================================================================

include(FetchContent)

# 1. spdlog
set(SPDLOG_USE_STD_FORMAT ON CACHE BOOL "" FORCE)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
    GIT_SHALLOW    TRUE
)

# 2. Boost
set(BOOST_INCLUDE_LIBRARIES asio beast json system url uuid filesystem)
FetchContent_Declare(boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.88.0/boost-1.88.0-cmake.zip
    DOWNLOAD_EXTRACT_TIMESTAMP ON
)

# 3. AWS SigV4
FetchContent_Declare(aws_sigv4
    GIT_REPOSITORY https://github.com/zxwind/aws-sigv4-cpp.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
    GIT_SHALLOW    TRUE
)


FetchContent_MakeAvailable(spdlog boost aws_sigv4 tomlplusplus)

# 4. OpenSSL
find_package(OpenSSL REQUIRED)

# 5. AWS SigV4 Lib Setup
add_library(awssigv4 STATIC
    ${aws_sigv4_SOURCE_DIR}/awssigv4.cc
    ${aws_sigv4_SOURCE_DIR}/awssigv4.h
)
target_include_directories(awssigv4 PUBLIC ${aws_sigv4_SOURCE_DIR})
target_link_libraries(awssigv4 PUBLIC OpenSSL::SSL OpenSSL::Crypto)

# 6. io_uring (Linux only)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(URING QUIET liburing)
        if(URING_FOUND)
            set(HERMES_USE_IO_URING TRUE)
            message(STATUS "io_uring: Enabled")
        else()
            set(HERMES_USE_IO_URING FALSE)
            message(WARNING "io_uring: Not found, disabled")
        endif()
    endif()
endif()

# =============================================================================
# Common Interface (Flags & Links)
# =============================================================================

add_library(hermes_dependencies INTERFACE)

target_link_libraries(hermes_dependencies INTERFACE
    spdlog::spdlog
    Boost::asio
    Boost::beast
    Boost::json
    Boost::system
    Boost::url
    Boost::filesystem
    OpenSSL::SSL
    OpenSSL::Crypto
    awssigv4
    tomlplusplus::tomlplusplus
)

if(HERMES_USE_IO_URING)
    target_compile_definitions(hermes_dependencies INTERFACE BOOST_ASIO_HAS_IO_URING=1)
    target_include_directories(hermes_dependencies INTERFACE ${URING_INCLUDE_DIRS})
    target_link_libraries(hermes_dependencies INTERFACE ${URING_LIBRARIES})
endif()

# =============================================================================
# Project Sources (Grouped by Folder)
# =============================================================================

# Infrastructure (Audio, IO, Parsers)
set(SOURCES_INFRA
    src/infra/audio/alaw.cpp
    src/infra/io/io_context_pool.cpp
    src/infra/parsers/Json2Graph.cpp
    src/core/config/config.cpp
)

# Network (HTTP, RTP, S3)
set(SOURCES_NETWORK
    src/network/s3/S3Client.cpp
    src/network/s3/S3Session.cpp
    src/network/rtp/packet.cpp
    src/network/rtp/RTPPacketizer.cpp
    src/network/rtp/RTPTransmitter.cpp
    src/network/http/http_session.cpp
    src/network/http/Listener.cpp
    src/network/http/Router.cpp
    src/network/http/Server.cpp
)

# Core Logic (Sessions, Graph)
set(SOURCES_CORE
    src/core/session/ActiveSessions.cpp
    src/core/session/Session.cpp
    src/core/graph/NodeRegistry.cpp
    src/core/graph/Nodes.cpp
    src/core/graph/AudioExecutor.cpp
)

# =============================================================================
# Main Library (The Engine)
# =============================================================================

add_library(hermes_engine STATIC
    ${SOURCES_INFRA}
    ${SOURCES_NETWORK}
    ${SOURCES_CORE}
)

target_include_directories(hermes_engine PUBLIC
    src                             # Allows: #include "core/session/Session.hpp"
    src/core/session                # Allows: #include "Session.hpp"
    src/core/graph                  # Allows: #include "Nodes.hpp"
    src/core/config                 # Allows: #include "Config.hpp"
    src/network/http
    src/network/rtp
    src/network/s3
    src/network/websocket
    src/infra/audio
    src/infra/io
    src/infra/parsers
)

target_link_libraries(hermes_engine PUBLIC hermes_dependencies)


target_link_libraries(hermes_engine PUBLIC hermes_dependencies)

# =============================================================================
# Main Executable
# =============================================================================

# Note: Assuming you moved main.cpp to src/app/
add_executable(Server src/app/main.cpp)

target_link_libraries(Server PRIVATE hermes_engine)

# =============================================================================
# Sanitizer Flags (Applied to Executable)
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
        set(SANITIZER_COMPILE_FLAGS "-fsanitize=${SANITIZER_FLAGS_STR}" "-fno-omit-frame-pointer" "-g")

        target_compile_options(Server PRIVATE ${SANITIZER_COMPILE_FLAGS})
        target_link_options(Server PRIVATE ${SANITIZER_COMPILE_FLAGS})
    endif()
endif()

# =============================================================================
# Build Summary
# =============================================================================

message(STATUS "")
message(STATUS "=== HermesFlow Configuration ===")
message(STATUS "Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard : ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler     : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
if(HERMES_USE_IO_URING)
    message(STATUS "io_uring     : Enabled")
endif()
if(ENABLE_ASAN OR ENABLE_UBSAN OR ENABLE_TSAN)
    message(STATUS "Sanitizers   : ${SANITIZER_FLAGS}")
endif()
message(STATUS "================================")
message(STATUS "")

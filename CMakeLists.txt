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
# Options
# =============================================================================

option(ENABLE_ASAN  "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN  "Enable ThreadSanitizer" OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "ASan and TSan cannot be used together")
endif()

# =============================================================================
# Platform Configuration
# =============================================================================

if(WIN32)
    add_definitions(
        -D_WIN32_WINNT=0x0601
        -DWIN32_LEAN_AND_MEAN
        -DNOMINMAX
    )
endif()

# =============================================================================
# Dependencies
# =============================================================================

include(FetchContent)

# spdlog
set(SPDLOG_USE_STD_FORMAT ON CACHE BOOL "" FORCE)
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
    GIT_SHALLOW    TRUE
)

# Boost
set(BOOST_INCLUDE_LIBRARIES asio beast json system url uuid filesystem)
FetchContent_Declare(boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.88.0/boost-1.88.0-cmake.zip
    DOWNLOAD_EXTRACT_TIMESTAMP ON
)

# AWS SigV4
FetchContent_Declare(aws_sigv4
    GIT_REPOSITORY https://github.com/zxwind/aws-sigv4-cpp.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(spdlog boost aws_sigv4)

# OpenSSL
find_package(OpenSSL REQUIRED)

# AWS SigV4 Library Target
add_library(awssigv4 STATIC
    ${aws_sigv4_SOURCE_DIR}/awssigv4.cc
    ${aws_sigv4_SOURCE_DIR}/awssigv4.h
)
target_include_directories(awssigv4 PUBLIC ${aws_sigv4_SOURCE_DIR})
target_link_libraries(awssigv4 PUBLIC OpenSSL::SSL OpenSSL::Crypto)

# io_uring (Linux only)
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
# Common Dependencies Interface
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
)

if(HERMES_USE_IO_URING)
    target_compile_definitions(hermes_dependencies INTERFACE
        BOOST_ASIO_HAS_IO_URING=1
    )
    target_include_directories(hermes_dependencies INTERFACE
        ${URING_INCLUDE_DIRS}
    )
    target_link_libraries(hermes_dependencies INTERFACE
        ${URING_LIBRARIES}
    )
endif()

# =============================================================================
# Project Libraries
# =============================================================================

# Models (Header-only)
add_library(models INTERFACE)
target_include_directories(models INTERFACE src/models)

# Utils
add_library(utils STATIC
    src/utils/Json2Graph.cpp
    src/utils/S3Session.cpp
    src/utils/S3Client.cpp
)
target_include_directories(utils PUBLIC src/utils)
target_link_libraries(utils PUBLIC models hermes_dependencies)

# Server
add_library(server STATIC
    src/server/ActiveSessions.cpp
    src/server/http_session.cpp
    src/server/io_context_pool.cpp
    src/server/Listener.cpp
    src/server/Router.cpp
    src/server/Session.cpp
)
target_include_directories(server PUBLIC src/server)
target_link_libraries(server PUBLIC utils hermes_dependencies)

# =============================================================================
# Executable
# =============================================================================

add_executable(Server src/main.cpp)
target_link_libraries(Server PRIVATE server)

# =============================================================================
# Sanitizers
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

        message(STATUS "Sanitizers: ${SANITIZER_FLAGS_STR}")
    else()
        message(WARNING "Sanitizers only supported with Clang/GCC")
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

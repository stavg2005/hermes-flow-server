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
# clang‑tidy is DISABLED
# ----------------------------------------------------------------------


# ----------------------------------------------------------------------
# Sanitizers (optional)
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
    add_definitions(-D_WIN32_WINNT=0x0601)   # **only once**
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "Linux – checking for liburing")
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(URING QUIET liburing)
    endif()
    if(URING_FOUND)
        set(HERMES_USE_IO_URING TRUE CACHE BOOL "Use io_uring")
        message(STATUS "liburing found – io_uring enabled")
    else()
        set(HERMES_USE_IO_URING FALSE CACHE BOOL "Use io_uring")
        message(WARNING "liburing not found – io_uring disabled")
    endif()
endif()

# ----------------------------------------------------------------------
# Dependencies
# ----------------------------------------------------------------------
include(FetchContent)

# ---- spdlog (use std::format – no external fmt) -----------------------
set(SPDLOG_USE_STD_FORMAT ON CACHE BOOL "Use std::format for spdlog" FORCE)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spdlog)

# ---- Boost -----------------------------------------------------------
set(BOOST_ENABLE_CMAKE ON)
FetchContent_Declare(
    boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.88.0/boost-1.88.0-cmake.zip
    DOWNLOAD_EXTRACT_TIMESTAMP ON
)
FetchContent_MakeAvailable(boost)


# ----------------------------------------------------------------------
# Sources
# ----------------------------------------------------------------------
set(SERVER_SOURCES
    src/main.cpp
    src/server/Listener.cpp
    src/server/http_session.cpp
    src/server/Router.cpp
    src/server/Session.cpp
    src/server/io_context_pool.cpp
    src/server/ActiveSessions.cpp
)

add_executable(Server ${SERVER_SOURCES})

find_program(CLANG_TIDY_EXE clang-tidy)
if(CLANG_TIDY_EXE)
    message(STATUS "Found clang-tidy: ${CLANG_TIDY_EXE}")

    set_target_properties(Server PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
else()
    message(STATUS "clang-tidy not found – linting disabled")
endif()
target_include_directories(Server
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/server
)

if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Don't try to link atomic on Windows with Clang
    set_target_properties(Server PROPERTIES
        LINK_FLAGS "-Wl,--allow-undefined-symbols"
    )
endif()
target_link_libraries(Server
    PRIVATE
        Boost::headers
        Boost::asio
        Boost::beast
        Boost::system
        Boost::json
        Boost::url
        spdlog::spdlog
)

# ----------------------------------------------------------------------
# Platform‑specific target settings
# ----------------------------------------------------------------------
if(WIN32)
    target_compile_definitions(Server
        PRIVATE
            WIN32_LEAN_AND_MEAN
            DNOMINMAX
    )
endif()

if(HERMES_USE_IO_URING)
    target_compile_definitions(Server
        PRIVATE
            BOOST_ASIO_HAS_IO_URING
            BOOST_ASIO_DISABLE_EPOLL
    )
    target_link_libraries(Server PRIVATE ${URING_LIBRARIES})
endif()

# ----------------------------------------------------------------------
# Sanitizers (apply only to the Server target)
# ----------------------------------------------------------------------
if(SANITIZER_FLAGS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        list(JOIN SANITIZER_FLAGS "," SANITIZER_FLAGS_STR)
        set(SANITIZER_FLAGS_STR "-fsanitize=${SANITIZER_FLAGS_STR}")
        message(STATUS "Enabling sanitizers: ${SANITIZER_FLAGS_STR}")

        target_compile_options(Server PRIVATE ${SANITIZER_FLAGS_STR} -fno-omit-frame-pointer -g)
        target_link_options(Server    PRIVATE ${SANITIZER_FLAGS_STR})
    else()
        message(WARNING "Sanitizers only supported with Clang/GCC – disabled")
    endif()
endif()

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

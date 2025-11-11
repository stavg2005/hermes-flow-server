cmake_minimum_required(VERSION 3.27)
project(HermesFlow
    VERSION 1.0.0
    DESCRIPTION "Audio workflows processing server"
    LANGUAGES CXX
)

# =============================================================================
# Project Configuration
# =============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# =============================================================================
# Platform-specific Configuration
# =============================================================================
if(WIN32)
    message(STATUS "Windows detected. Configuring for Windows.")
    add_definitions(-D_WIN32_WINNT=0x0601)  # Windows 7 or later
    add_definitions(-DWIN32_LEAN_AND_MEAN)  # Exclude rarely-used Windows headers
    add_definitions(-DNOMINMAX)             # Don't define min/max macros

elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "Linux detected. Checking for liburing...")
    # Find PkgConfig to help us find liburing
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        # Check for liburing, but quietly so it doesn't fail if not found
        pkg_check_modules(URING QUIET liburing)
    endif()

    if(URING_FOUND)
        message(STATUS "Found liburing. Enabling io_uring support.")
        # Set a flag that we'll use later
        set(HERMES_USE_IO_URING TRUE CACHE BOOL "Use io_uring")
    else()
        message(WARNING "liburing not found. io_uring support will be disabled. \
Install liburing-dev (or equivalent) for maximum performance.")
        set(HERMES_USE_IO_URING FALSE CACHE BOOL "Use io_uring")
    endif()
endif()

# =============================================================================
# Dependencies
# =============================================================================
include(FetchContent)

# Boost configuration
set(BOOST_ENABLE_CMAKE ON)
FetchContent_Declare(
    boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.88.0/boost-1.88.0-cmake.zip
    DOWNLOAD_EXTRACT_TIMESTAMP ON
)


# spdlog configuration
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
    GIT_SHALLOW    TRUE
)

# Make dependencies available
# --- FIX: ADDED 'fmt' ---
FetchContent_MakeAvailable(boost spdlog)

# =============================================================================
# Source Files Organization
# =============================================================================

set(SERVER_SOURCES
    src/main.cpp
    src/server/Listener.cpp
    src/server/http_session.cpp
    src/server/Router.cpp
    src/server/Session.cpp
    src/server/io_context_pool.cpp
    src/server/ActiveSessions.cpp
)

# Collect all implementation files
set(ALL_SOURCES
    ${SERVER_SOURCES}
)

#  List header files for IDE integration (helps with project navigation)
set(HEADER_FILES
    # You can list .hpp files here
)

# =============================================================================
# Target Definition
# =============================================================================

add_executable(Server ${ALL_SOURCES} ${HEADER_FILES})

# Modern CMake: Use target-specific commands instead of global include_directories
target_include_directories(Server
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/server
        # --- FIX: REMOVED redundant Boost include ---
        # The Boost::asio, etc. targets handle their own includes.
)

# Link libraries
target_link_libraries(Server
    PRIVATE
        Boost::headers
        Boost::asio
        Boost::beast
        Boost::system
        Boost::json
        Boost::url
        Boost::uuid
        spdlog::spdlog     
)

# =============================================================================
# Platform-specific Target Settings
# =============================================================================

# Conditionally add io_uring support if the flag was set earlier
if(HERMES_USE_IO_URING)
    message(STATUS "Adding io_uring compile definitions and linking liburing.")

    # 1. Add the compile definitions to enable io_uring in Asio
    target_compile_definitions(Server
        PRIVATE
            BOOST_ASIO_HAS_IO_URING
            BOOST_ASIO_DISABLE_EPOLL # Use io_uring as the default backend
    )

    # 2. Link your server against the liburing library
    target_link_libraries(Server
        PRIVATE
            ${URING_LIBRARIES} # Link liburing
    )
endif()


# Print configuration summary
message(STATUS "=== Hermes-flow Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
if(HERMES_USE_IO_URING)
    message(STATUS "io_uring: Enabled")
endif()
message(STATUS "===============================")

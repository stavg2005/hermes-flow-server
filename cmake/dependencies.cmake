# This file finds/fetches all 3rd-party dependencies
# and bundles them into a single INTERFACE target.

message(STATUS "Loading third-party dependencies...")
include(FetchContent)

# ---- spdlog -----------------------------------
set(SPDLOG_USE_STD_FORMAT ON CACHE BOOL "Use std::format for spdlog" FORCE)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spdlog)

# ---- Boost ------------------------------------
set(BOOST_INCLUDE_LIBRARIES asio beast json system url uuid filesystem)
FetchContent_Declare(
    boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.88.0/boost-1.88.0-cmake.zip
    DOWNLOAD_EXTRACT_TIMESTAMP ON
)
FetchContent_MakeAvailable(boost)

# ---- aws-client-sigv4 -------------------------
# (This file now finds OpenSSL and creates the 'awssigv4' target)
include(cmake/aws-client-sigv4.cmake)

# ---- Linux (io_uring) -------------------------
set(HERMES_URING_LIBRARIES "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(URING QUIET liburing)
    endif()

    if(URING_FOUND)
        set(HERMES_USE_IO_URING TRUE CACHE BOOL "Use io_uring")
        set(HERMES_URING_LIBRARIES ${URING_LIBRARIES})
        message(STATUS "liburing found – io_uring enabled")
    else()
        set(HERMES_USE_IO_URING FALSE CACHE BOOL "Use io_uring")
        message(WARNING "liburing not found – io_uring disabled")
    endif()
endif()


# ----------------------------------------------------------------------
# THE DEPENDENCY HUB TARGET
# ----------------------------------------------------------------------
add_library(hermes_dependencies INTERFACE)

target_link_libraries(hermes_dependencies
  INTERFACE
    # Our fetched targets
    spdlog::spdlog
    Boost::asio
    Boost::beast
    Boost::json
    Boost::system
    Boost::url
    Boost::uuid
    Boost::filesystem
    awssigv4

    # System/Found targets
    OpenSSL::SSL
    OpenSSL::Crypto
    ${HERMES_URING_LIBRARIES} # This will be blank if not on Linux
)

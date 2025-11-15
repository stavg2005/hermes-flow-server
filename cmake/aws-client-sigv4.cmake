# This file neatly encapsulates the manual build process
# for the aws-client-sigv4 library.

message(STATUS "Fetching and building 'awssigv4' library")

FetchContent_Declare(
  aws_client_sigv4
  GIT_REPOSITORY https://github.com/harendra247/aws-client-sigv4-cpp.git
  GIT_TAG        master
  GIT_SHALLOW   TRUE
)
FetchContent_MakeAvailable(aws_client_sigv4)

# Find its dependency, OpenSSL
find_package(OpenSSL REQUIRED)

# Create the library target
add_library(awssigv4 STATIC
  ${aws_client_sigv4_SOURCE_DIR}/awssigv4.cpp
)

# Tell CMake where its headers are
target_include_directories(awssigv4
  PUBLIC
    ${aws_client_sigv4_SOURCE_DIR}
)

# --- THIS IS THE FIX ---
# You found OpenSSL in your file but never linked it.
# This library needs to be linked to OpenSSL.
target_link_libraries(awssigv4
  PUBLIC
    OpenSSL::Crypto
)

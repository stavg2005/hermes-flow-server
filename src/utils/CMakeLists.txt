

add_library(utils STATIC
    Json2Graph.cpp
    S3Client.cpp
    S3Session.cpp
)

target_include_directories(utils
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)


# Clean and simple!
target_link_libraries(utils
  PUBLIC
    # Internal dependencies
    models

    # External dependencies
    hermes_dependencies
)

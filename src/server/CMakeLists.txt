

add_library(server STATIC
    ActiveSessions.cpp
    http_session.cpp
    io_context_pool.cpp
    Listener.cpp
    Router.cpp
    Session.cpp
)

target_include_directories(server
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Clean and simple!
target_link_libraries(server
  PUBLIC
    # Internal dependencies
    utils   # (This transitively pulls in 'models')

    # External dependencies
    hermes_dependencies
)

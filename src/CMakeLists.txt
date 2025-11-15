# Add our three library modules
add_subdirectory(models)
add_subdirectory(utils)
add_subdirectory(server)

# Define our main executable
add_executable(Server
    main.cpp
)

# Link the executable against the core 'server' library
target_link_libraries(Server
    PRIVATE
        server
        hermes_dependencies
)


# --- APPLY PLATFORM/BUILD SETTINGS ---



if(HERMES_USE_IO_URING)
    target_compile_definitions(Server
        PRIVATE
            BOOST_ASIO_HAS_IO_URING
            BOOST_ASIO_DISABLE_EPOLL
    )

endif()

if(WIN32)
    target_compile_definitions(Server
        PRIVATE
            WIN32_LEAN_AND_MEAN
            DNOMINMAX
    )
endif()

# (Copy your full sanitizer logic block here)
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

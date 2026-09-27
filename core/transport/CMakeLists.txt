add_library(catro_transport STATIC
    src/udp_peer_socket.cpp
)
add_library(Catro::Transport ALIAS catro_transport)

target_include_directories(catro_transport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    target_link_libraries(catro_transport PUBLIC ws2_32)
    target_compile_definitions(catro_transport PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00
    )
endif()

catro_enable_warnings(catro_transport)

# Portable real-time audio primitives and the engine. Platform backends implement AudioPlatform.
add_library(catro_audio STATIC
    src/engine.cpp
    src/external_session.cpp
)
add_library(Catro::Audio ALIAS catro_audio)

target_include_directories(catro_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(catro_audio PUBLIC Catro::Capabilities)
catro_enable_warnings(catro_audio)

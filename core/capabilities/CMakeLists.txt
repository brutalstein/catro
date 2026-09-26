add_library(catro_capabilities INTERFACE)
add_library(Catro::Capabilities ALIAS catro_capabilities)

target_include_directories(
    catro_capabilities
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

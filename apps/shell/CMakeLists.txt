add_library(catro_shell STATIC
    AppUpdate.cpp
    PresentationState.cpp
    ShellModel.cpp
)
add_library(Catro::Shell ALIAS catro_shell)
target_compile_definitions(catro_shell PRIVATE CATRO_APP_VERSION="${PROJECT_VERSION}")

target_include_directories(catro_shell PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(catro_shell PUBLIC Catro::Community)
catro_enable_warnings(catro_shell)

add_library(catro_shell STATIC ShellModel.cpp)
add_library(Catro::Shell ALIAS catro_shell)

target_include_directories(catro_shell PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
catro_enable_warnings(catro_shell)

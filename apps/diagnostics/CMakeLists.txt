# Plain C++ presentation shared by the native shells. CMake builds it for tests; the Windows shell
# links it through the CatroCoreRoot property and the macOS bridge links it directly.
add_library(catro_diagnostics STATIC DiagnosticsViewModel.cpp AudioViewModel.cpp)
target_include_directories(catro_diagnostics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(catro_diagnostics PUBLIC Catro::Reporting Catro::Audio)
catro_enable_warnings(catro_diagnostics)

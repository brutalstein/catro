# The shell builds with MSBuild (Catro.sln). Its view model is plain C++, so CMake builds it for
# tests and the shell links the result through the CatroCoreRoot property.
add_library(catro_windows_diagnostics STATIC Catro/Diagnostics/DiagnosticsViewModel.cpp)
target_include_directories(catro_windows_diagnostics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Catro)
target_link_libraries(catro_windows_diagnostics PUBLIC Catro::Reporting)
catro_enable_warnings(catro_windows_diagnostics)

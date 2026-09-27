add_library(catro_platform_windows STATIC
    src/capability_service.cpp
    src/process_probe_executor.cpp
    src/probe_dispatch.cpp
    src/probes/system_probe.cpp
    src/probes/runtime_probe.cpp
    src/probes/gpu_display_probe.cpp
    src/windows_translation.cpp
)
add_library(Catro::Windows ALIAS catro_platform_windows)

target_include_directories(catro_platform_windows PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(catro_platform_windows
    PUBLIC Catro::Capabilities
    PRIVATE Catro::Reporting powrprof user32 wtsapi32 dxgi d3d11 d3d12 dxcore shcore windowsapp
)
target_compile_definitions(catro_platform_windows PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00
)
catro_enable_warnings(catro_platform_windows)

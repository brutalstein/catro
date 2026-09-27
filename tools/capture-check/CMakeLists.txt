if(NOT (WIN32 AND MSVC))
    return()
endif()

add_library(catro_capture_check_cli STATIC capture_check.cpp)
target_include_directories(catro_capture_check_cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
catro_enable_warnings(catro_capture_check_cli)

add_executable(catro_capture_check main.cpp)
set_target_properties(catro_capture_check PROPERTIES OUTPUT_NAME "catro-capture-check")
target_link_libraries(catro_capture_check PRIVATE catro_capture_check_cli Catro::Windows)
target_compile_definitions(catro_capture_check PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00
)
catro_enable_warnings(catro_capture_check)

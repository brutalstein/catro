if(NOT (WIN32 AND MSVC))
    return()
endif()

add_library(catro_encode_check_cli STATIC encode_check.cpp)
target_include_directories(catro_encode_check_cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(catro_encode_check_cli PUBLIC Catro::Windows)
catro_enable_warnings(catro_encode_check_cli)

add_executable(catro_encode_check main.cpp)
set_target_properties(catro_encode_check PROPERTIES OUTPUT_NAME "catro-encode-check")
target_link_libraries(catro_encode_check PRIVATE catro_encode_check_cli Catro::Video)
target_compile_definitions(catro_encode_check PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00
)
catro_enable_warnings(catro_encode_check)

add_library(catro_capabilities STATIC
    src/validation.cpp
    src/snapshot_diff.cpp
)
add_library(Catro::Capabilities ALIAS catro_capabilities)

target_include_directories(catro_capabilities PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(catro_capabilities PUBLIC cxx_std_20)
catro_enable_warnings(catro_capabilities)

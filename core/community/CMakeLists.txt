add_library(catro_community STATIC
    src/directory.cpp
    src/model.cpp
    src/state_codec.cpp
)
add_library(Catro::Community ALIAS catro_community)

target_include_directories(catro_community PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(catro_community PRIVATE nlohmann_json::nlohmann_json)
catro_enable_warnings(catro_community)

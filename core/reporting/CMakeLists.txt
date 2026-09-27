# Reporting is a projection of the capability model; its JSON dependency never reaches the core.
add_library(catro_reporting STATIC
    src/canonical_json.cpp
    src/human_report.cpp
)
add_library(Catro::Reporting ALIAS catro_reporting)

target_include_directories(catro_reporting PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(catro_reporting
    PUBLIC Catro::Capabilities
    PRIVATE nlohmann_json::nlohmann_json
)
catro_enable_warnings(catro_reporting)

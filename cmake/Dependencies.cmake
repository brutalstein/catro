include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG 65ee68451d8eb2b5f3a30b410476ab83deb3289b
    GIT_PROGRESS TRUE
    # Third-party headers are not held to Catro's warnings-as-errors policy.
    SYSTEM
)

function(catro_enable_reporting_dependency)
    FetchContent_MakeAvailable(nlohmann_json)
endfunction()

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG 95d8a61b089317bec800c7cc4c64064cbcb3802d
    GIT_PROGRESS TRUE
)

function(catro_enable_test_dependencies)
    FetchContent_MakeAvailable(Catch2)
endfunction()

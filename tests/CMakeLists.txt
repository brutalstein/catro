add_library(catro_test_fixtures STATIC fixtures/capability_fixtures.cpp)
target_include_directories(catro_test_fixtures PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(catro_test_fixtures PUBLIC Catro::Capabilities)
catro_enable_warnings(catro_test_fixtures)

# One executable per suite so `ctest -R catro_<suite>` runs a focused set.
function(catro_add_test name source)
    add_executable(catro_${name}_test ${source})
    target_link_libraries(catro_${name}_test PRIVATE catro_test_fixtures Catch2::Catch2WithMain)
    catro_enable_warnings(catro_${name}_test)
    add_test(NAME catro_${name} COMMAND catro_${name}_test)
endfunction()

catro_add_test(model capabilities/model_test.cpp)
catro_add_test(validation capabilities/validation_test.cpp)
catro_add_test(snapshot_diff capabilities/snapshot_diff_test.cpp)
catro_add_test(operating_profile capabilities/operating_profile_test.cpp)
catro_add_test(local_envelope capabilities/local_envelope_test.cpp)

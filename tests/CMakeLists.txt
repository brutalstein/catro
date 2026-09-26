add_executable(catro_model_test capabilities/model_test.cpp)
target_link_libraries(catro_model_test PRIVATE Catro::Capabilities Catch2::Catch2WithMain)
catro_enable_warnings(catro_model_test)

add_test(NAME catro_model COMMAND catro_model_test)

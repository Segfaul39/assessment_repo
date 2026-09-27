cmake_minimum_required(VERSION 3.16)
project(robomaster_assessment LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(assessment_contracts INTERFACE)
target_include_directories(assessment_contracts INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(contract_compile_test tests/contract_compile_test.cpp)
target_link_libraries(contract_compile_test PRIVATE assessment_contracts)

include(CTest)
if(BUILD_TESTING)
  find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
  add_executable(starter_regression_test tests/starter_regression_test.cpp)
  target_link_libraries(starter_regression_test PRIVATE assessment_contracts)
  add_test(NAME contract_compile_test COMMAND contract_compile_test)
  add_test(NAME starter_regression_test COMMAND starter_regression_test)
  add_test(NAME public_data_contracts
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/public_data_test.py)
  add_test(NAME f6_review_checkpoints
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/f6_public_data_test.py)
  add_test(NAME f5_public_evaluation
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/f5_public_evaluation.py self-test)
endif()

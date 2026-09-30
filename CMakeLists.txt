cmake_minimum_required(VERSION 3.16)
project(robomaster_assessment LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 改动过的源文件带中文注释，已存为 UTF-8 with BOM；这里再显式指定 /utf-8 作为
# MSVC 的第二道保险（其他编译器无影响）。
if(MSVC)
  add_compile_options(/utf-8)
endif()

# MinGW 默认动态链接 libstdc++。Git Bash 的 PATH 上可能先命中另一套 MinGW 的同名 DLL，
# 版本不匹配会直接以 0xc0000139（找不到入口点）退出，与源码无关。静态链接后
# build/ 下的可执行文件自包含，可在任意终端运行。
if(MINGW)
  add_link_options(-static -static-libgcc -static-libstdc++)
endif()

add_library(assessment_contracts INTERFACE)
target_include_directories(assessment_contracts INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(contract_compile_test tests/contract_compile_test.cpp)
target_link_libraries(contract_compile_test PRIVATE assessment_contracts)

#f5
add_executable(f5_sequence tests/f5_sequence.cpp)
target_link_libraries(f5_sequence PRIVATE assessment_contracts)

#f6
add_executable(f6_replay tests/f6_replay.cpp)
target_link_libraries(f6_replay PRIVATE assessment_contracts)

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
  
  add_test(NAME f5_sequence
    COMMAND f5_sequence 
      ${CMAKE_CURRENT_SOURCE_DIR}/data/F5/F5_latency_sequence.csv)
  add_test(NAME f6_replay
    COMMAND f6_replay
      ${CMAKE_CURRENT_SOURCE_DIR}/data/F6/F6_detections.jsonl
      ${CMAKE_CURRENT_SOURCE_DIR}/data/F6/F6_tracking.jsonl
      ${CMAKE_CURRENT_SOURCE_DIR}/data/F6/F6_commands.jsonl)
endif()


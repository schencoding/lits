cmake_minimum_required(VERSION 3.10)

project(litsPlus)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-O3 -march=native)

include_directories(
    ${CMAKE_SOURCE_DIR}/litsPlus/src   
    ${CMAKE_SOURCE_DIR}/litsPlus/hot_src/src
    ${CMAKE_SOURCE_DIR}/microbench   
)

add_executable(bench microbench/bench.cpp)
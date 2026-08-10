cmake_minimum_required(VERSION 3.10)

project(litmt)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

find_package(TBB)
if(TBB_FOUND)
  set(TBB_LIB TBB::tbb)
else()
  find_library(TBB_LIB tbb)
endif()

find_library(JEMALLOC_LIB jemalloc)

add_library(bench_common INTERFACE)
target_include_directories(bench_common INTERFACE
  ${CMAKE_SOURCE_DIR}/litmt
)
target_compile_options(bench_common INTERFACE -O3 -march=native -DNDEBUG -fno-semantic-interposition)
target_link_options(bench_common INTERFACE -fno-semantic-interposition -fno-plt)
target_link_libraries(bench_common INTERFACE
  ${TBB_LIB}
  Threads::Threads
)
target_link_libraries(bench_common INTERFACE ${JEMALLOC_LIB})

function(add_bench exe src)
  add_executable(${exe} ${src})
  target_link_libraries(${exe} PRIVATE bench_common)
endfunction()

add_bench(insertScalability  microbench/insertScalability.cpp)

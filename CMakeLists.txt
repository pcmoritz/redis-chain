cmake_minimum_required(VERSION 3.4)

include_directories(${CMAKE_SOURCE_DIR}/redis/src/)

project(redis_chain)

set (CMAKE_CXX_STANDARD 11)

if(APPLE)
  set(REDIS_MODULE_CFLAGS -W -Wall -dynamic -fno-common -g -ggdb -std=c++11 -O2)
  set(REDIS_MODULE_LDFLAGS "-undefined dynamic_lookup")
  set(CMAKE_SHARED_LIBRARY_SUFFIX ".so")
else()
  set(REDIS_MODULE_CFLAGS -W -Wall -fno-common -g -ggdb -std=c++11 -O2)
  set(REDIS_MODULE_LDFLAGS -shared)
endif()

add_library(redis_chain_async SHARED chain_async.cc)
target_compile_options(redis_chain_async PUBLIC ${REDIS_MODULE_CFLAGS} -fPIC)
target_link_libraries(redis_chain_async ${REDIS_MODULE_LDFLAGS})

add_library(redis_chain SHARED chain.cc)
target_compile_options(redis_chain PUBLIC ${REDIS_MODULE_CFLAGS} -fPIC)
target_link_libraries(redis_chain ${REDIS_MODULE_LDFLAGS})

add_executable(benchmark benchmark.cc)
target_link_libraries(benchmark ${CMAKE_SOURCE_DIR}/hiredis/libhiredis.a)

add_executable(benchmark_async benchmark_async.cc ${CMAKE_SOURCE_DIR}/redis/src/ae.c ${CMAKE_SOURCE_DIR}/redis/src/zmalloc.c)
target_link_libraries(benchmark_async ${CMAKE_SOURCE_DIR}/hiredis/libhiredis.a)

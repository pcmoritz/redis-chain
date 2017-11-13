cmake_minimum_required(VERSION 3.4)

project(redis_chain)

if(APPLE)
  set(REDIS_MODULE_CFLAGS -W -Wall -dynamic -fno-common -g -ggdb -std=c++11 -O2)
  set(REDIS_MODULE_LDFLAGS "-undefined dynamic_lookup")
  set(CMAKE_SHARED_LIBRARY_SUFFIX ".so")
else()
  set(REDIS_MODULE_CFLAGS -W -Wall -fno-common -g -ggdb -std=c++11 -O2)
  set(REDIS_MODULE_LDFLAGS -shared)
endif()

add_library(redis_chain SHARED chain.cc)

target_compile_options(redis_chain PUBLIC ${REDIS_MODULE_CFLAGS} -fPIC)
target_link_libraries(redis_chain ${REDIS_MODULE_LDFLAGS})

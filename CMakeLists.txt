cmake_minimum_required(VERSION 3.0.0)

project(aiopp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS on)

# Define functions after add_subdirectory, so they are not overwritten
include(cmake/wall.cmake)

if(AIOPP_ENABLE_ASAN)
  include(cmake/asan.cmake)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O2")
set(SRC
  completermap.cpp
  eventfd.cpp
  fd.cpp
  ioqueue.cpp
  iouring.cpp
  log.cpp
  net.cpp
  socket.cpp
  threadpool.cpp
  util.cpp
)
list(TRANSFORM SRC PREPEND src/)

find_path(LIBURING_INCLUDE_DIR NAMES liburing.h)
find_library(LIBURING_LIBRARY NAMES uring)

if((NOT LIBURING_INCLUDE_DIR) OR(NOT LIBURING_LIBRARY))
  message(FATAL_ERROR "Unable to find liburing")
endif()

add_library(aiopp ${SRC})
target_include_directories(aiopp PUBLIC include)
target_include_directories(aiopp PUBLIC ${LIBURING_INCLUDE_DIR})
target_link_libraries(aiopp PUBLIC ${LIBURING_LIBRARY})
set_wall(aiopp)

find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)

add_library(example-lib examples/spdlogger.cpp)
target_link_libraries(example-lib aiopp spdlog fmt)

add_executable(echo-tcp examples/echo-tcp.cpp)
target_link_libraries(echo-tcp example-lib)
set_wall(echo-tcp)

add_executable(echo-udp examples/echo-udp.cpp)
target_link_libraries(echo-udp example-lib)
set_wall(echo-udp)

add_executable(echo-tcp-coro examples/echo-tcp-coro.cpp)
target_link_libraries(echo-tcp-coro example-lib)
set_wall(echo-tcp-coro)

add_executable(echo-udp-coro examples/echo-udp-coro.cpp)
target_link_libraries(echo-udp-coro example-lib)
set_wall(echo-udp-coro)

add_executable(resolve examples/resolve.cpp)
target_link_libraries(resolve example-lib)
set_wall(resolve)

add_executable(channel examples/channel.cpp)
target_link_libraries(channel example-lib)
set_wall(channel)

add_executable(http-coro examples/http-coro.cpp)
target_link_libraries(http-coro example-lib)
set_wall(http-coro)

add_executable(http examples/http.cpp)
target_link_libraries(http example-lib)
set_wall(http)

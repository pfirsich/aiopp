cmake_minimum_required(VERSION 3.0.0)

project(aiopp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS on)

# Define functions after add_subdirectory, so they are not overwritten
include(cmake/wall.cmake)

if(AIOPP_ENABLE_ASAN)
  include(cmake/asan.cmake)
endif()

set(SRC
  events.cpp
  fd.cpp
  ioqueue.cpp
  iouring.cpp
  log.cpp
  socket.cpp
  util.cpp
)
list(TRANSFORM SRC PREPEND src/)

add_library(aiopp ${SRC})
target_include_directories(aiopp PUBLIC include)
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

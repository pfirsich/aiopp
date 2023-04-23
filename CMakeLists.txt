cmake_minimum_required(VERSION 3.0.0)

project(aiopp)

set(CMAKE_CXX_STANDARD 17)
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

add_executable(echo-tcp src/echo-tcp.cpp src/spdlogger.cpp)
target_link_libraries(echo-tcp aiopp)
target_link_libraries(echo-tcp spdlog)
target_link_libraries(echo-tcp fmt)
set_wall(echo-tcp)

add_executable(echo-udp src/echo-udp.cpp src/spdlogger.cpp)
target_link_libraries(echo-udp aiopp)
target_link_libraries(echo-udp spdlog)
target_link_libraries(echo-udp fmt)
set_wall(echo-udp)

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
  util.cpp
)
list(TRANSFORM SRC PREPEND src/)

add_library(aiopp ${SRC})
target_include_directories(aiopp PUBLIC include)
set_wall(aiopp)

find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)

add_executable(http src/http.cpp)
target_link_libraries(http aiopp)
target_link_libraries(http spdlog)
target_link_libraries(http fmt)
set_wall(http)

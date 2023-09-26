cmake_minimum_required(VERSION 3.3.0)

project(aiopp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS on)

# Define functions after add_subdirectory, so they are not overwritten
include(cmake/wall.cmake)

if(AIOPP_ENABLE_ASAN)
  include(cmake/asan.cmake)
endif()

set(AIOPP_IOQUEUE_DEFAULT_BACKEND iouring)
set(AIOPP_IOQUEUE_BACKEND_OPTIONS iouring)
set(AIOPP_IOQUEUE_BACKEND "${AIOPP_IOQUEUE_DEFAULT_BACKEND}" CACHE STRING "Which backend to use for IO (one of ${AIOPP_IOQUEUE_BACKEND_OPTIONS})")

message("IO Queue Backend: ${AIOPP_IOQUEUE_BACKEND}")

if(NOT ${AIOPP_IOQUEUE_BACKEND} IN_LIST AIOPP_IOQUEUE_BACKEND_OPTIONS)
  message(FATAL_ERROR "AIOPP_IOQUEUE_BACKEND must be one of ${AIOPP_IOQUEUE_BACKEND_OPTIONS}")
endif()

set(SRC
  completermap.cpp
  eventfd.cpp
  fd.cpp
  ioqueue.cpp
  ioqueue_impl_${AIOPP_IOQUEUE_BACKEND}.cpp
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

if(AIOPP_IOQUEUE_BACKEND STREQUAL iouring)
  target_compile_definitions(aiopp PRIVATE AIOPP_IOQUEUE_BACKEND_IOURING)
endif()

target_include_directories(aiopp PUBLIC include)
target_include_directories(aiopp PUBLIC ${LIBURING_INCLUDE_DIR})
target_link_libraries(aiopp PUBLIC ${LIBURING_LIBRARY})
set_wall(aiopp)

# This will only be true if this project is not used as a subdirectory (e.g. FetchContent)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  option(AIOPP_BUILD_EXAMPLES "Whether to build examples" ON)

  if(AIOPP_BUILD_EXAMPLES)
    add_subdirectory(examples)
  endif()
endif()

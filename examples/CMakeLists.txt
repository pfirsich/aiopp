find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)

add_library(example-lib spdlogger.cpp)
target_link_libraries(example-lib aiopp spdlog fmt)

add_executable(echo-tcp echo-tcp.cpp)
target_link_libraries(echo-tcp example-lib)
set_wall(echo-tcp)

add_executable(echo-udp echo-udp.cpp)
target_link_libraries(echo-udp example-lib)
set_wall(echo-udp)

add_executable(echo-tcp-coro echo-tcp-coro.cpp)
target_link_libraries(echo-tcp-coro example-lib)
set_wall(echo-tcp-coro)

add_executable(echo-udp-coro echo-udp-coro.cpp)
target_link_libraries(echo-udp-coro example-lib)
set_wall(echo-udp-coro)

add_executable(resolve resolve.cpp)
target_link_libraries(resolve example-lib)
set_wall(resolve)

add_executable(channel channel.cpp)
target_link_libraries(channel example-lib)
set_wall(channel)

add_executable(http-coro http-coro.cpp)
target_link_libraries(http-coro example-lib)
set_wall(http-coro)

add_executable(proxy-coro proxy-coro.cpp)
target_link_libraries(proxy-coro example-lib)
set_wall(proxy-coro)

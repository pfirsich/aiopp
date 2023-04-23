# aiopp
This is a library for asynchronous IO using io_uring for C++20.
Most of the code in this repository was once part of [liburingpp](https://github.com/pfirsich/liburingpp) and [htcpp](https://github.com/pfirsich/htcpp) and some of it is still in there until it is properly integrated into this repository (mostly TLS related).

It provides an `IoQueue` abstraction to issue IO operations with completion callbacks (see [examples](./examples)).

There is also early support for issuing IO operations on an `IoQueue` using coroutines, examples of which can also be found in the examples directory.

## To Do
* Build coroutine support right into IoQueue so we don't have to take a detour through callbacks
* Move all the TLS stuff from [htcpp](https://github.com/pfirsich/htcpp) into this repository

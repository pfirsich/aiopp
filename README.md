# aiopp
This is a library for asynchronous IO using io_uring for C++20.
Most of the code in this repository was once part of [liburingpp](https://github.com/pfirsich/liburingpp) and [htcpp](https://github.com/pfirsich/htcpp) and some of it is still in there until it is properly integrated into this repository (mostly TLS related).

It provides an `IoQueue` abstraction to issue IO operations with completion callbacks (see [examples](./examples)).

There is also early support for issuing IO operations on an `IoQueue` using coroutines, examples of which can also be found in the examples directory.

## Building
It's a pretty boring CMake project, but it simply uses `find_path`/`find_library` to find liburing and `find_package` to find spdlog and fmt, so make sure to install those with your system package manager. On PopOS they are named `liburing-dev`, `libspdlog-dev` and `libfmt-dev` respectively.

Then just do "ye old CMake dance":
```bash
$ cmake -B build -G Ninja
$ cmake --build build
```

## To Do
* Optimize CompleterMap (linear probing is BAD) and CompleterMap is a bottleneck (~15% of runtime)
* Multi-Shot Accept
* Test if I can get rid of the whole callback path with just adding `Awaitable::execute(Func func)`, i.e. if using it has any measurable cost, which I want to be able to avoid. If there is no significant cost, cut the whole callback path.
* Figure out and finish timeouts and cancellation
* Add an `TcpStream` and then `SslStream`.
* Move all the TLS stuff from [htcpp](https://github.com/pfirsich/htcpp) into this repository

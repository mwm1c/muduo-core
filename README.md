## Project Introduction

This project is a multi-threaded network library based on the multi-Reactor model, inspired by muduo. It is implemented in C++11 and removes muduo's dependency on boost.

The project has implemented the Channel module, Poller module, event loop module, logging module, thread pool module, and consistent hash polling algorithm.

## Development Environment

* Linux version 6.8.0-59-generic (buildd@lcy02-amd64-117) 
* gcc version 11.4.0 (Ubuntu 11.4.0-1ubuntu1~22.04.2)
* cmake version 3.22.1

## Concurrency Model

![image.png](https://cdn.nlark.com/yuque/0/2022/png/26752078/1670853134528-c88d27f2-10a2-46d3-b308-48f7632a2f09.png?x-oss-process=image%2Fresize%2Cw_937%2Climit_0)

The project adopts a master-slave multi-Reactor multi-threaded model. The MainReactor is only responsible for listening and dispatching new connections. In the MainReactor, new connections are accepted via Acceptor and dispatched to SubReactors using a designed polling algorithm. SubReactors handle read and write events for these connections.

After calling the start function of TcpServer, a thread pool is created internally. Each thread runs an independent event loop, i.e., a SubReactor. The MainReactor polls the thread pool to obtain a SubReactor and dispatches new connections to it. The number of SubReactors handling read/write events is usually equal to the number of CPU cores. The master-slave Reactor model has several advantages:

1. Fast response, not blocked by a single synchronous event, although the Reactor itself is still synchronous;
2. Minimizes complex multi-threading and synchronization issues, and avoids frequent thread/process switching;
3. Good scalability, easy to fully utilize CPU resources by increasing the number of Reactor instances;
4. Good reusability, as the Reactor model itself is independent of specific event handling logic.

## Build Instructions

Install basic tools:

```shell
sudo apt-get update
sudo apt-get install -y wget cmake build-essential unzip git
```

## Compilation Instructions

Clone the project:

```shell
git clone git@github.com:mwm1c/muduo-core.git
```

Enter the muduo-core directory:
```shell
cd muduo-core
```

Create a build folder and enter it:
```shell
mkdir build && cd build
```

Then generate the executable program:
```shell
cmake .. && make -j${nproc}
```

To run the program, enter the example folder and execute the executable:
```shell
cd example  &&  ./testserver
```

## Feature Introduction

- **Event Polling and Dispatch Module**: `EventLoop.*`, `Channel.*`, `Poller.*`, `EPollPoller.*` are responsible for event polling and dispatching. `EventLoop` polls `Poller`, which is implemented by `EPollPoller` at the bottom.
- **Thread and Event Binding Module**: `Thread.*`, `EventLoopThread.*`, `EventLoopThreadPool.*` bind threads and event loops, implementing the "one loop per thread" model.
- **Network Connection Module**: `TcpServer.*`, `TcpConnection.*`, `Acceptor.*`, `Socket.*` handle network connections in the main loop and dispatch them to subloops.
- **Buffer Module**: `Buffer.*` provides an auto-expanding buffer to ensure data arrives in order.

## Technical Highlights

1. **High-Concurrency Non-Blocking Network Library**  
   `muduo` uses a combination of Reactor models and multi-threading to achieve a high-concurrency non-blocking network library.

2. **Smart Pointer to Prevent Dangling Pointers**  
   `TcpConnection` inherits from `enable_shared_from_this` to prevent objects from being released in inappropriate places, which could cause dangling pointers.  
   This avoids the risk of users deleting objects during the OnMessage event, ensuring `TcpConnection` is released correctly.

3. **Wakeup Mechanism**  
   `EventLoop` uses `eventfd` to call `wakeup()`, allowing the main loop to wake up the subloop's `epoll_wait` blocking.

4. **Consistent Hash Polling Algorithm**  
   The newly added `ConsistenHash` header file uses a consistent hash polling algorithm to reasonably distribute `EventLoop` to each `TcpConnection` object.  
   It also supports custom hash functions to meet high concurrency needs. Note that the number of virtual nodes should not be too small.

5. **Thread Creation Orderliness**  
   In `Thread`, C++ lambda expressions and semaphore mechanisms are used to ensure orderly thread creation, making sure threads are created normally before executing thread functions.

6. **Non-Blocking Core Buffer**  
   `Buffer.*` is the core module for non-blocking in the muduo network library. When read/write events are triggered, the kernel buffer may not have enough space to send all data at once. There are two options:  
   - First, set it to non-blocking, which may cause CPU busy-waiting;  
   - Second, block and wait for kernel buffer space, which is inefficient.  

   To solve these problems, the Buffer module stores excess data in the user buffer and registers corresponding read/write event listeners, sending all data when the event is triggered again.

7. **Flexible Logging Module**  
   `Logger` supports setting log levels. During debugging, you can enable DEBUG mode to print logs; when the server is running, you can disable DEBUG logs to reduce performance impact.

## Optimization Directions

- Improve memory pool, asynchronous log buffer, timer, and connection pool.
- Add more test cases, such as HTTP and RPC.
- Consider introducing coroutine libraries and other modules.
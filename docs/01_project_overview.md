# 01. 项目概览与快速开始

## 项目简介

`mycoroutine` 是一个小型 C++17 协程运行时库，核心目标是用尽量少的代码展示系统级协程库的关键路径：
- 协程上下文切换（`ucontext`）
- 任务调度（`Scheduler`）
- IO 事件驱动（`IOManager` + `epoll`）
- 定时器管理（`TimerManager`）
- 阻塞系统调用协程化（`hook`）

## 项目目标

- 提供一个可编译、可运行、可阅读的协程库样例
- 清晰展示“协程 + 调度 + IO + 超时”如何组合
- 为后续扩展（调度策略、内存管理、网络库）提供基础

## 协程库解决的问题

- 用协程替代“每请求一个线程”的高成本模型
- 将阻塞式 IO 调用转为“事件等待 + 协程挂起/恢复”
- 在多线程下统一调度 CPU 任务、IO 任务、定时任务

## 项目特点

- 有栈协程（stackful），通过 `ucontext` 切换
- 支持提交协程对象或回调函数到调度器
- `epoll + eventfd` 事件循环，支持跨线程唤醒
- 定时器支持一次性、循环、条件触发
- Hook `read/write/connect/accept/sleep` 等阻塞调用
- 提供 `Thread`、`FdManager`、日志工具等基础设施

## 目录结构

```text
.
├── CMakeLists.txt
├── CMakePresets.json
├── include/mycoroutine/
│   ├── fiber.h
│   ├── scheduler.h
│   ├── iomanager.h
│   ├── timer.h
│   ├── hook.h
│   ├── fd_manager.h
│   ├── thread.h
│   └── utils.h
├── src/
│   ├── fiber.cpp
│   ├── scheduler.cpp
│   ├── iomanager.cpp
│   ├── timer.cpp
│   ├── hook.cpp
│   ├── fd_manager.cpp
│   ├── thread.cpp
│   └── utils.cpp
├── examples/
│   └── coroutine_http_server.cpp
├── tests/
│   ├── smoke/main.cpp
│   ├── epoll/main.cpp
│   └── libevent/main.cpp
└── docs/
```

## 编译与运行

### 使用 CMake Presets（推荐）

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

运行示例：

```bash
./build/debug/examples/coroutine_http_server
```

### 手动构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## API 简要说明

### Fiber

- `Fiber(std::function<void()>, size_t stacksize=0, bool run_in_scheduler=true)`
- `resume()` / `yield()`
- `reset(cb)`
- `static Fiber::GetThis()`

### Scheduler

- `scheduleLock(fiber_or_cb, thread=-1)`
- `start()` / `stop()`
- `static Scheduler::GetThis()`

### IOManager

- `addEvent(fd, READ/WRITE, cb)`
- `delEvent(fd, event)`
- `cancelEvent(fd, event)` / `cancelAll(fd)`
- 继承了 `TimerManager` 的定时器能力

### TimerManager

- `addTimer(ms, cb, recurring=false)`
- `addConditionTimer(ms, cb, weak_cond, recurring=false)`
- `getNextTimer()` / `listExpiredCb(...)`

### Hook

- `set_hook_enable(bool)`
- `is_hook_enable()`

注：当前实现中 `Scheduler::run()` 内自动开启 Hook 的代码是注释状态；若要启用 IO 协程化，请在工作线程显式调用 `set_hook_enable(true)`。
